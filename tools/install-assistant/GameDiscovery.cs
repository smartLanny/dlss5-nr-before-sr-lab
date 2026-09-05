using System.Diagnostics;
using System.Reflection.PortableExecutable;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace Dlss5InstallAssistant;

public static class SteamScanner
{
    public static IEnumerable<GameCandidate> Scan()
    {
        string? root = null;
        if (OperatingSystem.IsWindows()) { using var key = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Valve\Steam"); root = key?.GetValue("SteamPath") as string; }
        return Scan(root ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"));
    }
    public static IEnumerable<GameCandidate> Scan(string steam)
    {
        var result = new List<GameCandidate>(); var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.GetFullPath(steam) };
        try { foreach (Match m in Regex.Matches(File.ReadAllText(Path.Combine(steam, "steamapps", "libraryfolders.vdf")), "\"path\"\\s+\"([^\"]+)\"")) roots.Add(Path.GetFullPath(m.Groups[1].Value.Replace("\\\\", "\\"))); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        foreach (var root in roots)
        {
            var apps = Path.Combine(root, "steamapps"); if (!Directory.Exists(apps)) continue;
            IEnumerable<string> manifests; try { manifests = Directory.GetFiles(apps, "appmanifest_*.acf"); } catch { continue; }
            foreach (var manifest in manifests)
            {
                try
                {
                    var text = File.ReadAllText(manifest); var id = Regex.Match(Path.GetFileName(manifest), @"^appmanifest_(\d+)\.acf$").Groups[1].Value;
                    var name = Value(text, "name"); var folder = PackageManifest.SafePath(Path.Combine(apps, "common"), Value(text, "installdir"));
                    SafeFiles.RejectReparse(folder); if (!Directory.Exists(folder)) continue;
                    var selected = id switch { "1091500" => Path.Combine(folder, "bin", "x64", "Cyberpunk2077.exe"), "292030" => Path.Combine(folder, "bin", "x64_dx12", "witcher3.exe"), _ => "" };
                    if (File.Exists(selected)) result.Add(GameInspector.Inspect(selected, id, name));
                    else foreach (var exe in SafeExecutables(folder, 3).Take(12)) result.Add(GameInspector.Inspect(exe, id, name));
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException) { }
            }
        }
        return result.DistinctBy(x => x.Executable, StringComparer.OrdinalIgnoreCase);
    }
    static string Value(string text, string key) => Regex.Match(text, "\"" + key + "\"\\s+\"([^\"]+)\"").Groups[1].Value;
    static IEnumerable<string> SafeExecutables(string folder, int depth)
    {
        if (depth < 0) yield break;
        string[] files; string[] dirs;
        try { SafeFiles.RejectReparse(folder); files = Directory.GetFiles(folder, "*.exe"); dirs = Directory.GetDirectories(folder); } catch { yield break; }
        foreach (var f in files) yield return f;
        foreach (var d in dirs) foreach (var f in SafeExecutables(d, depth - 1)) yield return f;
    }
}

public static class GameInspector
{
    public static GameCandidate Inspect(string exe, string? appId, string? name = null)
    {
        exe = Path.GetFullPath(exe); var g = new GameCandidate { Executable = exe, SteamAppId = appId, Name = name ?? Path.GetFileNameWithoutExtension(exe) };
        try
        {
            exe = SafeFiles.CanonicalExistingPath(exe); g.Executable = exe; var imports = PeFacts.Imports(exe);
            g.Api = imports.Contains("d3d12.dll") ? "D3D12" : imports.Contains("d3d11.dll") ? "D3D11" : "未知";
            g.ExecutableSha256 = PackageManifest.Hash(exe);
            var version = FileVersionInfo.GetVersionInfo(exe); var leaf = Path.GetFileName(exe);
            var cp = leaf.Equals("Cyberpunk2077.exe", StringComparison.OrdinalIgnoreCase) && version.ProductName == "Cyberpunk 2077" && (version.CompanyName?.Contains("CD PROJEKT", StringComparison.OrdinalIgnoreCase) ?? false);
            var w3 = leaf.Equals("witcher3.exe", StringComparison.OrdinalIgnoreCase) && version.ProductName == "The Witcher 3" && Path.GetFileName(g.Directory).Equals("x64_dx12", StringComparison.OrdinalIgnoreCase) && (version.CompanyName?.Contains("CD Projekt", StringComparison.OrdinalIgnoreCase) ?? false);
            if (cp && (appId is null or "1091500")) g.ProfileId = "cyberpunk2077-d3d12";
            if (w3 && (appId is null or "292030")) g.ProfileId = "witcher3-d3d12";
            var sr = Path.Combine(g.Directory, "nvngx_dlss.dll"); var exports = PeFacts.Exports(sr);
            var native = exports.Contains("NVSDK_NGX_D3D12_EvaluateFeature") && exports.Contains("NVSDK_NGX_D3D12_CreateFeature");
            g.NativeDlss = native ? "发现 x64 NGX D3D12 SR" : "未确认原生 D3D12 SR";
            g.EvidenceConfirmed = PeFacts.IsX64(exe) && native && (g.Api == "D3D12" || (g.ProfileId.Length != 0 && imports.Contains("dxgi.dll")));
            if (g.EvidenceConfirmed) g.Api = "D3D12";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or BadImageFormatException or ArgumentException or System.ComponentModel.Win32Exception) { g.Status = "无法检查：" + ex.Message; return g; }
        g.Status = appId == "553850" ? "Helldivers 2：仅查看，不部署" : g.ProfileId.Length == 0 ? "仅查看：未验证的游戏" : g.EvidenceConfirmed ? "可检查安装计划" : "原生 D3D12 SR 尚未确认";
        return g;
    }
    public static void EnsureNotRunning(GameCandidate game)
    {
        var baseName = Path.GetFileNameWithoutExtension(game.Executable);
        foreach (var p in Process.GetProcessesByName(baseName))
        {
            using (p)
            {
                try { if (!p.HasExited && SafeFiles.PathEquals(p.MainModule?.FileName, game.Executable)) throw new InvalidOperationException("请先完全退出游戏，再更改安装。"); }
                catch (System.ComponentModel.Win32Exception) { throw new InvalidOperationException("同名游戏进程无法核实，暂不更改文件。"); }
            }
        }
    }
    internal static bool IsExactProcessRunning(GameCandidate game)
    {
        var baseName = Path.GetFileNameWithoutExtension(game.Executable);
        foreach (var p in Process.GetProcessesByName(baseName))
        {
            using (p)
            {
                try { if (!p.HasExited && SafeFiles.PathEquals(p.MainModule?.FileName, game.Executable)) return true; }
                catch (InvalidOperationException) { }
                catch (System.ComponentModel.Win32Exception) { throw new InvalidOperationException("同名游戏进程无法核实，启动保护尚不能安全移交。"); }
            }
        }
        return false;
    }
}

internal static class PeFacts
{
    internal static bool IsX64(string path) { try { using var stream = File.OpenRead(path); using var pe = new PEReader(stream); return pe.PEHeaders.CoffHeader.Machine == Machine.Amd64 && pe.PEHeaders.PEHeader?.Magic == PEMagic.PE32Plus; } catch { return false; } }
    internal static HashSet<string> Imports(string path)
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            using var stream = File.OpenRead(path); using var pe = new PEReader(stream); if (pe.PEHeaders.CoffHeader.Machine != Machine.Amd64) return names;
            var directory = pe.PEHeaders.PEHeader!.ImportTableDirectory; if (directory.RelativeVirtualAddress == 0) return names;
            var reader = pe.GetSectionData(directory.RelativeVirtualAddress).GetReader();
            for (var count = 0; count < 4096 && reader.RemainingBytes >= 20; count++)
            { reader.ReadUInt32(); reader.ReadUInt32(); reader.ReadUInt32(); var name = reader.ReadInt32(); reader.ReadUInt32(); if (name == 0) break; names.Add(CString(pe, name)); }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or BadImageFormatException or ArgumentException) { }
        return names;
    }
    internal static HashSet<string> Exports(string path)
    {
        var names = new HashSet<string>(StringComparer.Ordinal);
        try
        {
            using var stream = File.OpenRead(path); using var pe = new PEReader(stream); if (pe.PEHeaders.CoffHeader.Machine != Machine.Amd64) return names;
            var rva = pe.PEHeaders.PEHeader!.ExportTableDirectory.RelativeVirtualAddress; if (rva == 0) return names;
            var r = pe.GetSectionData(rva).GetReader(); if (r.RemainingBytes < 40) return names;
            r.Offset = 24; var count = r.ReadUInt32(); r.ReadUInt32(); var pointers = r.ReadInt32(); if (count > 65536) return names;
            var n = pe.GetSectionData(pointers).GetReader(); for (var i = 0; i < count && n.RemainingBytes >= 4; i++) names.Add(CString(pe, n.ReadInt32()));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or BadImageFormatException or ArgumentException) { }
        return names;
    }
    static string CString(PEReader pe, int rva) { var r = pe.GetSectionData(rva).GetReader(); var bytes = new List<byte>(); while (r.RemainingBytes > 0 && bytes.Count < 512) { var b = r.ReadByte(); if (b == 0) return Encoding.ASCII.GetString(bytes.ToArray()); bytes.Add(b); } throw new BadImageFormatException("PE string is not terminated."); }
}

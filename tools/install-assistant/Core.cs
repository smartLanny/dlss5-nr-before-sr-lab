using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Dlss5InstallAssistant;

public sealed class GameCandidate
{
    public string Name { get; set; } = "";
    public string Executable { get; set; } = "";
    public string Directory => Path.GetDirectoryName(Executable)!;
    public string Api { get; set; } = "Unknown";
    public string NativeDlss { get; set; } = "未确认";
    public string Status { get; set; } = "未部署";
    public string? SteamAppId { get; set; }
}

public sealed class PackageManifest
{
    public string Id { get; set; } = "";
    public string Build { get; set; } = "";
    public bool NativeD3D12 { get; set; } = true;
    public bool OnlineAllowed { get; set; }
    public List<PackageFile> Files { get; set; } = new();
    [System.Text.Json.Serialization.JsonIgnore] public string Root { get; private set; } = "";
    public static PackageManifest Load(string root)
    {
        var p = JsonSerializer.Deserialize<PackageManifest>(File.ReadAllText(Path.Combine(root, "manifest.json")), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? throw new InvalidDataException("manifest.json 无法读取");
        p.Root = Path.GetFullPath(root);
        if (string.IsNullOrWhiteSpace(p.Id) || p.Files.Count == 0) throw new InvalidDataException("缺少 id 或 files");
        foreach (var f in p.Files)
        {
            var rel = f.Path.Replace('/', '\\');
            if (Path.IsPathRooted(rel) || rel.Split('\\').Any(x => x == "..")) throw new InvalidDataException("manifest 路径非法：" + f.Path);
            var q = Path.Combine(p.Root, rel);
            if (!File.Exists(q)) throw new FileNotFoundException("包内文件缺失", f.Path);
            if (!Hash(q).Equals(f.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("SHA256 不匹配：" + f.Path);
            f.Path = rel;
        }
        return p;
    }
    internal static string Hash(string path) { using var h = SHA256.Create(); using var s = File.OpenRead(path); return Convert.ToHexString(h.ComputeHash(s)); }
}
public sealed class PackageFile { public string Path { get; set; } = ""; public string Sha256 { get; set; } = ""; public bool Required { get; set; } = true; }
public sealed class RuntimeStatus { public string Schema { get; set; } = "dlss5-runtime-status/v1"; public string State { get; set; } = "unknown"; public string BuildId { get; set; } = ""; public bool Active { get; set; } public string BypassReason { get; set; } = ""; public DateTimeOffset? UpdatedAt { get; set; } }

public static class SteamScanner
{
    public static IEnumerable<GameCandidate> Scan()
    {
        var steam = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam");
        var roots = new List<string>(); var vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        if (File.Exists(vdf)) foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"", RegexOptions.IgnoreCase)) roots.Add(m.Groups[1].Value.Replace("\\\\", "\\"));
        if (!roots.Contains(steam, StringComparer.OrdinalIgnoreCase)) roots.Add(steam);
        foreach (var root in roots.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var apps = Path.Combine(root, "steamapps"); if (!Directory.Exists(apps)) continue;
            foreach (var af in Directory.EnumerateFiles(apps, "appmanifest_*.acf"))
            {
                var text = File.ReadAllText(af); var name = Regex.Match(text, "\\\"name\\\"\\s+\\\"([^\\\"]+)\\\"").Groups[1].Value; var id = Regex.Match(Path.GetFileName(af), "(\\d+)").Groups[1].Value; var dir = Regex.Match(text, "\\\"installdir\\\"\\s+\\\"([^\\\"]+)\\\"").Groups[1].Value;
                var folder = Path.Combine(apps, "common", dir); if (!Directory.Exists(folder)) continue;
                var exe = Directory.EnumerateFiles(folder, "*.exe", SearchOption.TopDirectoryOnly).OrderByDescending(x => new FileInfo(x).Length).FirstOrDefault(); if (exe == null) continue;
                var g = GameInspector.Inspect(exe, id, name); if (g.Api == "D3D12" && g.NativeDlss.StartsWith("已发现", StringComparison.Ordinal)) yield return g;
            }
        }
    }
}

public static class GameInspector
{
    public static GameCandidate Inspect(string exe, string? appId, string? name = null)
    {
        var dir = Path.GetDirectoryName(exe)!; var dlls = Directory.EnumerateFiles(dir, "*.dll").Select(Path.GetFileName).ToArray(); var hasDlss = dlls.Any(x => x.Equals("nvngx_dlss.dll", StringComparison.OrdinalIgnoreCase));
        var api = HasImport(exe, "d3d12.dll") ? "D3D12" : HasImport(exe, "d3d11.dll") ? "D3D11" : "未知"; var hd2 = (name ?? exe).Contains("helldivers", StringComparison.OrdinalIgnoreCase);
        return new GameCandidate { Name = name ?? Path.GetFileNameWithoutExtension(exe), Executable = exe, SteamAppId = appId, Api = api, NativeDlss = hasDlss ? "已发现 nvngx_dlss.dll" : "未发现", Status = hd2 ? "Helldivers 2：禁止自动部署" : "未部署" };
    }
    static bool HasImport(string path, string dll) { try { return File.ReadAllBytes(path).AsSpan().IndexOf(System.Text.Encoding.ASCII.GetBytes(dll)) >= 0; } catch { return false; } }
}

public static class Deployer
{
    static string StateDir(GameCandidate g) => Path.Combine(g.Directory, ".dlss5-install-assistant");
    public static (bool Ok, string Message) Install(GameCandidate g, PackageManifest p)
    {
        if (g.Name.Contains("helldivers", StringComparison.OrdinalIgnoreCase)) return (false, "已拒绝：Helldivers 2 属于在线/反作弊游戏，本助手不自动部署。");
        if (!p.NativeD3D12 || !g.Api.Equals("D3D12", StringComparison.OrdinalIgnoreCase)) return (false, "已拒绝：包或游戏未确认 native D3D12。");
        var dxgi = Path.Combine(g.Directory, "dxgi.dll"); if (File.Exists(dxgi) && !LooksLikeReShade(dxgi)) return (false, "已拒绝：已有未知 dxgi.dll，未覆盖；请先人工确认。");
        var backup = Path.Combine(StateDir(g), "backups", DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff")); Directory.CreateDirectory(backup); var records = new List<BackupRecord>();
        try
        {
            foreach (var f in p.Files)
            {
                var dest = SafeDestination(g, f.Path); var had = File.Exists(dest); var rel = f.Path;
                if (had) { var bp = Path.Combine(backup, rel); Directory.CreateDirectory(Path.GetDirectoryName(bp)!); File.Copy(dest, bp, true); }
                var tmp = dest + ".dlss5-new"; File.Copy(Path.Combine(p.Root, f.Path), tmp, true); if (!PackageManifest.Hash(tmp).Equals(f.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("部署文件哈希不匹配：" + f.Path); File.Move(tmp, dest, true); records.Add(new BackupRecord { Path = rel, Before = had ? PackageManifest.Hash(Path.Combine(backup, rel)) : null, After = f.Sha256, HadOriginal = had });
            }
            File.WriteAllText(Path.Combine(backup, "transaction.json"), JsonSerializer.Serialize(records, new JsonSerializerOptions { WriteIndented = true })); File.WriteAllText(Path.Combine(StateDir(g), "installed.json"), JsonSerializer.Serialize(new { p.Id, p.Build, Files = p.Files }, new JsonSerializerOptions { WriteIndented = true })); g.Status = "已部署（运行时尚未确认）"; return (true, "安装完成：已备份并部署；启动后查看 runtime-status.json 才算运行时生效。");
        }
        catch { Rollback(g, backup, records); throw; }
    }
    public static string Uninstall(GameCandidate g)
    {
        var state = Path.Combine(StateDir(g), "installed.json"); if (!File.Exists(state)) return "没有助手部署记录。"; using var doc = JsonDocument.Parse(File.ReadAllText(state));
        foreach (var f in doc.RootElement.GetProperty("Files").EnumerateArray()) { var rel = f.GetProperty("Path").GetString()!; var dest = SafeDestination(g, rel); var expected = f.GetProperty("Sha256").GetString()!; if (File.Exists(dest) && PackageManifest.Hash(dest).Equals(expected, StringComparison.OrdinalIgnoreCase)) File.Delete(dest); }
        g.Status = "已卸载（用户后来改动的文件已保留）"; return "卸载完成；检测到后来被修改的文件已保留。";
    }
    public static string RestoreLatest(GameCandidate g) { var dir = Path.Combine(StateDir(g), "backups"); var b = Directory.Exists(dir) ? new DirectoryInfo(dir).GetDirectories().OrderByDescending(x => x.Name).FirstOrDefault() : null; if (b == null) return "没有可恢复备份。"; var tx = Path.Combine(b.FullName, "transaction.json"); if (!File.Exists(tx)) return "备份缺少事务记录，未操作。"; foreach (var r in JsonSerializer.Deserialize<List<BackupRecord>>(File.ReadAllText(tx)) ?? new()) if (r.HadOriginal && File.Exists(Path.Combine(b.FullName, r.Path))) File.Copy(Path.Combine(b.FullName, r.Path), SafeDestination(g, r.Path), true); return "已恢复最近备份。"; }
    static void Rollback(GameCandidate g, string backup, List<BackupRecord> records) { foreach (var r in records) { var dest = SafeDestination(g, r.Path); if (r.HadOriginal && File.Exists(Path.Combine(backup, r.Path))) File.Copy(Path.Combine(backup, r.Path), dest, true); else if (File.Exists(dest) && PackageManifest.Hash(dest).Equals(r.After, StringComparison.OrdinalIgnoreCase)) File.Delete(dest); } }
    static string SafeDestination(GameCandidate g, string rel) { var full = Path.GetFullPath(Path.Combine(g.Directory, rel)); if (!full.StartsWith(Path.GetFullPath(g.Directory) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("目标路径越界：" + rel); Directory.CreateDirectory(Path.GetDirectoryName(full)!); return full; }
    static bool LooksLikeReShade(string path) { try { return System.Text.Encoding.ASCII.GetString(File.ReadAllBytes(path)).Contains("ReShade", StringComparison.OrdinalIgnoreCase); } catch { return false; } }
    sealed class BackupRecord { public string Path { get; set; } = ""; public string? Before { get; set; } public string? After { get; set; } public bool HadOriginal { get; set; } }
}

public static class Launcher
{
    public static void Start(GameCandidate g) { if (!string.IsNullOrWhiteSpace(g.SteamAppId)) { Process.Start(new ProcessStartInfo("steam://rungameid/" + g.SteamAppId) { UseShellExecute = true }); return; } Process.Start(new ProcessStartInfo(g.Executable) { WorkingDirectory = g.Directory, UseShellExecute = true }); }
    public static RuntimeStatus? ReadStatus(GameCandidate g) { var path = Path.Combine(g.Directory, "runtime-status.json"); return File.Exists(path) ? JsonSerializer.Deserialize<RuntimeStatus>(File.ReadAllText(path)) : null; }
}

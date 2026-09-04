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
    public bool EvidenceConfirmed { get; set; }
}

public sealed class PackageManifest
{
    public string Id { get; set; } = "";
    public string Build { get; set; } = "";
    public bool NativeD3D12 { get; set; } = true;
    public bool OnlineAllowed { get; set; }
    public List<PackageFile> Files { get; set; } = new();
    public List<string> CompatibleProfiles { get; set; } = new();
    [System.Text.Json.Serialization.JsonIgnore] public string Root { get; private set; } = "";
    public static PackageManifest Load(string root)
    {
        var p = JsonSerializer.Deserialize<PackageManifest>(File.ReadAllText(Path.Combine(root, "manifest.json")), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? throw new InvalidDataException("manifest.json 无法读取");
        p.Root = Path.GetFullPath(root);
        if (string.IsNullOrWhiteSpace(p.Id) || string.IsNullOrWhiteSpace(p.Build) || p.Files.Count == 0 || p.CompatibleProfiles.Count == 0) throw new InvalidDataException("缺少 id/build/compatibleProfiles/files");
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
public sealed class PackageFile { public string Path { get; set; } = ""; public string Sha256 { get; set; } = ""; public string Role { get; set; } = "addon"; public string Source { get; set; } = "trusted-local"; public string Target { get; set; } = "game-root"; public bool Required { get; set; } = true; }
public sealed class RuntimeStatus { public string Schema { get; set; } = "dlss5-runtime-status/v1"; public string State { get; set; } = "unknown"; public string BuildId { get; set; } = ""; public bool Active { get; set; } public string BypassReason { get; set; } = ""; public DateTimeOffset? UpdatedAt { get; set; } public int? SessionPid { get; set; } public DateTimeOffset? SessionStartedAt { get; set; } public int? ProcessId { get; set; } public DateTimeOffset? ProcessStartedAt { get; set; } public string Executable { get; set; } = ""; public bool NativeSrObserved { get; set; } public ulong NrEvaluations { get; set; } public ulong TransferSubmitted { get; set; } public int ConfigGeneration { get; set; } }

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
                foreach (var exe in Directory.EnumerateFiles(folder, "*.exe", SearchOption.AllDirectories).Where(x => x.Contains("\\bin\\", StringComparison.OrdinalIgnoreCase) || x.Contains("\\x64\\", StringComparison.OrdinalIgnoreCase) || Path.GetDirectoryName(x)!.Equals(folder, StringComparison.OrdinalIgnoreCase)).OrderByDescending(x => new FileInfo(x).Length).Take(8))
                { var g = GameInspector.Inspect(exe, id, name); yield return g; }
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
        return new GameCandidate { Name = name ?? Path.GetFileNameWithoutExtension(exe), Executable = exe, SteamAppId = appId, Api = api, NativeDlss = hasDlss ? "已发现 nvngx_dlss.dll" : "未发现", EvidenceConfirmed = api == "D3D12" && hasDlss, Status = hd2 ? "Helldivers 2：禁止自动部署" : api == "D3D12" && hasDlss ? "待兼容表确认" : "不支持/待确认" };
    }
    static bool HasImport(string path, string dll) { try { return File.ReadAllBytes(path).AsSpan().IndexOf(System.Text.Encoding.ASCII.GetBytes(dll)) >= 0; } catch { return false; } }
}

public static class Deployer
{
    static string StateDir(GameCandidate g) => Path.Combine(g.Directory, ".dlss5-install-assistant");
    public static (bool Ok, string Message) Install(GameCandidate g, PackageManifest p)
    {
        if (g.Name.Contains("helldivers", StringComparison.OrdinalIgnoreCase)) return (false, "已拒绝：Helldivers 2 属于在线/反作弊游戏，本助手不自动部署。");
        if (!p.NativeD3D12 || !g.EvidenceConfirmed || !p.CompatibleProfiles.Contains("native-d3d12", StringComparer.OrdinalIgnoreCase)) return (false, "已拒绝：游戏或安装包未通过已验证兼容表。");
        var stateDir = StateDir(g); var existing = Path.Combine(stateDir, "installed.json"); if (File.Exists(existing)) return (false, "已拒绝：已有安装记录；请先卸载或恢复，避免覆盖首份备份。");
        foreach (var f in p.Files) { var same = Path.Combine(g.Directory, f.Path); if (Path.GetFileName(same).Equals("dxgi.dll", StringComparison.OrdinalIgnoreCase) && File.Exists(same) && !LooksLikeReShade(same)) return (false, "已拒绝：已有未知 dxgi.dll，未覆盖。"); if (File.Exists(same) && Path.GetExtension(same).Equals(".dll", StringComparison.OrdinalIgnoreCase) && !LooksLikeReShade(same)) return (false, "已拒绝：已有未知同名 DLL，未覆盖：" + f.Path); }
        var backup = Path.Combine(StateDir(g), "backups", DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff")); Directory.CreateDirectory(backup); var records = new List<BackupRecord>();
        var journal = Path.Combine(backup, "journal.json");
        try
        {
            foreach (var f in p.Files)
            {
                var dest = Destination(g, f); var had = File.Exists(dest); var rel = Path.GetRelativePath(g.Directory, dest);
                var record = new BackupRecord { Path = rel, HadOriginal = had }; records.Add(record); SaveJournal(journal, records);
                if (had) { var bp = Path.Combine(backup, rel); Directory.CreateDirectory(Path.GetDirectoryName(bp)!); File.Copy(dest, bp, true); record.Before = PackageManifest.Hash(bp); SaveJournal(journal, records); }
                var tmp = dest + ".dlss5-new"; File.Copy(Path.Combine(p.Root, f.Path), tmp, true); if (!PackageManifest.Hash(tmp).Equals(f.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("部署文件哈希不匹配：" + f.Path); File.Move(tmp, dest, true); record.After = f.Sha256; SaveJournal(journal, records);
            }
            File.WriteAllText(Path.Combine(backup, "transaction.json"), JsonSerializer.Serialize(records, new JsonSerializerOptions { WriteIndented = true })); File.WriteAllText(Path.Combine(StateDir(g), "installed.json"), JsonSerializer.Serialize(new { p.Id, p.Build, Files = p.Files }, new JsonSerializerOptions { WriteIndented = true })); g.Status = "已部署（运行时尚未确认）"; return (true, "安装完成：已备份并部署；启动后查看 runtime-status.json 才算运行时生效。");
        }
        catch { Rollback(g, backup, records); throw; }
    }
    public static string Recover(GameCandidate g) { var root = Path.Combine(StateDir(g), "backups"); if (!Directory.Exists(root)) return "没有待恢复事务。"; var b = new DirectoryInfo(root).GetDirectories().OrderByDescending(x => x.Name).FirstOrDefault(x => File.Exists(Path.Combine(x.FullName, "journal.json"))); if (b == null) return "没有待恢复事务。"; var rs = JsonSerializer.Deserialize<List<BackupRecord>>(File.ReadAllText(Path.Combine(b.FullName, "journal.json"))) ?? new(); Rollback(g, b.FullName, rs); return "已恢复中断事务，后来修改的文件已保留。"; }
    public static string Uninstall(GameCandidate g)
    {
        var state = Path.Combine(StateDir(g), "installed.json"); if (!File.Exists(state)) return "没有助手部署记录。"; using var doc = JsonDocument.Parse(File.ReadAllText(state));
        foreach (var f in doc.RootElement.GetProperty("Files").EnumerateArray()) { var rel = f.GetProperty("Path").GetString()!; var dest = SafeDestination(g, rel); var expected = f.GetProperty("Sha256").GetString()!; if (!File.Exists(dest) || !PackageManifest.Hash(dest).Equals(expected, StringComparison.OrdinalIgnoreCase)) continue; var backup = Directory.Exists(Path.Combine(StateDir(g), "backups")) ? new DirectoryInfo(Path.Combine(StateDir(g), "backups")).GetDirectories().OrderByDescending(x => x.Name).FirstOrDefault(x => File.Exists(Path.Combine(x.FullName, "transaction.json"))) : null; var original = backup == null ? null : Path.Combine(backup.FullName, rel); if (original != null && File.Exists(original)) File.Copy(original, dest, true); else File.Delete(dest); }
        g.Status = "已卸载（用户后来改动的文件已保留）"; return "卸载完成；检测到后来被修改的文件已保留。";
    }
    public static string RestoreLatest(GameCandidate g) { var dir = Path.Combine(StateDir(g), "backups"); var b = Directory.Exists(dir) ? new DirectoryInfo(dir).GetDirectories().OrderByDescending(x => x.Name).FirstOrDefault() : null; if (b == null) return "没有可恢复备份。"; var tx = Path.Combine(b.FullName, "transaction.json"); if (!File.Exists(tx)) return "备份缺少事务记录，未操作。"; foreach (var r in JsonSerializer.Deserialize<List<BackupRecord>>(File.ReadAllText(tx)) ?? new()) if (r.HadOriginal && File.Exists(Path.Combine(b.FullName, r.Path))) File.Copy(Path.Combine(b.FullName, r.Path), SafeDestination(g, r.Path), true); return "已恢复最近备份。"; }
    static void SaveJournal(string path, List<BackupRecord> records) { var tmp = path + ".tmp"; File.WriteAllText(tmp, JsonSerializer.Serialize(records, new JsonSerializerOptions { WriteIndented = true })); File.Move(tmp, path, true); }
    static void Rollback(GameCandidate g, string backup, List<BackupRecord> records) { foreach (var r in records.AsEnumerable().Reverse()) { var dest = SafeDestination(g, r.Path); if (r.HadOriginal && File.Exists(Path.Combine(backup, r.Path))) File.Copy(Path.Combine(backup, r.Path), dest, true); else if (!r.HadOriginal && r.After != null && File.Exists(dest) && PackageManifest.Hash(dest).Equals(r.After, StringComparison.OrdinalIgnoreCase)) File.Delete(dest); } }
    static string Destination(GameCandidate g, PackageFile f) { if (f.Target.Equals("game-root", StringComparison.OrdinalIgnoreCase)) return SafeDestination(g, f.Path); if (!f.Target.Equals("reshade-addon", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("未知目标映射：" + f.Target); var ini = Path.Combine(g.Directory, "ReShade.ini"); if (!File.Exists(ini)) throw new InvalidDataException("未找到 ReShade.ini，无法确认 AddOnPath。"); var line = File.ReadLines(ini).FirstOrDefault(x => x.TrimStart().StartsWith("AddOnPath", StringComparison.OrdinalIgnoreCase)); if (line == null) throw new InvalidDataException("ReShade.ini 没有 AddOnPath，拒绝猜测安装目录。"); var value = line[(line.IndexOf('=') + 1)..].Trim().Trim('"'); var dir = Path.GetFullPath(Path.Combine(g.Directory, Environment.ExpandEnvironmentVariables(value))); if (!Directory.Exists(dir)) throw new InvalidDataException("AddOnPath 目录不存在：" + dir); return SafeDestination(new GameCandidate { Executable = Path.Combine(dir, "placeholder.exe") }, Path.GetFileName(f.Path)); }
    static string SafeDestination(GameCandidate g, string rel) { var full = Path.GetFullPath(Path.Combine(g.Directory, rel)); if (!full.StartsWith(Path.GetFullPath(g.Directory) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("目标路径越界：" + rel); Directory.CreateDirectory(Path.GetDirectoryName(full)!); return full; }
    static bool LooksLikeReShade(string path) { try { return System.Text.Encoding.ASCII.GetString(File.ReadAllBytes(path)).Contains("ReShade", StringComparison.OrdinalIgnoreCase); } catch { return false; } }
    sealed class BackupRecord { public string Path { get; set; } = ""; public string? Before { get; set; } public string? After { get; set; } public bool HadOriginal { get; set; } }
}

public static class ElevationHelper
{
    public static bool TryRun(string executable, string arguments, out string message) { try { Process.Start(new ProcessStartInfo(executable, arguments) { UseShellExecute = true, Verb = "runas" }); message = "已请求最小权限 helper；游戏启动仍保持非管理员。"; return true; } catch (System.ComponentModel.Win32Exception) { message = "用户取消或系统拒绝提权，未执行写入。"; return false; } catch (Exception ex) { message = "提权 helper 失败：" + ex.Message; return false; } }
}

public static class Launcher
{
    public static void Start(GameCandidate g) { if (!string.IsNullOrWhiteSpace(g.SteamAppId)) { Process.Start(new ProcessStartInfo("steam://rungameid/" + g.SteamAppId) { UseShellExecute = true }); return; } Process.Start(new ProcessStartInfo(g.Executable) { WorkingDirectory = g.Directory, UseShellExecute = true }); }
    public static RuntimeStatus? ReadStatus(GameCandidate g)
    {
        var root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DLSS5InstallAssistant", "runtime"); if (!Directory.Exists(root)) return null;
        foreach (var path in Directory.EnumerateFiles(root, "*.json").OrderByDescending(File.GetLastWriteTimeUtc)) try
        {
            var s = JsonSerializer.Deserialize<RuntimeStatus>(File.ReadAllText(path), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }); if (s == null || !s.Schema.Equals("dlss5-runtime-status/v1", StringComparison.OrdinalIgnoreCase) || !s.Active || s.ProcessId is null || !PathEquals(s.Executable, g.Executable) || s.UpdatedAt is null || DateTimeOffset.UtcNow - s.UpdatedAt > TimeSpan.FromSeconds(15)) continue;
            using var p = Process.GetProcessById(s.ProcessId.Value); if (!PathEquals(p.MainModule?.FileName, g.Executable)) continue; var started = new DateTimeOffset(p.StartTime.ToUniversalTime()); var expected = s.ProcessStartedAt ?? s.SessionStartedAt; if (expected is null || Math.Abs((started - expected.Value).TotalSeconds) > 2) continue; return s;
        } catch { }
        return null;
    }
    static bool PathEquals(string? a, string b) => !string.IsNullOrWhiteSpace(a) && Path.GetFullPath(a).Equals(Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);
}

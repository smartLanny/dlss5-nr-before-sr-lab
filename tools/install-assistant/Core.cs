using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace Dlss5InstallAssistant;

public sealed class GameCandidate
{
    public string Name { get; set; } = "";
    public string Executable { get; set; } = "";
    public string Directory => Path.GetDirectoryName(Path.GetFullPath(Executable))!;
    public string Api { get; set; } = "Unknown";
    public string NativeDlss { get; set; } = "未确认";
    public string Status { get; set; } = "未部署";
    public string? SteamAppId { get; set; }
    public bool EvidenceConfirmed { get; set; }
    public string ExecutableSha256 { get; set; } = "";
}

public sealed class NativeD3D12Evidence
{
    public bool Verified { get; set; }
    public string Method { get; set; } = "";
    public string Api { get; set; } = "D3D12";
    public string ExecutableSha256 { get; set; } = "";
}

public sealed class CompatibilityProfile
{
    public List<string> SteamAppIds { get; set; } = new();
    public List<string> ExecutableSha256 { get; set; } = new();
    public bool VerifiedNativeD3D12 { get; set; }
    public bool Matches(GameCandidate game, bool packageEvidence) => VerifiedNativeD3D12 && packageEvidence && game.EvidenceConfirmed &&
        ((!string.IsNullOrWhiteSpace(game.SteamAppId) && SteamAppIds.Contains(game.SteamAppId, StringComparer.OrdinalIgnoreCase)) ||
         (!string.IsNullOrWhiteSpace(game.ExecutableSha256) && ExecutableSha256.Contains(game.ExecutableSha256, StringComparer.OrdinalIgnoreCase)));
}

public sealed class PackageManifest
{
    public string Id { get; set; } = "";
    public string Build { get; set; } = "";
    public bool NativeD3D12 { get; set; } = true;
    public bool OnlineAllowed { get; set; }
    public List<PackageFile> Files { get; set; } = new();
    public List<CompatibilityProfile> CompatibleProfiles { get; set; } = new();
    public NativeD3D12Evidence VerifiedNativeD3D12 { get; set; } = new();
    [JsonIgnore] public string Root { get; private set; } = "";
    public static PackageManifest Load(string root)
    {
        var fullRoot = Path.GetFullPath(root); var manifestPath = Path.Combine(fullRoot, "manifest.json");
        var p = JsonSerializer.Deserialize<PackageManifest>(File.ReadAllText(manifestPath), JsonOptions()) ?? throw new InvalidDataException("manifest.json 无法读取"); p.Root = fullRoot;
        if (string.IsNullOrWhiteSpace(p.Id) || string.IsNullOrWhiteSpace(p.Build) || p.Files.Count == 0 || p.CompatibleProfiles.Count == 0) throw new InvalidDataException("缺少 id/build/compatibleProfiles/files");
        if (!p.NativeD3D12 || !p.VerifiedNativeD3D12.Verified || !p.VerifiedNativeD3D12.Api.Equals("D3D12", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("缺少 verifiedNativeD3D12 证据结构");
        foreach (var profile in p.CompatibleProfiles) if (!profile.VerifiedNativeD3D12 || (profile.SteamAppIds.Count == 0 && profile.ExecutableSha256.Count == 0)) throw new InvalidDataException("compatibleProfiles 必须绑定 SteamAppId 或 EXE SHA256，并声明 verifiedNativeD3D12");
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var f in p.Files)
        {
            var rel = NormalizeRelative(f.Path, "manifest 路径非法："); if (!seen.Add(rel)) throw new InvalidDataException("manifest 含重复路径：" + rel); var q = SafePath(fullRoot, rel);
            if (!File.Exists(q)) throw new FileNotFoundException("包内文件缺失", f.Path); if (!Hash(q).Equals(f.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("SHA256 不匹配：" + f.Path); f.Path = rel;
        }
        return p;
    }
    internal static JsonSerializerOptions JsonOptions() => new() { PropertyNameCaseInsensitive = true, WriteIndented = true };
    internal static string NormalizeRelative(string path, string message)
    {
        if (string.IsNullOrWhiteSpace(path)) throw new InvalidDataException(message + path); var rel = path.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(rel) || rel.Split(Path.DirectorySeparatorChar).Any(x => x == ".." || x.Length == 0)) throw new InvalidDataException(message + path); return rel;
    }
    internal static string SafePath(string root, string rel)
    {
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar; var full = Path.GetFullPath(Path.Combine(fullRoot, rel));
        if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("目标路径越界：" + rel); return full;
    }
    internal static string Hash(string path) { using var h = SHA256.Create(); using var s = File.OpenRead(path); return Convert.ToHexString(h.ComputeHash(s)); }
}

public sealed class PackageFile
{
    public string Path { get; set; } = "";
    public string Sha256 { get; set; } = "";
    public string Role { get; set; } = "addon";
    public string Source { get; set; } = "trusted-local";
    public string Target { get; set; } = "game-root";
    public bool Required { get; set; } = true;
    public List<string> AllowedExistingSha256 { get; set; } = new();
}

public sealed class RuntimeStatus
{
    public string Schema { get; set; } = "dlss5-runtime-status/v1"; public string State { get; set; } = "unknown"; public string BuildId { get; set; } = ""; public bool Active { get; set; } public string BypassReason { get; set; } = "";
    public DateTimeOffset? UpdatedAt { get; set; } public int? SessionPid { get; set; } public DateTimeOffset? SessionStartedAt { get; set; } public int? ProcessId { get; set; } public DateTimeOffset? ProcessStartedAt { get; set; } public string Executable { get; set; } = ""; public bool NativeSrObserved { get; set; } public ulong NrEvaluations { get; set; } public ulong TransferSubmitted { get; set; } public int ConfigGeneration { get; set; }
}

public static class SteamScanner
{
    public static IEnumerable<GameCandidate> Scan()
    {
        var steam = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"); var roots = new List<string>(); var vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        if (File.Exists(vdf)) foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"", RegexOptions.IgnoreCase)) roots.Add(m.Groups[1].Value.Replace("\\\\", "\\")); if (!roots.Contains(steam, StringComparer.OrdinalIgnoreCase)) roots.Add(steam);
        foreach (var root in roots.Distinct(StringComparer.OrdinalIgnoreCase)) { var apps = Path.Combine(root, "steamapps"); if (!Directory.Exists(apps)) continue; foreach (var af in Directory.EnumerateFiles(apps, "appmanifest_*.acf")) { var text = File.ReadAllText(af); var name = Regex.Match(text, "\\\"name\\\"\\s+\\\"([^\\\"]+)\\\"").Groups[1].Value; var id = Regex.Match(Path.GetFileName(af), "(\\d+)").Groups[1].Value; var dir = Regex.Match(text, "\\\"installdir\\\"\\s+\\\"([^\\\"]+)\\\"").Groups[1].Value; var folder = Path.Combine(apps, "common", dir); if (!Directory.Exists(folder)) continue; foreach (var exe in Directory.EnumerateFiles(folder, "*.exe", SearchOption.AllDirectories).Where(x => x.Contains("\\bin\\", StringComparison.OrdinalIgnoreCase) || x.Contains("\\x64\\", StringComparison.OrdinalIgnoreCase) || Path.GetDirectoryName(x)!.Equals(folder, StringComparison.OrdinalIgnoreCase)).OrderByDescending(x => new FileInfo(x).Length).Take(8)) yield return GameInspector.Inspect(exe, id, name); } }
    }
}

public static class GameInspector
{
    public static GameCandidate Inspect(string exe, string? appId, string? name = null)
    {
        var fullExe = Path.GetFullPath(exe); var dir = Path.GetDirectoryName(fullExe)!; var dlls = Directory.Exists(dir) ? Directory.EnumerateFiles(dir, "*.dll").Select(x => Path.GetFileName(x) ?? "").ToArray() : Array.Empty<string>(); var hasDlss = dlls.Any(x => x.Equals("nvngx_dlss.dll", StringComparison.OrdinalIgnoreCase)); var api = HasImport(fullExe, "d3d12.dll") ? "D3D12" : HasImport(fullExe, "d3d11.dll") ? "D3D11" : "未知"; var hd2 = string.Equals(appId, "553850", StringComparison.OrdinalIgnoreCase) || (name ?? fullExe).Contains("helldivers 2", StringComparison.OrdinalIgnoreCase); var sha = File.Exists(fullExe) ? PackageManifest.Hash(fullExe) : "";
        return new GameCandidate { Name = name ?? Path.GetFileNameWithoutExtension(fullExe), Executable = fullExe, SteamAppId = appId, Api = api, NativeDlss = hasDlss ? "已发现 nvngx_dlss.dll" : "未发现", ExecutableSha256 = sha, EvidenceConfirmed = api == "D3D12" && hasDlss, Status = hd2 ? "Helldivers 2：禁止自动部署" : api == "D3D12" && hasDlss ? "待安装包具体兼容表确认" : "不支持/待确认" };
    }
    static bool HasImport(string path, string dll) { try { return File.ReadAllBytes(path).AsSpan().IndexOf(Encoding.ASCII.GetBytes(dll)) >= 0; } catch { return false; } }
}

public sealed record DeploymentPlan(GameCandidate Game, PackageManifest Package, IReadOnlyList<PlannedFile> Files);
public sealed record PlannedFile(PackageFile Source, string Destination, string TargetRoot);

public static class Deployer
{
    static string StateDir(GameCandidate g) => Path.Combine(g.Directory, ".dlss5-install-assistant");
    public static DeploymentPlan Inspect(GameCandidate g, PackageManifest p) => Plan(g, p);
    public static DeploymentPlan Plan(GameCandidate g, PackageManifest p)
    {
        if (string.Equals(g.SteamAppId, "553850", StringComparison.OrdinalIgnoreCase) || g.Name.Contains("helldivers 2", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Helldivers 2（553850）永久禁止自动部署。");
        if (!p.NativeD3D12 || !g.EvidenceConfirmed || !p.CompatibleProfiles.Any(x => x.Matches(g, p.VerifiedNativeD3D12.Verified))) throw new InvalidDataException("游戏或安装包未通过具体 SteamAppId/EXE SHA256 兼容表。");
        var files = p.Files.Select(f => { var (dest, root) = Destination(g, f); return new PlannedFile(f, dest, root); }).ToList(); var destinations = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in files) { if (!destinations.Add(item.Destination)) throw new InvalidDataException("安装包映射到重复目标：" + item.Destination); if (File.Exists(item.Destination)) { var current = PackageManifest.Hash(item.Destination); if (!item.Source.AllowedExistingSha256.Contains(current, StringComparer.OrdinalIgnoreCase)) throw new InvalidDataException("目标已有未知同名文件，拒绝覆盖：" + item.Destination); } }
        return new DeploymentPlan(g, p, files);
    }
    public static (bool Ok, string Message) Install(GameCandidate g, PackageManifest p) => Install(g, p, CancellationToken.None);
    public static (bool Ok, string Message) Install(GameCandidate g, PackageManifest p, CancellationToken cancellationToken) => Apply(Plan(g, p), cancellationToken);
    public static (bool Ok, string Message) Apply(DeploymentPlan plan, CancellationToken cancellationToken = default)
    {
        var g = plan.Game; var stateDir = StateDir(g); var existing = Path.Combine(stateDir, "installed.json"); if (File.Exists(existing)) return (false, "已拒绝：已有安装记录；请先卸载或恢复。"); var backup = Path.Combine(stateDir, "backups", DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff") + "-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(backup); var records = new List<BackupRecord>(); var journal = Path.Combine(backup, "journal.json"); SaveJournal(journal, records);
        try
        {
            var index = 0; foreach (var item in plan.Files) { cancellationToken.ThrowIfCancellationRequested(); MaybeFail("before-record"); var dest = item.Destination; var had = File.Exists(dest); var backupName = Path.Combine("files", (++index).ToString("D6") + ".bin"); var record = new BackupRecord { Destination = dest, TargetRoot = item.TargetRoot, BackupFile = backupName, HadOriginal = had, After = item.Source.Sha256, SourcePath = item.Source.Path }; records.Add(record); if (had) { var bp = Path.Combine(backup, backupName); Directory.CreateDirectory(Path.GetDirectoryName(bp)!); File.Copy(dest, bp, true); record.Before = PackageManifest.Hash(bp); } SaveJournal(journal, records); MaybeFail("after-backup"); cancellationToken.ThrowIfCancellationRequested(); var tmp = dest + ".dlss5-new-" + Guid.NewGuid().ToString("N"); try { File.Copy(Path.Combine(plan.Package.Root, item.Source.Path), tmp, true); if (!PackageManifest.Hash(tmp).Equals(item.Source.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("部署文件哈希不匹配：" + item.Source.Path); MaybeFail("before-move"); cancellationToken.ThrowIfCancellationRequested(); File.Move(tmp, dest, true); } finally { if (File.Exists(tmp)) File.Delete(tmp); } MaybeFail("after-move"); }
            SaveJournal(Path.Combine(backup, "transaction.json"), records); AtomicWrite(Path.Combine(stateDir, "installed.json"), JsonSerializer.Serialize(new InstalledState { Id = plan.Package.Id, Build = plan.Package.Build, Files = records }, PackageManifest.JsonOptions())); g.Status = "已部署（运行时尚未确认）"; return (true, "安装完成：已备份并部署；启动后查看 runtime-status 才算运行时生效。");
        }
        catch { Rollback(backup, records); throw; }
    }
    public static string Recover(GameCandidate g) { var b = LatestBackup(Path.Combine(StateDir(g), "backups"), true); if (b == null) return "没有待恢复事务。"; Rollback(b.FullName, ReadRecords(Path.Combine(b.FullName, "journal.json"))); return "已恢复中断事务；后来修改的文件已保留。"; }
    public static string Uninstall(GameCandidate g)
    {
        var statePath = Path.Combine(StateDir(g), "installed.json"); if (!File.Exists(statePath)) return "没有助手部署记录。"; var state = JsonSerializer.Deserialize<InstalledState>(File.ReadAllText(statePath), PackageManifest.JsonOptions()) ?? throw new InvalidDataException("installed.json 无法读取"); var backup = LatestBackup(Path.Combine(StateDir(g), "backups"), false); foreach (var r in state.Files.AsEnumerable().Reverse()) RestoreOne(backup?.FullName, r, false); File.Delete(statePath); g.Status = "已卸载（用户后来改动的文件已保留）"; return "卸载完成；后来修改的文件已保留，安装记录已清理。";
    }
    public static string RestoreLatest(GameCandidate g) { var b = LatestBackup(Path.Combine(StateDir(g), "backups"), false); if (b == null) return "没有可恢复备份。"; var tx = Path.Combine(b.FullName, "transaction.json"); if (!File.Exists(tx)) return "备份缺少事务记录，未操作。"; foreach (var r in ReadRecords(tx).AsEnumerable().Reverse()) RestoreOne(b.FullName, r, false); return "已尝试恢复最近备份；后来修改的文件已保留。"; }
    static DirectoryInfo? LatestBackup(string root, bool journal) => Directory.Exists(root) ? new DirectoryInfo(root).GetDirectories().OrderByDescending(x => x.Name).FirstOrDefault(x => File.Exists(Path.Combine(x.FullName, journal ? "journal.json" : "transaction.json"))) : null;
    static List<BackupRecord> ReadRecords(string path) => JsonSerializer.Deserialize<List<BackupRecord>>(File.ReadAllText(path), PackageManifest.JsonOptions()) ?? new();
    static void RestoreOne(string? backup, BackupRecord r, bool restoreMissingOriginal)
    {
        var backupFile = backup == null ? null : BackupPath(backup, r.BackupFile);
        if (!File.Exists(r.Destination)) { if (restoreMissingOriginal && r.HadOriginal && backupFile != null && File.Exists(backupFile)) { Directory.CreateDirectory(Path.GetDirectoryName(r.Destination)!); File.Copy(backupFile, r.Destination, true); } return; }
        var current = PackageManifest.Hash(r.Destination); if (!current.Equals(r.After, StringComparison.OrdinalIgnoreCase)) return; if (r.HadOriginal && backupFile != null && File.Exists(backupFile)) File.Copy(backupFile, r.Destination, true); else File.Delete(r.Destination);
    }
    static string BackupPath(string backup, string relative) => PackageManifest.SafePath(backup, PackageManifest.NormalizeRelative(relative, "备份路径非法："));
    static void Rollback(string backup, List<BackupRecord> records) { foreach (var r in records.AsEnumerable().Reverse()) RestoreOne(backup, r, true); }
    static void SaveJournal(string path, List<BackupRecord> records) => AtomicWrite(path, JsonSerializer.Serialize(records, PackageManifest.JsonOptions()));
    static void AtomicWrite(string path, string text)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!); var tmp = path + ".tmp-" + Guid.NewGuid().ToString("N"); using (var fs = new FileStream(tmp, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough)) using (var sw = new StreamWriter(fs, new UTF8Encoding(false))) { sw.Write(text); sw.Flush(); fs.Flush(true); } if (File.Exists(path)) File.Replace(tmp, path, null); else File.Move(tmp, path);
    }
    static (string Destination, string Root) Destination(GameCandidate g, PackageFile f)
    {
        if (f.Target.Equals("game-root", StringComparison.OrdinalIgnoreCase)) { var root = Path.GetFullPath(g.Directory); return (SafeDestination(root, f.Path), root); } if (!f.Target.Equals("reshade-addon", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("未知目标映射：" + f.Target);
        var ini = Path.Combine(g.Directory, "ReShade.ini"); if (!File.Exists(ini)) throw new InvalidDataException("未找到 ReShade.ini，无法确认 AddOnPath。"); var line = File.ReadLines(ini).FirstOrDefault(x => x.TrimStart().StartsWith("AddOnPath", StringComparison.OrdinalIgnoreCase)); if (line == null || !line.Contains('=')) throw new InvalidDataException("ReShade.ini 没有 AddOnPath，拒绝猜测安装目录。"); var value = line[(line.IndexOf('=') + 1)..].Trim().Trim('"'); var dir = Path.GetFullPath(Path.IsPathRooted(value) ? Environment.ExpandEnvironmentVariables(value) : Path.Combine(g.Directory, Environment.ExpandEnvironmentVariables(value))); if (!Directory.Exists(dir)) throw new InvalidDataException("AddOnPath 目录不存在：" + dir); RejectReparse(dir, dir); return (SafeDestination(dir, Path.GetFileName(f.Path)), dir);
    }
    static string SafeDestination(string root, string rel) { var normalized = PackageManifest.NormalizeRelative(rel, "目标路径非法："); var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar); RejectReparse(fullRoot, fullRoot); var full = PackageManifest.SafePath(fullRoot, normalized); var parent = Path.GetDirectoryName(full)!; RejectReparse(fullRoot, parent); Directory.CreateDirectory(parent); return full; }
    static void RejectReparse(string root, string path) { var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar); var current = Path.GetFullPath(path); while (current.Length >= fullRoot.Length) { if (Directory.Exists(current) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("目标目录含 reparse point，拒绝写入：" + current); if (current.Equals(fullRoot, StringComparison.OrdinalIgnoreCase)) break; current = Path.GetDirectoryName(current) ?? fullRoot; } }
    static void MaybeFail(string stage) { if (string.Equals(Environment.GetEnvironmentVariable("DLSS5_TEST_FAIL_STAGE"), stage, StringComparison.OrdinalIgnoreCase)) throw new IOException("测试注入失败：" + stage); }
    sealed class InstalledState { public string Id { get; set; } = ""; public string Build { get; set; } = ""; public List<BackupRecord> Files { get; set; } = new(); }
    public sealed class BackupRecord { public string Destination { get; set; } = ""; public string TargetRoot { get; set; } = ""; public string BackupFile { get; set; } = ""; public string SourcePath { get; set; } = ""; public string? Before { get; set; } public string? After { get; set; } public bool HadOriginal { get; set; } }
}

public static class ElevationHelper
{
    public static string HelperPath => Path.Combine(AppContext.BaseDirectory, "DLSS5InstallAssistant.Helper.exe");
    public static bool TryRun(string executable, string arguments, out string message)
    {
        try { var full = Path.GetFullPath(executable); var baseDir = Path.GetFullPath(AppContext.BaseDirectory); if (!File.Exists(full) || !full.StartsWith(baseDir, StringComparison.OrdinalIgnoreCase) || !Path.GetFileName(full).Equals("DLSS5InstallAssistant.Helper.exe", StringComparison.OrdinalIgnoreCase)) { message = "提权 helper 路径未通过白名单校验，未执行。"; return false; } Process.Start(new ProcessStartInfo(full, arguments) { UseShellExecute = true, Verb = "runas", WorkingDirectory = baseDir }); message = "已请求最小权限 helper；游戏启动仍保持非管理员。"; return true; }
        catch (System.ComponentModel.Win32Exception) { message = "用户取消或系统拒绝提权，未执行写入。"; return false; } catch (Exception ex) { message = "提权 helper 失败：" + ex.Message; return false; }
    }
    public static bool TryRunFor(string operation, GameCandidate game, string? packageRoot, out string message) { static string Q(string v) => "\"" + v.Replace("\"", "\\\"") + "\""; var args = operation + " " + Q(game.Executable) + " " + Q(game.SteamAppId ?? "") + (packageRoot == null ? "" : " " + Q(packageRoot)); return TryRun(HelperPath, args, out message); }
}

public static class Launcher
{
    public static void Start(GameCandidate g) { if (!string.IsNullOrWhiteSpace(g.SteamAppId)) { Process.Start(new ProcessStartInfo("steam://rungameid/" + g.SteamAppId) { UseShellExecute = true }); return; } Process.Start(new ProcessStartInfo(g.Executable) { WorkingDirectory = g.Directory, UseShellExecute = true }); }
    public static RuntimeStatus? ReadStatus(GameCandidate g, string? expectedBuild = null)
    {
        expectedBuild ??= ReadInstalledBuild(g); var root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DLSS5InstallAssistant", "runtime"); if (!Directory.Exists(root)) return null; foreach (var path in Directory.EnumerateFiles(root, "*.json").OrderByDescending(File.GetLastWriteTimeUtc)) try { var s = JsonSerializer.Deserialize<RuntimeStatus>(File.ReadAllText(path), PackageManifest.JsonOptions()); var now = DateTimeOffset.UtcNow; if (s == null || !s.Schema.Equals("dlss5-runtime-status/v1", StringComparison.OrdinalIgnoreCase) || !s.Active || !s.State.Equals("active", StringComparison.OrdinalIgnoreCase) || !s.NativeSrObserved || s.NrEvaluations == 0 || s.TransferSubmitted == 0 || s.ConfigGeneration <= 0 || s.ProcessId is null || !PathEquals(s.Executable, g.Executable) || s.UpdatedAt is null || s.UpdatedAt > now || now - s.UpdatedAt > TimeSpan.FromSeconds(15) || (expectedBuild != null && !s.BuildId.Equals(expectedBuild, StringComparison.OrdinalIgnoreCase))) continue; using var p = Process.GetProcessById(s.ProcessId.Value); if (!PathEquals(p.MainModule?.FileName, g.Executable)) continue; var started = new DateTimeOffset(p.StartTime.ToUniversalTime()); var expected = s.ProcessStartedAt ?? s.SessionStartedAt; if (expected is null || expected > now || Math.Abs((started - expected.Value).TotalSeconds) > 1) continue; return s; } catch { } return null;
    }
    static string? ReadInstalledBuild(GameCandidate g) { try { var p = Path.Combine(g.Directory, ".dlss5-install-assistant", "installed.json"); using var d = JsonDocument.Parse(File.ReadAllText(p)); return d.RootElement.GetProperty("Build").GetString(); } catch { return null; } }
    static bool PathEquals(string? a, string b) => !string.IsNullOrWhiteSpace(a) && Path.GetFullPath(a).Equals(Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);
}

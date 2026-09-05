using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Dlss5InstallAssistant;

public enum FileAction { Write, Reuse, Remove }
public sealed record PlannedFile(string Destination, string? SourcePath, string? BeforeSha256, string? AfterSha256, FileAction Action, string Role, byte[]? Content = null, string Detail = "");
public sealed record DeploymentPlan(GameCandidate Game, PackageManifest Package, VerifiedReShadeHost Host, string AddonDirectory,
    IReadOnlyList<PlannedFile> Files, string? InstalledStateSha256, string? ReShadeIniSha256);
public sealed record OperationResult(bool Ok, string Message);

public sealed class OwnedFile
{
    public string RelativePath { get; set; } = "";
    public string? Before { get; set; }
    public string? After { get; set; }
    public string BackupTransactionId { get; set; } = "";
    public string BackupFile { get; set; } = "";
    public bool Removal { get; set; }
    public bool Prepared { get; set; }
}
public sealed class InstalledState
{
    public string Schema { get; set; } = "dlss5-installed/v3";
    public string TransactionId { get; set; } = "";
    public string Executable { get; set; } = "";
    public string Build { get; set; } = "";
    public string PayloadSourceCommit { get; set; } = "";
    public string ArtifactId { get; set; } = "";
    public string AddonRelativePath { get; set; } = "";
    public string AddonSha256 { get; set; } = "";
    public string ManifestSha256 { get; set; } = "";
    public List<OwnedFile> Files { get; set; } = new();
}
internal sealed class InstallTransaction
{
    public string Schema { get; set; } = "dlss5-transaction/v3";
    public string Id { get; set; } = "";
    public string Executable { get; set; } = "";
    public string Status { get; set; } = "prepared";
    public string? PreviousInstalledJson { get; set; }
    public List<OwnedFile> Files { get; set; } = new();
}

public static class Deployer
{
    internal static Action<string>? TestCheckpoint { get; set; }
    internal static string StateDirectory(GameCandidate game) => Path.Combine(game.Directory, ".dlss5-install-assistant");
    static string InstalledPath(GameCandidate game) => Path.Combine(StateDirectory(game), "installed.json");
    static string ActivePath(GameCandidate game) => Path.Combine(StateDirectory(game), "active.json");
    static string TransactionDirectory(GameCandidate game, string id) { if (!Guid.TryParseExact(id, "N", out _)) throw new InvalidDataException("事务ID非法。"); return Path.Combine(StateDirectory(game), "transactions", id); }
    static string JournalPath(GameCandidate game, string id) => Path.Combine(TransactionDirectory(game, id), "journal.json");
    static void Checkpoint(string name) => TestCheckpoint?.Invoke(name);

    public static DeploymentPlan Inspect(GameCandidate game, PackageManifest package, VerifiedReShadeHost host) => Plan(game, package, host);
    public static DeploymentPlan Plan(GameCandidate game, PackageManifest package, VerifiedReShadeHost host)
    {
        game = GameInspector.Inspect(game.Executable, game.SteamAppId, game.Name);
        if (game.SteamAppId == "553850" || !game.EvidenceConfirmed || !package.CompatibleProfiles.Any(x => x.Matches(game)))
            throw new InvalidDataException("此游戏尚未通过首版原生 D3D12 配置，只能查看。");
        GpuCompatibility.EnsurePackageMatches(package.GpuFamily);
        GameInspector.EnsureNotRunning(game); SafeFiles.RejectReparse(StateDirectory(game));
        if (File.Exists(ActivePath(game))) throw new InvalidOperationException("存在未结束事务，请先恢复中断事务。");
        VerifyHost(host); var files = new List<PlannedFile>();
        var loader = Path.Combine(game.Directory, "dxgi.dll"); var loaderHash = SafeFiles.ExistingHash(loader);
        if (loaderHash != null && !Equal(loaderHash, ReShadeHost.PinnedHostSha256)) throw new InvalidDataException("已有未知或不同版本 dxgi.dll，保留该文件并停止安装。");
        foreach (var name in new[] { "d3d9.dll", "d3d10.dll", "d3d11.dll", "d3d12.dll", "opengl32.dll" })
            if (File.Exists(Path.Combine(game.Directory, name))) throw new InvalidDataException("检测到另一宿主入口，需先确认现有安装：" + name);
        files.Add(new(loader, host.HostPath, loaderHash, host.HostSha256, loaderHash == null ? FileAction.Write : FileAction.Reuse, "reshade-host"));
        var prior = ReadInstalled(game);
        var configureChineseFont = prior == null && package.Language == "zh-CN";
        var ini = Path.Combine(game.Directory, "ReShade.ini"); var iniHash = SafeFiles.ExistingHash(ini); string addonRoot;
        if (iniHash != null)
        {
            if (loaderHash == null) throw new InvalidDataException("存在 ReShade.ini 但没有可验证的宿主，先修复现有安装。");
            addonRoot = ExistingAddonDirectory(game, ini);
            byte[]? updated = null;
            if (configureChineseFont && new FileInfo(ini).Length <= 4 * 1024 * 1024)
            {
                var original = File.ReadAllBytes(ini);
                if (!ReShadeFontConfig.TryAddChineseOverlayFont(original, out updated, out _))
                    updated = null;
            }
            if (updated != null)
                files.Add(new(ini, null, iniHash, Convert.ToHexString(SHA256.HashData(updated)),
                    FileAction.Write, "reshade-config", updated));
            else
                files.Add(new(ini, null, iniHash, iniHash, FileAction.Reuse, "reshade-config"));
        }
        else
        {
            addonRoot = Path.Combine(game.Directory, ".dlss5-addons");
            var bytes = Encoding.UTF8.GetBytes("[ADDON]\r\nAddonPath=.\\.dlss5-addons\r\n\r\n[GENERAL]\r\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\r\nTextureSearchPaths=.\\reshade-shaders\\Textures\\**\r\n");
            if (configureChineseFont && ReShadeFontConfig.TryAddChineseOverlayFont(
                    bytes, out var updated, out _))
                bytes = updated;
            files.Add(new(ini, null, null, Convert.ToHexString(SHA256.HashData(bytes)), FileAction.Write, "reshade-config", bytes));
        }
        SafeFiles.RejectReparse(addonRoot);
        foreach (var f in package.Files)
        {
            var destination = PackageManifest.SafePath(addonRoot, f.Path); var before = SafeFiles.ExistingHash(destination);
            if (destination.Length >= 260) throw new InvalidDataException("插件路径超过当前插件支持的长度，请将游戏放在更短的本地路径后重试。");
            var owned = prior?.Files.FirstOrDefault(x => SafeFiles.PathEquals(ResolveOwned(game, x), destination));
            var same = Equal(before, f.Sha256);
            var managedDefault = f.Role == "config" && before != null &&
                (Equal(owned?.After, before) || f.AllowedExistingSha256.Contains(before, StringComparer.OrdinalIgnoreCase));
            var preserve = f.Role == "config" && before != null && !same && !managedDefault;
            if (before != null && !same && !preserve && !f.AllowedExistingSha256.Contains(before, StringComparer.OrdinalIgnoreCase) && !Equal(owned?.After, before))
                throw new InvalidDataException("目标已有未知同名文件，保留并停止：" + destination);
            var detail = f.Role != "config" ? "" : same ? "配置已经是所选版本，直接复用。" :
                managedDefault ? "现有配置是已知未修改的默认值，将随版本切换到新默认。" :
                preserve ? "检测到用户改过配置，保留强度、颜色跟随等设置。" : "首次安装所选版本的默认配置。";
            files.Add(new(destination, Path.Combine(package.Root, f.Path), before, preserve ? before : f.Sha256,
                same || preserve ? FileAction.Reuse : FileAction.Write, f.Role, Detail: detail));
        }
        var addon = package.Files.Single(x => x.Role == "addon"); var currentAddon = Path.Combine(addonRoot, addon.Path);
        var roots = new[] { addonRoot, game.Directory }.Distinct(StringComparer.OrdinalIgnoreCase);
        foreach (var root in roots.Where(Directory.Exists))
        foreach (var old in Directory.EnumerateFiles(root, "*.addon64"))
        {
            if (SafeFiles.PathEquals(old, currentAddon)) continue;
            var hash = SafeFiles.ExistingHash(old)!; var leaf = Path.GetFileName(old);
            var known = Equal(hash, addon.Sha256) || package.LegacyFiles.Any(x => x.Path.Equals(leaf, StringComparison.OrdinalIgnoreCase) && Equal(x.Sha256, hash));
            var previous = prior?.Files.Any(x => SafeFiles.PathEquals(ResolveOwned(game, x), old) && Equal(x.After, hash)) ?? false;
            if (known || previous) files.Add(new(old, null, hash, null, FileAction.Remove, "legacy-addon"));
            else if (leaf.Contains("DLSS5", StringComparison.OrdinalIgnoreCase) || leaf.StartsWith("renodx-dlss", StringComparison.OrdinalIgnoreCase) || package.LegacyFiles.Any(x => x.Path.Equals(leaf, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("检测到身份未知的另一 NR 插件，保留并停止：" + leaf);
        }
        if (files.Select(x => x.Destination).Distinct(StringComparer.OrdinalIgnoreCase).Count() != files.Count) throw new InvalidDataException("安装计划含重复目标。");
        return new(game, package, host, addonRoot, files, SafeFiles.ExistingHash(InstalledPath(game)), iniHash);
    }

    public static OperationResult Install(GameCandidate game, PackageManifest package, VerifiedReShadeHost host, CancellationToken token = default) => Apply(Plan(game, package, host), token);
    public static OperationResult Apply(DeploymentPlan plan, CancellationToken token = default)
    {
        var game = plan.Game; using var lease = new TargetLease(game); GameInspector.EnsureNotRunning(game);
        if (File.Exists(ActivePath(game))) throw new InvalidOperationException("存在未结束事务，请先恢复。");
        var package = PackageManifest.LoadWithTrust(plan.Package.Root, plan.Package.TrustKey);
        if (!Equal(package.ManifestSha256, plan.Package.ManifestSha256) || !Equal(PackageManifest.Hash(game.Executable), game.ExecutableSha256)) throw new InvalidDataException("安装包或游戏在检查后发生变化。");
        VerifyHost(plan.Host);
        if (!Equal(SafeFiles.ExistingHash(InstalledPath(game)), plan.InstalledStateSha256) || !Equal(SafeFiles.ExistingHash(Path.Combine(game.Directory, "ReShade.ini")), plan.ReShadeIniSha256)) throw new InvalidDataException("安装状态或 ReShade 配置已变化，请重新检查。");
        // Recreate the plan under the lock. Do not trust mutable UI objects.
        var fresh = Plan(game, package, plan.Host);
        if (fresh.Files.Count != plan.Files.Count || !fresh.Files.Zip(plan.Files).All(x => x.First == x.Second || SameOperation(x.First, x.Second))) throw new InvalidDataException("目标计划发生变化，请重新检查。");
        foreach (var f in fresh.Files) { EnsureInside(game, f.Destination); lease.Pin(Path.GetDirectoryName(f.Destination)!); if (!Equal(SafeFiles.ExistingHash(f.Destination), f.BeforeSha256)) throw new InvalidDataException("目标文件已变化。"); }
        token.ThrowIfCancellationRequested();
        var previous = File.Exists(InstalledPath(game)) ? File.ReadAllText(InstalledPath(game)) : null;
        var transaction = new InstallTransaction { Id = Guid.NewGuid().ToString("N"), Executable = game.Executable, PreviousInstalledJson = previous };
        var txRoot = TransactionDirectory(game, transaction.Id); Directory.CreateDirectory(txRoot); lease.Pin(txRoot); Save(game, transaction);
        SafeFiles.AtomicText(ActivePath(game), transaction.Id);
        try
        {
            var index = 0;
            foreach (var f in fresh.Files.Where(x => x.Action != FileAction.Reuse))
            {
                token.ThrowIfCancellationRequested(); GameInspector.EnsureNotRunning(game); Checkpoint("before-record");
                var parent = Path.GetDirectoryName(f.Destination)!; Directory.CreateDirectory(parent); lease.Pin(parent);
                if (!Equal(SafeFiles.ExistingHash(f.Destination), f.BeforeSha256)) throw new InvalidDataException("写入前目标已变化。");
                var r = new OwnedFile { RelativePath = Path.GetRelativePath(game.Directory, f.Destination), Before = f.BeforeSha256, After = f.AfterSha256,
                    BackupTransactionId = transaction.Id, BackupFile = (++index).ToString("D6") + ".bin", Removal = f.Action == FileAction.Remove };
                transaction.Files.Add(r); Save(game, transaction);
                if (r.Before != null)
                {
                    var backup = Path.Combine(txRoot, r.BackupFile); File.Copy(f.Destination, backup, false);
                    if (!Equal(PackageManifest.Hash(backup), r.Before)) throw new IOException("备份与检查时内容不同，未继续写入。");
                }
                r.Prepared = true; Save(game, transaction); Checkpoint("after-backup"); token.ThrowIfCancellationRequested();
                if (f.Action == FileAction.Remove)
                { if (!Equal(SafeFiles.ExistingHash(f.Destination), f.BeforeSha256)) throw new IOException("移除前文件已变化。"); File.Delete(f.Destination); }
                else WriteFile(f, lease, token);
                Checkpoint("after-move"); Save(game, transaction);
            }
            var state = previous == null ? new InstalledState() : ReadJson<InstalledState>(previous);
            var owned = state.Files.ToDictionary(x => x.RelativePath, StringComparer.OrdinalIgnoreCase);
            foreach (var r in transaction.Files)
            {
                if (owned.TryGetValue(r.RelativePath, out var baseline)) { baseline.After = r.After; baseline.Removal = r.Removal; }
                else owned[r.RelativePath] = Clone(r);
            }
            state.TransactionId = transaction.Id; state.Executable = game.Executable; state.Build = package.Build;
            state.PayloadSourceCommit = package.PayloadSourceCommit; state.ArtifactId = package.ArtifactId; state.ManifestSha256 = package.ManifestSha256;
            var addon = package.Files.Single(x => x.Role == "addon"); state.AddonRelativePath = Path.GetRelativePath(game.Directory, Path.Combine(fresh.AddonDirectory, addon.Path)); state.AddonSha256 = addon.Sha256;
            state.Files = owned.Values.ToList();
            Checkpoint("before-installed"); SafeFiles.AtomicText(InstalledPath(game), JsonSerializer.Serialize(state, PackageManifest.JsonOptions()));
            Checkpoint("after-installed"); transaction.Status = "complete"; Save(game, transaction); File.Delete(ActivePath(game));
            game.Status = "已部署，等待启动验证"; return new(true, "安装完成；已有自定义设置已保留，必要的 ReShade 设置已通过可恢复事务更新。启动后验证实际加载的插件。");
        }
        catch
        {
            Rollback(game, transaction, lease); throw;
        }
    }

    public static OperationResult Recover(GameCandidate game)
    {
        using var lease = new TargetLease(game); GameInspector.EnsureNotRunning(game);
        if (!File.Exists(ActivePath(game))) return new(true, "没有待恢复事务。");
        var id = File.ReadAllText(ActivePath(game)).Trim(); var transaction = ReadTransaction(game, id);
        if (transaction.Status is "complete" or "rolledback") { File.Delete(ActivePath(game)); return new(true, "事务已结束；未触碰后续安装。"); }
        var result = Rollback(game, transaction, lease);
        return new(result, result ? "中断事务已恢复；后续改动已保留。" : "备份缺失或损坏，已保留当前文件；恢复尚未完成。");
    }
    public static OperationResult Uninstall(GameCandidate game)
    {
        using var lease = new TargetLease(game); GameInspector.EnsureNotRunning(game);
        if (File.Exists(ActivePath(game))) throw new InvalidOperationException("请先恢复中断事务。");
        var state = ReadInstalled(game); if (state == null) return new(true, "没有助手部署记录。");
        var missing = false; var preserved = 0;
        foreach (var r in state.Files.AsEnumerable().Reverse()) { var result = RestoreOne(game, r, lease); if (result == RestoreOutcome.MissingBackup) missing = true; if (result == RestoreOutcome.Preserved) preserved++; }
        if (missing) return new(false, "部分备份缺失或损坏；当前文件和安装记录已保留，可补回备份后重试。");
        File.Delete(InstalledPath(game)); game.Status = "已卸载助手部署";
        return new(true, $"卸载完成；保留 {preserved} 个安装后改动的文件，复用的文件始终未移除。");
    }
    public static OperationResult RestoreLatest(GameCandidate game) => Uninstall(game);
    internal static InstalledState? ReadInstalled(GameCandidate game)
    {
        var path = InstalledPath(game); SafeFiles.RejectReparse(path); if (!File.Exists(path)) return null;
        var s = ReadBounded<InstalledState>(path);
        if (s.Schema != "dlss5-installed/v3" || !SafeFiles.PathEquals(s.Executable, game.Executable) || !Guid.TryParseExact(s.TransactionId, "N", out _)) throw new InvalidDataException("安装记录身份不匹配。");
        foreach (var r in s.Files) ValidateRecord(game, r); EnsureInside(game, PackageManifest.SafePath(game.Directory, s.AddonRelativePath));
        return s;
    }
    internal static IDisposable AcquireForLaunch(GameCandidate game)
    { var lease = new TargetLease(game); if (File.Exists(ActivePath(game))) { lease.Dispose(); throw new InvalidOperationException("安装事务未结束，暂不能启动。"); } return lease; }

    static void VerifyHost(VerifiedReShadeHost host)
    {
        SafeFiles.RejectReparse(host.HostPath);
        if (!Equal(host.HostSha256, ReShadeHost.PinnedHostSha256) || !Equal(host.SetupSha256, ReShadeHost.PinnedSetupSha256) || host.Version != ReShadeHost.Version || !Equal(PackageManifest.Hash(host.HostPath), ReShadeHost.PinnedHostSha256))
            throw new InvalidDataException("官方 ReShade 宿主缓存未通过固定哈希验证。");
    }
    static string ExistingAddonDirectory(GameCandidate game, string ini)
    {
        var section = ""; string? value = null;
        foreach (var raw in File.ReadLines(ini))
        {
            var line = raw.Trim(); if (line.StartsWith(';') || line.StartsWith('#') || line.Length == 0) continue;
            if (line.StartsWith('[') && line.EndsWith(']')) { section = line[1..^1].Trim(); continue; }
            var eq = line.IndexOf('='); if (eq < 0 || !section.Equals("ADDON", StringComparison.OrdinalIgnoreCase) || !line[..eq].Trim().Equals("AddonPath", StringComparison.OrdinalIgnoreCase)) continue;
            if (value != null) throw new InvalidDataException("ReShade.ini 包含重复 AddonPath。"); value = line[(eq + 1)..].Trim().Trim('"');
        }
        // ReShade's absent ADDON/AddonPath defaults to the module directory.
        var root = string.IsNullOrWhiteSpace(value) ? game.Directory : Path.GetFullPath(Path.IsPathRooted(value) ? value : Path.Combine(game.Directory, value));
        if (!SafeFiles.PathEquals(root, game.Directory) && !root.StartsWith(game.Directory.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("AddOnPath 指向游戏外共享目录，首版只支持每游戏独立安装。");
        SafeFiles.RejectReparse(root); return root;
    }
    static void WriteFile(PlannedFile f, TargetLease lease, CancellationToken token)
    {
        var tmp = f.Destination + ".dlss5-new-" + Guid.NewGuid().ToString("N");
        try
        {
            if (f.Content != null) { using var fs = new FileStream(tmp, FileMode.CreateNew, FileAccess.Write, FileShare.None); fs.Write(f.Content); fs.Flush(true); }
            else { SafeFiles.RejectReparse(f.SourcePath!); using var source = File.OpenRead(f.SourcePath!); using var target = new FileStream(tmp, FileMode.CreateNew, FileAccess.Write, FileShare.None); source.CopyTo(target); target.Flush(true); }
            if (!Equal(PackageManifest.Hash(tmp), f.AfterSha256)) throw new InvalidDataException("暂存文件校验失败。");
            Checkpoint("before-move"); token.ThrowIfCancellationRequested(); SafeFiles.RejectReparse(f.Destination);
            if (!Equal(SafeFiles.ExistingHash(f.Destination), f.BeforeSha256)) throw new IOException("替换前目标已变化，保留当前内容。");
            File.Move(tmp, f.Destination, true);
        }
        finally { if (File.Exists(tmp)) File.Delete(tmp); }
    }
    static bool Rollback(GameCandidate game, InstallTransaction tx, TargetLease lease)
    {
        var complete = true;
        foreach (var r in tx.Files.AsEnumerable().Reverse().Where(x => x.Prepared)) if (RestoreOne(game, r, lease) == RestoreOutcome.MissingBackup) complete = false;
        if (!complete) return false;
        if (tx.PreviousInstalledJson == null) { if (File.Exists(InstalledPath(game))) File.Delete(InstalledPath(game)); }
        else SafeFiles.AtomicText(InstalledPath(game), tx.PreviousInstalledJson);
        tx.Status = "rolledback"; Save(game, tx); if (File.Exists(ActivePath(game))) File.Delete(ActivePath(game)); return true;
    }
    enum RestoreOutcome { Restored, Preserved, MissingBackup }
    static RestoreOutcome RestoreOne(GameCandidate game, OwnedFile r, TargetLease lease)
    {
        ValidateRecord(game, r); var destination = ResolveOwned(game, r); var current = SafeFiles.ExistingHash(destination);
        if (Equal(current, r.Before)) return RestoreOutcome.Restored;
        if (!Equal(current, r.After)) return RestoreOutcome.Preserved;
        // Missing files after a Write are user deletions; a recorded Remove is reversible.
        if (current == null && !r.Removal) return RestoreOutcome.Preserved;
        if (r.Before == null) { if (current != null) File.Delete(destination); return RestoreOutcome.Restored; }
        var backup = Path.Combine(TransactionDirectory(game, r.BackupTransactionId), r.BackupFile); SafeFiles.RejectReparse(backup);
        if (!File.Exists(backup) || !Equal(PackageManifest.Hash(backup), r.Before)) return RestoreOutcome.MissingBackup;
        var parent = Path.GetDirectoryName(destination)!; Directory.CreateDirectory(parent); lease.Pin(parent);
        WriteFile(new(destination, backup, current, r.Before, FileAction.Write, "restore"), lease, CancellationToken.None); return RestoreOutcome.Restored;
    }
    static void ValidateRecord(GameCandidate game, OwnedFile r)
    {
        var path = ResolveOwned(game, r); var leaf = Path.GetFileName(path);
        if (leaf is not ("dxgi.dll" or "ReShade.ini" or "nrchain_nvngx.dll" or "nvngx_dlssnr.dll" or "nr_before_sr.ini" or "Collect-Feedback.cmd" or "collect-feedback.ps1" or "Collect-Crash-Feedback.cmd" or "collect-crash-feedback.ps1") && !leaf.EndsWith(".addon64", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("安装记录目标不是托管文件。");
        if ((r.Before != null && !PackageManifest.IsHex(r.Before, 64)) || (r.After != null && !PackageManifest.IsHex(r.After, 64)) || !Guid.TryParseExact(r.BackupTransactionId, "N", out _) || PackageManifest.NormalizeLeaf(r.BackupFile) != r.BackupFile) throw new InvalidDataException("备份记录身份无效。");
    }
    static string ResolveOwned(GameCandidate game, OwnedFile r) { var path = PackageManifest.SafePath(game.Directory, r.RelativePath); EnsureInside(game, path); return path; }
    static void EnsureInside(GameCandidate game, string path) { var relative = Path.GetRelativePath(game.Directory, path); PackageManifest.SafePath(game.Directory, relative); SafeFiles.RejectReparse(path); }
    static InstallTransaction ReadTransaction(GameCandidate game, string id)
    { var tx = ReadBounded<InstallTransaction>(JournalPath(game, id)); if (tx.Schema != "dlss5-transaction/v3" || tx.Id != id || !SafeFiles.PathEquals(tx.Executable, game.Executable)) throw new InvalidDataException("事务身份不匹配。"); foreach (var r in tx.Files) ValidateRecord(game, r); return tx; }
    static T ReadBounded<T>(string path) { SafeFiles.RejectReparse(path); if (new FileInfo(path).Length > 16 * 1024 * 1024) throw new InvalidDataException("安装记录过大。"); return ReadJson<T>(File.ReadAllText(path)); }
    static T ReadJson<T>(string text) => JsonSerializer.Deserialize<T>(text, PackageManifest.JsonOptions()) ?? throw new InvalidDataException("安装记录无法解析。");
    static void Save(GameCandidate game, InstallTransaction tx) => SafeFiles.AtomicText(JournalPath(game, tx.Id), JsonSerializer.Serialize(tx, PackageManifest.JsonOptions()));
    static OwnedFile Clone(OwnedFile f) => ReadJson<OwnedFile>(JsonSerializer.Serialize(f));
    static bool Equal(string? a, string? b) => string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
    static bool SameOperation(PlannedFile a, PlannedFile b) => SafeFiles.PathEquals(a.Destination, b.Destination) && a.Action == b.Action && a.Role == b.Role && Equal(a.BeforeSha256, b.BeforeSha256) && Equal(a.AfterSha256, b.AfterSha256);
}

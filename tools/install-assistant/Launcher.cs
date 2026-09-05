using System.Diagnostics;
using System.Text.Json;
using System.Security.Principal;

namespace Dlss5InstallAssistant;

public static class Launcher
{
    internal static Action<GameCandidate>? TestStartRequest { get; set; }
    internal static Func<GameCandidate, bool>? TestExactProcessRunning { get; set; }
    internal static TimeSpan TestLaunchTimeout { get; set; } = TimeSpan.Zero;
    public static async Task StartAsync(GameCandidate game, CancellationToken token = default)
    {
        if (OperatingSystem.IsWindows() && new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))
            throw new InvalidOperationException("请以普通权限打开助手再启动游戏；管理员权限仅用于安装 helper。");
        using var lease = Deployer.AcquireForLaunch(game);
        GameInspector.EnsureNotRunning(game);
        if (TestStartRequest != null) TestStartRequest(game);
        else if (!string.IsNullOrWhiteSpace(game.SteamAppId) && ulong.TryParse(game.SteamAppId, out _))
            Process.Start(new ProcessStartInfo("steam://rungameid/" + game.SteamAppId) { UseShellExecute = true });
        else Process.Start(new ProcessStartInfo(game.Executable) { WorkingDirectory = game.Directory, UseShellExecute = true });

        var timeout = TestLaunchTimeout > TimeSpan.Zero ? TestLaunchTimeout : TimeSpan.FromSeconds(45);
        var expires = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < expires)
        {
            token.ThrowIfCancellationRequested();
            if ((TestExactProcessRunning ?? GameInspector.IsExactProcessRunning)(game)) return;
            await Task.Delay(100, token).ConfigureAwait(false);
        }
        throw new TimeoutException("没有在 45 秒内确认所选游戏进程，启动保护已经结束。若游戏稍后才出现，请不要安装、恢复或卸载；先退出游戏，再重新操作。");
    }
    public static RuntimeStatus? ReadStatus(GameCandidate game)
    {
        var installed = Deployer.ReadInstalled(game);
        var root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DLSS5InstallAssistant", "runtime");
        if (!Directory.Exists(root)) return null;
        foreach (var path in Directory.EnumerateFiles(root, "*.json").OrderByDescending(File.GetLastWriteTimeUtc))
        {
            try
            {
                if (new FileInfo(path).Length > 1024 * 1024) continue;
                var s = JsonSerializer.Deserialize<RuntimeStatus>(File.ReadAllText(path), PackageManifest.JsonOptions()); var now = DateTimeOffset.UtcNow;
                if (s == null || s.Schema != "dlss5-runtime-status/v2" || !int.TryParse(Path.GetFileNameWithoutExtension(path), out var pid) ||
                    s.ProcessId != pid || s.ProcessStartedAt is null || s.UpdatedAt is null || s.UpdatedAt > now || now - s.UpdatedAt > TimeSpan.FromSeconds(15) ||
                    !SafeFiles.PathEquals(s.Executable, game.Executable)) continue;
                using var process = Process.GetProcessById(pid);
                if (process.HasExited || !SafeFiles.PathEquals(process.MainModule?.FileName, game.Executable) || Math.Abs((new DateTimeOffset(process.StartTime.ToUniversalTime()) - s.ProcessStartedAt.Value).TotalSeconds) > 1) continue;
                s.DeploymentMatches = installed != null && s.AddonIdentityStatus == "verified" && PackageManifest.IsHex(s.AddonSha256, 64) &&
                    SafeFiles.PathEquals(s.AddonPath, PackageManifest.SafePath(game.Directory, installed.AddonRelativePath)) &&
                    string.Equals(s.AddonSha256, installed.AddonSha256, StringComparison.OrdinalIgnoreCase) &&
                    string.Equals(s.ArtifactId, installed.ArtifactId, StringComparison.OrdinalIgnoreCase) &&
                    string.Equals(s.BuildId, installed.Build, StringComparison.Ordinal) &&
                    PackageManifest.IsHex(s.SourceCommit, 40) &&
                    string.Equals(s.SourceCommit, installed.PayloadSourceCommit, StringComparison.OrdinalIgnoreCase) &&
                    !s.SourceDirty;
                s.Active = s.Active && s.State == "active" && s.NativeSrObserved && s.NrEvaluations > 0 && s.TransferSubmitted > 0 && s.ConfigGeneration > 0 && (string.IsNullOrEmpty(s.BypassReason) || s.BypassReason == "none");
                return s;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception or ArgumentException or InvalidOperationException or JsonException) { }
        }
        return null;
    }
    public static string Describe(GameCandidate game)
    {
        var installed = Deployer.ReadInstalled(game); var status = ReadStatus(game);
        if (status == null) return installed == null ? (game.ProfileId.Length == 0 ? "未部署；游戏尚未验证" : "未部署") : "已部署；尚未收到当前进程证据";
        if (!status.DeploymentMatches) return "已加载插件，但路径/哈希与本次部署不匹配";
        if (status.Active) return $"NR 正在提交（PID {status.ProcessId}）";
        return status.State switch { "disabled" => "已加载，NR 已关闭", "bypassed" => "已加载，旁路：" + status.BypassReason, "waiting" => "已加载，等待连续 NR 提交", _ => "已加载，等待验证" };
    }
}

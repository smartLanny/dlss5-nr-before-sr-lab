using System.Diagnostics;
using System.Net.Http;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;

namespace Dlss5InstallAssistant;

public sealed record VerifiedReShadeHost(string HostPath, string HostSha256,
    string SetupSha256, string Version);

/// <summary>
/// Uses the pinned official Full Add-on setup only in an isolated cache directory.
/// Game files are exclusively managed by Deployer's reversible transaction.
/// </summary>
public static class ReShadeHost
{
    public const string Version = "6.8.0";
    public const string PinnedSetupSha256 = "AFE4C8F13048306307983B8B3D41D5BF00A86820440B0E57DEA10950E1176445";
    public const string PinnedHostSha256 = "0CEE63F9C9F13F3AC909C5B4903F4DBB4B719A7AB3B4F13B0DEAF83C814B94F7";
    public const string SetupFileName = "ReShade_Setup_6.8.0_Addon.exe";
    public const string OfficialSetupUrl = "https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe";
    private const long MaximumSetupBytes = 16 * 1024 * 1024;

    private static string CacheDirectory(string cacheRoot)
    {
        var root = Path.GetFullPath(cacheRoot);
        RejectReparsePath(root);
        var result = Path.Combine(root, "reshade-" + Version);
        RejectReparsePath(result);
        return result;
    }

    private static void RejectReparsePath(string path)
    {
        for (var current = Path.GetFullPath(path); !string.IsNullOrEmpty(current);
             current = Path.GetDirectoryName(current))
        {
            if ((File.Exists(current) || Directory.Exists(current)) &&
                (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("ReShade 缓存路径包含链接目录，请选择本地普通目录。");
        }
    }

    private static string HashFile(string path)
    {
        RejectReparsePath(path);
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    private static bool VerifyHost(string path)
    {
        try
        {
            RejectReparsePath(path);
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (!Convert.ToHexString(SHA256.HashData(stream)).Equals(PinnedHostSha256, StringComparison.Ordinal))
                return false;
            stream.Position = 0;
            using var pe = new PEReader(stream);
            return pe.PEHeaders.CoffHeader.Machine == Machine.Amd64 &&
                   (pe.PEHeaders.CoffHeader.Characteristics & Characteristics.Dll) != 0;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or BadImageFormatException)
        {
            return false;
        }
    }

    public static VerifiedReShadeHost? TryGetCached(string cacheRoot)
    {
        var path = Path.Combine(CacheDirectory(cacheRoot), "dxgi.dll");
        return VerifyHost(path) ? new(path, PinnedHostSha256, PinnedSetupSha256, Version) : null;
    }

    private static async Task<FileStream> LockCacheAsync(string directory, CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, "prepare.lock");
        var until = DateTime.UtcNow.AddSeconds(60);
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            RejectReparsePath(path);
            try { return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
            catch (IOException) when (DateTime.UtcNow < until)
            { await Task.Delay(100, cancellationToken).ConfigureAwait(false); }
        }
    }

    // Importing never executes the selected file. Only the pinned digest is accepted.
    public static async Task ImportSetupAsync(string setupPath, string cacheRoot,
        CancellationToken cancellationToken = default)
    {
        var directory = CacheDirectory(cacheRoot);
        using var cacheLock = await LockCacheAsync(directory, cancellationToken).ConfigureAwait(false);
        RejectReparsePath(setupPath);
        await using var input = new FileStream(setupPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        await StoreVerifiedSetupAsync(input, directory, cancellationToken).ConfigureAwait(false);
    }

    private static async Task StoreVerifiedSetupAsync(Stream input, string directory, CancellationToken token)
    {
        var temporary = Path.Combine(directory, "setup-" + Guid.NewGuid().ToString("N") + ".part");
        try
        {
            await using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                var buffer = new byte[64 * 1024];
                long size = 0;
                int count;
                while ((count = await input.ReadAsync(buffer, token).ConfigureAwait(false)) != 0)
                {
                    size += count;
                    if (size > MaximumSetupBytes) throw new InvalidDataException("ReShade 安装器超过允许大小。");
                    await output.WriteAsync(buffer.AsMemory(0, count), token).ConfigureAwait(false);
                }
            }
            if (HashFile(temporary) != PinnedSetupSha256)
                throw new InvalidDataException("需要官方 ReShade 6.8.0 Full Add-on 安装器；所选文件的校验值不匹配。");
            var target = Path.Combine(directory, SetupFileName);
            RejectReparsePath(target);
            File.Move(temporary, target, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }

    public static async Task<VerifiedReShadeHost> PrepareAsync(string cacheRoot,
        CancellationToken cancellationToken = default)
    {
        var directory = CacheDirectory(cacheRoot);
        using var cacheLock = await LockCacheAsync(directory, cancellationToken).ConfigureAwait(false);
        var cached = TryGetCached(cacheRoot);
        if (cached is not null) return cached;

        var setup = Path.Combine(directory, SetupFileName);
        if (!File.Exists(setup) || HashFile(setup) != PinnedSetupSha256)
        {
            using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(45) };
            using var response = await client.GetAsync(OfficialSetupUrl,
                HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
            response.EnsureSuccessStatusCode();
            await using var input = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
            await StoreVerifiedSetupAsync(input, directory, cancellationToken).ConfigureAwait(false);
        }

        // The official .NET Framework setup does not accept arbitrary long paths.
        // Keep its entire working directory short; our .NET host can publish into a long cache path.
        var sandbox = Path.Combine(Path.GetTempPath(), "DLSS5ReShade", Guid.NewGuid().ToString("N"));
        RejectReparsePath(sandbox);
        Directory.CreateDirectory(sandbox);
        // A valid x64 PE is needed only for setup's architecture parser. The probe is never launched.
        var self = Environment.ProcessPath ?? throw new InvalidOperationException("无法定位安装助手程序。");
        var probe = Path.Combine(sandbox, "dlss5-probe-" + Guid.NewGuid().ToString("N") + ".exe");
        File.Copy(self, probe);
        // Hold the installer immutable until its child process exits.
        using var setupLease = new FileStream(setup, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (Convert.ToHexString(SHA256.HashData(setupLease)) != PinnedSetupSha256)
            throw new InvalidDataException("ReShade 安装器在准备过程中发生变化。");
        var stagedSetup = Path.Combine(sandbox, SetupFileName);
        setupLease.Position = 0;
        using (var output = new FileStream(stagedSetup, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            setupLease.CopyTo(output);
        using var stagedLease = new FileStream(stagedSetup, FileMode.Open, FileAccess.Read, FileShare.Read);
        var start = new ProcessStartInfo(stagedSetup)
        {
            WorkingDirectory = sandbox, UseShellExecute = false,
            CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        start.ArgumentList.Add(probe);
        start.ArgumentList.Add("--headless");
        start.ArgumentList.Add("--api");
        // DXGI is the loader used by our two D3D12 game profiles. Official d3d12 uses a different filename.
        start.ArgumentList.Add("dxgi");
        using var process = Process.Start(start) ?? throw new IOException("无法启动官方 ReShade 安装器。");
        var outputTask = process.StandardOutput.ReadToEndAsync();
        var errorTask = process.StandardError.ReadToEndAsync();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(60));
        try { await process.WaitForExitAsync(timeout.Token).ConfigureAwait(false); }
        catch (OperationCanceledException)
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            await process.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            if (cancellationToken.IsCancellationRequested) throw;
            throw new TimeoutException("官方 ReShade 安装器准备超时，游戏文件尚未修改。");
        }
        var outputText = await outputTask.ConfigureAwait(false);
        var errorText = await errorTask.ConfigureAwait(false);
        var log = outputText + Environment.NewLine + errorText;
        File.WriteAllText(Path.Combine(directory, "last-setup.txt"), log.Length > 65536 ? log[..65536] : log);
        if (process.ExitCode != 0)
            throw new IOException($"官方 ReShade 安装器失败（退出码 {process.ExitCode}），游戏文件尚未修改。详情见缓存 last-setup.txt。");
        var generated = Path.Combine(sandbox, "dxgi.dll");
        if (!VerifyHost(generated))
            throw new InvalidDataException("官方安装器未生成预期的 x64 Full Add-on 宿主，游戏文件尚未修改。");
        var destination = Path.Combine(directory, "dxgi.dll");
        RejectReparsePath(destination);
        File.Copy(generated, destination, true);
        return TryGetCached(cacheRoot) ?? throw new IOException("ReShade 缓存校验失败。");
    }
}

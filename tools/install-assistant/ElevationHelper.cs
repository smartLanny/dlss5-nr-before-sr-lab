using System.Diagnostics;
using System.Text.Json;

namespace Dlss5InstallAssistant;

public sealed class HelperRequest
{
    public string Id { get; set; } = "";
    public string Operation { get; set; } = "";
    public string Executable { get; set; } = "";
    public string? SteamAppId { get; set; }
    public string? PackageRoot { get; set; }
    public string CacheRoot { get; set; } = "";
}
public sealed class HelperResponse
{
    public string Id { get; set; } = "";
    public bool Ok { get; set; }
    public string Message { get; set; } = "";
}
public static class ElevationHelper
{
    public static string CacheRoot => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DLSS5InstallAssistant", "cache", "ReShade-6.8.0-FullAddon");
    public static string OperationsRoot => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DLSS5InstallAssistant", "operations");
    public static string HelperPath => Path.Combine(AppContext.BaseDirectory, "DLSS5InstallAssistant.Helper.exe");
    public static async Task<OperationResult> TryRunForAsync(string operation, GameCandidate game, string? packageRoot, CancellationToken token)
    {
        var trustedFiles = PackageManifest.VerifyEnvelope(AppContext.BaseDirectory, PackageManifest.ReleasePublicKey);
        if (!trustedFiles.Contains("DLSS5InstallAssistant.Helper.exe")) throw new InvalidDataException("helper 未包含在本安装器签名中，未请求提权。");
        using var helperLease = SafeFiles.OpenExpectedReadLease(HelperPath, trustedFiles.HashOf("DLSS5InstallAssistant.Helper.exe"));
        SafeFiles.RejectReparse(OperationsRoot); Directory.CreateDirectory(OperationsRoot);
        var id = Guid.NewGuid().ToString("N"); var request = new HelperRequest { Id = id, Operation = operation, Executable = game.Executable, SteamAppId = game.SteamAppId, PackageRoot = packageRoot, CacheRoot = CacheRoot };
        var requestPath = Path.Combine(OperationsRoot, id + ".request.json"); var responsePath = Path.Combine(OperationsRoot, id + ".response.json"); var cancelPath = Path.Combine(OperationsRoot, id + ".cancel");
        FileStream? requestLease = null;
        SafeFiles.AtomicText(requestPath, JsonSerializer.Serialize(request));
        var requestHash = PackageManifest.Hash(requestPath);
        requestLease = SafeFiles.OpenExpectedReadLease(requestPath, requestHash);
        try
        {
            var info = new ProcessStartInfo(HelperPath) { UseShellExecute = true, Verb = "runas", WorkingDirectory = AppContext.BaseDirectory, WindowStyle = ProcessWindowStyle.Hidden };
            info.ArgumentList.Add(requestPath);
            info.ArgumentList.Add(requestHash);
            using var process = Process.Start(info) ?? throw new IOException("系统没有启动 helper。");
            using var registration = token.Register(() => { try { SafeFiles.AtomicText(cancelPath, "cancel"); } catch { } });
            await process.WaitForExitAsync();
            if (!File.Exists(responsePath)) return new(false, $"helper 未返回结果（退出码 {process.ExitCode}）；请检查中断事务。");
            var response = JsonSerializer.Deserialize<HelperResponse>(File.ReadAllText(responsePath));
            if (response == null || response.Id != id) return new(false, "helper 返回的事务身份不匹配。");
            return new(process.ExitCode == 0 && response.Ok, response.Message);
        }
        catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode == 1223) { return new(false, "已取消管理员授权，没有启动提权安装。"); }
        finally { requestLease?.Dispose(); foreach (var path in new[] { requestPath, responsePath, cancelPath }) { try { if (File.Exists(path)) File.Delete(path); } catch { } } }
    }
    internal static HelperRequest ReadBoundRequest(string requestPath, string expectedSha256)
    {
        requestPath = Path.GetFullPath(requestPath); SafeFiles.RejectReparse(requestPath);
        if (!SafeFiles.PathEquals(Path.GetDirectoryName(requestPath), OperationsRoot)) throw new InvalidDataException("helper 请求目录无效。");
        using var requestLease = SafeFiles.OpenExpectedReadLease(requestPath, expectedSha256);
        if (requestLease.Length > 32768) throw new InvalidDataException("helper 请求过大。");
        using var memory = new MemoryStream(); requestLease.CopyTo(memory);
        return JsonSerializer.Deserialize<HelperRequest>(memory.ToArray()) ?? throw new InvalidDataException("helper 请求为空。");
    }
    public static async Task<int> RunRequestAsync(string requestPath, string expectedSha256)
    {
        string? responsePath = null; var id = "";
        try
        {
            requestPath = Path.GetFullPath(requestPath);
            var request = ReadBoundRequest(requestPath, expectedSha256);
            if (!Guid.TryParseExact(request.Id, "N", out _) || Path.GetFileName(requestPath) != request.Id + ".request.json") return 2;
            id = request.Id; responsePath = Path.Combine(OperationsRoot, id + ".response.json");
            var game = GameInspector.Inspect(request.Executable, request.SteamAppId); var cancel = Path.Combine(OperationsRoot, id + ".cancel");
            using var cts = new CancellationTokenSource(); using var timer = new Timer(_ => { if (File.Exists(cancel)) cts.Cancel(); }, null, 0, 100);
            OperationResult result;
            if (request.Operation == "install")
            {
                if (request.PackageRoot == null || !SafeFiles.PathEquals(request.CacheRoot, CacheRoot)) throw new InvalidDataException("helper 安装请求路径无效。");
                var package = PackageManifest.Load(request.PackageRoot); var host = ReShadeHost.TryGetCached(CacheRoot) ?? throw new InvalidDataException("官方宿主缓存未验证，请先在普通权限下准备。");
                result = Deployer.Install(game, package, host, cts.Token);
            }
            else result = request.Operation switch { "uninstall" => Deployer.Uninstall(game), "restore" => Deployer.RestoreLatest(game), "recover" => Deployer.Recover(game), _ => throw new InvalidDataException("helper 操作不在允许列表。") };
            SafeFiles.AtomicText(responsePath, JsonSerializer.Serialize(new HelperResponse { Id = id, Ok = result.Ok, Message = result.Message }));
            await Task.CompletedTask; return result.Ok ? 0 : 1;
        }
        catch (Exception ex)
        {
            if (responsePath != null) { try { SafeFiles.AtomicText(responsePath, JsonSerializer.Serialize(new HelperResponse { Id = id, Ok = false, Message = ex is OperationCanceledException ? "安装已取消，已尝试安全回滚。" : ex.Message })); } catch { } }
            return 1;
        }
    }
}

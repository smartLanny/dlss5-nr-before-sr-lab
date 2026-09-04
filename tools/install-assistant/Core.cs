using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

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
    public string ProfileId { get; set; } = "";
}

public sealed class CompatibilityProfile
{
    public string Id { get; set; } = "";
    public List<string> ExecutableSha256 { get; set; } = new();
    public List<string> SteamAppIds { get; set; } = new();
    public bool VerifiedNativeD3D12 { get; set; }
    public string Evidence { get; set; } = "";
    public bool Matches(GameCandidate game) => game.EvidenceConfirmed &&
        ((game.ProfileId == Id) || ExecutableSha256.Contains(game.ExecutableSha256, StringComparer.OrdinalIgnoreCase));
}

public sealed class PackageFile
{
    public string Path { get; set; } = "";
    public string Sha256 { get; set; } = "";
    public long Bytes { get; set; }
    public string Role { get; set; } = "";
    public string Source { get; set; } = "";
    public string Target { get; set; } = "addon-directory";
    public bool Required { get; set; } = true;
    public List<string> AllowedExistingSha256 { get; set; } = new();
}

public sealed class LegacyFile
{
    public string Path { get; set; } = "";
    public string Sha256 { get; set; } = "";
}

// The compiled trust anchor authenticates both the manifest and payload bytes.
// A public key placed next to an imported package is never trusted.
public sealed class PackageManifest
{
    internal const string ReleasePublicKey = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE9kpPkc3saExOxRh60zW/jQ57Romdz10DNwpY7DqZC/e+Du3RRSopwYLo/RMhaFuD1rI6cZ5dUPcXWMmG4QLbXA==";
    public string Schema { get; set; } = "";
    public string Id { get; set; } = "";
    public string Build { get; set; } = "";
    public string PayloadSourceCommit { get; set; } = "";
    public string ArtifactId { get; set; } = "";
    public string GpuFamily { get; set; } = "";
    public string Language { get; set; } = "";
    public bool NativeD3D12 { get; set; }
    public bool OnlineAllowed { get; set; }
    public List<PackageFile> Files { get; set; } = new();
    public List<CompatibilityProfile> CompatibleProfiles { get; set; } = new();
    public List<LegacyFile> LegacyFiles { get; set; } = new();
    [JsonIgnore] public string Root { get; private set; } = "";
    [JsonIgnore] public string ManifestSha256 { get; private set; } = "";
    [JsonIgnore] internal string TrustKey { get; private set; } = ReleasePublicKey;
    public static PackageManifest Load(string root) => LoadWithTrust(root, ReleasePublicKey);
    internal static PackageManifest LoadWithTrust(string root, string trustKey)
    {
        root = Path.GetFullPath(root); SafeFiles.RejectReparse(root);
        var signed = VerifyEnvelope(root, trustKey);
        var path = SafePath(root, "manifest.json");
        if (!signed.Contains("manifest.json")) throw new InvalidDataException("签名不包含安装清单。");
        var p = JsonSerializer.Deserialize<PackageManifest>(File.ReadAllText(path), JsonOptions()) ?? throw new InvalidDataException("安装清单无法读取。");
        p.Root = root; p.TrustKey = trustKey; p.ManifestSha256 = Hash(path);
        if (p.Schema != "dlss5-install-manifest/v3" || p.Id != "nr-before-sr" || string.IsNullOrWhiteSpace(p.Build) ||
            !IsHex(p.PayloadSourceCommit, 40) || !p.NativeD3D12 || p.OnlineAllowed || p.Files.Count < 4 || p.Files.Count > 8 ||
            p.CompatibleProfiles.Count == 0 || p.GpuFamily is not ("RTX50" or "RTX30-40") || p.Language is not ("zh-CN" or "en"))
            throw new InvalidDataException("安装清单身份、版本、显卡或兼容声明不完整。");
        foreach (var profile in p.CompatibleProfiles)
            if (profile.Id is not ("cyberpunk2077-d3d12" or "witcher3-d3d12") || !profile.VerifiedNativeD3D12 ||
                string.IsNullOrWhiteSpace(profile.Evidence) || profile.ExecutableSha256.Any(x => !IsHex(x, 64)))
                throw new InvalidDataException("首版只接受已验证的赛博朋克2077和巫师3 D3D12配置。");
        var roles = new HashSet<string>(StringComparer.Ordinal); var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var f in p.Files)
        {
            f.Path = NormalizeLeaf(f.Path); var q = SafePath(root, f.Path);
            if (!roles.Add(f.Role) || !names.Add(f.Path) || !signed.Contains(f.Path) || (f.Role is "addon" or "bridge" or "runtime" or "config" && !f.Required) || f.Target != "addon-directory" ||
                !IsHex(f.Sha256, 64) || f.AllowedExistingSha256.Any(x => !IsHex(x, 64)) || f.Bytes <= 0 ||
                new FileInfo(q).Length != f.Bytes || !Hash(q).Equals(f.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("载荷校验失败：" + f.Path);
            var validName = f.Role switch { "addon" => f.Path.EndsWith(".addon64", StringComparison.OrdinalIgnoreCase),
                "bridge" => f.Path == "nrchain_nvngx.dll", "runtime" => f.Path == "nvngx_dlssnr.dll",
                "config" => f.Path == "nr_before_sr.ini", "feedback-launcher" => f.Path == "Collect-Feedback.cmd",
                "feedback-collector" => f.Path == "collect-feedback.ps1",
                "crash-feedback-launcher" => f.Path == "Collect-Crash-Feedback.cmd",
                "crash-feedback-collector" => f.Path == "collect-crash-feedback.ps1", _ => false };
            if (!validName) throw new InvalidDataException("载荷角色与文件名不匹配：" + f.Path);
            if (f.Role is "addon" or "bridge" or "runtime" && !PeFacts.IsX64(q)) throw new InvalidDataException("载荷不是有效 x64 PE：" + f.Path);
        }
        if (!new[] { "addon", "bridge", "runtime", "config" }.All(roles.Contains)) throw new InvalidDataException("缺少必需载荷角色。");
        if (!p.ArtifactId.Equals("sha256:" + p.Files.Single(x => x.Role == "addon").Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("插件 artifactId 与载荷哈希不一致。");
        foreach (var old in p.LegacyFiles) { old.Path = NormalizeLeaf(old.Path); if (!old.Path.EndsWith(".addon64", StringComparison.OrdinalIgnoreCase) || !IsHex(old.Sha256, 64)) throw new InvalidDataException("旧插件身份非法。"); }
        return p;
    }
    internal static HashSet<string> VerifyEnvelope(string root, string key)
    {
        var manifest = SafePath(root, "RELEASE_MANIFEST.json"); var signature = SafePath(root, "RELEASE_MANIFEST.sig");
        SafeFiles.RejectReparse(manifest); SafeFiles.RejectReparse(signature);
        if (new FileInfo(manifest).Length > 1024 * 1024 || new FileInfo(signature).Length > 16384) throw new InvalidDataException("签名清单过大。");
        var bytes = File.ReadAllBytes(manifest); using var doc = JsonDocument.Parse(bytes); using var sig = JsonDocument.Parse(File.ReadAllBytes(signature));
        var d = doc.RootElement; var s = sig.RootElement; var publicBytes = Convert.FromBase64String(key);
        var keyId = Convert.ToHexString(SHA256.HashData(publicBytes))[..16];
        if (d.GetProperty("schema").GetString() != "dlss5-release-manifest-v1" || s.GetProperty("schema").GetString() != "dlss5-release-signature-v1" ||
            s.GetProperty("algorithm").GetString() != "ECDSA-P256-SHA256" || d.GetProperty("keyId").GetString() != keyId || s.GetProperty("keyId").GetString() != keyId ||
            s.GetProperty("manifest").GetString() != "RELEASE_MANIFEST.json" || !Convert.ToHexString(SHA256.HashData(bytes)).Equals(s.GetProperty("manifestSha256").GetString(), StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("签名身份不匹配。");
        using var ec = ECDsa.Create(); ec.ImportSubjectPublicKeyInfo(publicBytes, out var read);
        if (read != publicBytes.Length || !ec.VerifyData(bytes, Convert.FromBase64String(s.GetProperty("signatureBase64").GetString()!), HashAlgorithmName.SHA256))
            throw new InvalidDataException("安装包签名无效。");
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var f in d.GetProperty("files").EnumerateArray())
        {
            var name = NormalizeLeaf(f.GetProperty("path").GetString()!); var path = SafePath(root, name); SafeFiles.RejectReparse(path);
            if (!names.Add(name) || !File.Exists(path) || new FileInfo(path).Length != f.GetProperty("bytes").GetInt64() ||
                !Hash(path).Equals(f.GetProperty("sha256").GetString(), StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("签名文件校验失败：" + name);
        }
        return names;
    }
    internal static JsonSerializerOptions JsonOptions() => new() { PropertyNameCaseInsensitive = true, WriteIndented = true };
    internal static bool IsHex(string? s, int length) => s?.Length == length && s.All(Uri.IsHexDigit);
    internal static string NormalizeLeaf(string path) { var rel = NormalizeRelative(path, "非法文件名："); if (rel.Contains(Path.DirectorySeparatorChar)) throw new InvalidDataException("载荷须为单级文件：" + path); return rel; }
    internal static string NormalizeRelative(string path, string message)
    {
        if (string.IsNullOrWhiteSpace(path)) throw new InvalidDataException(message + path);
        var rel = path.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(rel) || rel.Split(Path.DirectorySeparatorChar).Any(x => x is ".." or "." || x.Length == 0 || x.EndsWith('.') || x.EndsWith(' ') || x.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)) throw new InvalidDataException(message + path);
        return rel;
    }
    internal static string SafePath(string root, string rel) { rel = NormalizeRelative(rel, "路径非法："); var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar; var full = Path.GetFullPath(Path.Combine(fullRoot, rel)); if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("目标路径越界。"); return full; }
    internal static string Hash(string path) { using var s = File.OpenRead(path); return Convert.ToHexString(SHA256.HashData(s)); }
}

public sealed class RuntimeStatus
{
    public string Schema { get; set; } = ""; public string State { get; set; } = "unknown"; public string BuildId { get; set; } = "";
    public bool Active { get; set; } public string BypassReason { get; set; } = ""; public DateTimeOffset? UpdatedAt { get; set; }
    public int? ProcessId { get; set; } public DateTimeOffset? ProcessStartedAt { get; set; } public string Executable { get; set; } = "";
    public bool NativeSrObserved { get; set; } public ulong NrEvaluations { get; set; } public ulong TransferSubmitted { get; set; } public int ConfigGeneration { get; set; }
    public string AddonPath { get; set; } = ""; public string AddonSha256 { get; set; } = ""; public string ArtifactId { get; set; } = "";
    public string AddonIdentityStatus { get; set; } = ""; public string SourceCommit { get; set; } = ""; public bool SourceDirty { get; set; }
    [JsonIgnore] public bool DeploymentMatches { get; set; }
}

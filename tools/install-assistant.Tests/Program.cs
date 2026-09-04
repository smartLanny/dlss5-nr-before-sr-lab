using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Dlss5InstallAssistant;

static string Hash(string p) { using var h = SHA256.Create(); return Convert.ToHexString(h.ComputeHash(File.ReadAllBytes(p))); }
var root = Path.Combine(Path.GetTempPath(), "dlss5-assistant-test-" + Guid.NewGuid().ToString("N")); var gameDir = Path.Combine(root, "game"); var pkg = Path.Combine(root, "pkg"); Directory.CreateDirectory(gameDir); Directory.CreateDirectory(pkg);
try
{
    File.WriteAllBytes(Path.Combine(gameDir, "game.exe"), Encoding.ASCII.GetBytes("MZ d3d12.dll"));
    File.WriteAllText(Path.Combine(pkg, "nrchain.dll"), "new"); var sha = Hash(Path.Combine(pkg, "nrchain.dll"));
    File.WriteAllText(Path.Combine(pkg, "manifest.json"), JsonSerializer.Serialize(new { id = "test", build = "t1", nativeD3D12 = true, files = new[] { new { path = "nrchain.dll", sha256 = sha, required = true } } }));
    var manifest = PackageManifest.Load(pkg); var game = GameInspector.Inspect(Path.Combine(gameDir, "game.exe"), null, "Test Game");
    if (!Deployer.Install(game, manifest).Ok || File.ReadAllText(Path.Combine(gameDir, "nrchain.dll")) != "new") throw new Exception("install failed");
    File.WriteAllText(Path.Combine(gameDir, "nrchain.dll"), "user-edited"); Deployer.Uninstall(game); if (!File.Exists(Path.Combine(gameDir, "nrchain.dll"))) throw new Exception("later edit was removed");
    File.WriteAllBytes(Path.Combine(gameDir, "dxgi.dll"), Encoding.ASCII.GetBytes("unknown loader")); var denied = Deployer.Install(game, manifest); if (denied.Ok) throw new Exception("unknown dxgi was overwritten");
    Console.WriteLine("PASS manifest-hash install uninstall-preserve unknown-dxgi");
}
finally { try { Directory.Delete(root, true); } catch { } }

using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Dlss5InstallAssistant;

static string Hash(string p) { using var h = SHA256.Create(); return Convert.ToHexString(h.ComputeHash(File.ReadAllBytes(p))); }
static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
static string Manifest(string pkg, string file, string build = "test-build", string? target = null, string? manifestPath = null, string[]? allowed = null)
{
    var sha = Hash(Path.Combine(pkg, file.Replace('/', Path.DirectorySeparatorChar)));
    var profile = new { steamAppIds = new[] { "1091500" }, executableSha256 = Array.Empty<string>(), verifiedNativeD3D12 = true };
    var evidence = new { verified = true, method = "fixture-evidence", api = "D3D12", executableSha256 = "fixture" };
    var item = new { path = manifestPath ?? file, sha256 = sha, role = "addon", source = "trusted-local", target = target ?? "game-root", required = true, allowedExistingSha256 = allowed ?? Array.Empty<string>() };
    return JsonSerializer.Serialize(new { id = "fixture", build, nativeD3D12 = true, onlineAllowed = false, verifiedNativeD3D12 = evidence, compatibleProfiles = new[] { profile }, files = new[] { item } });
}

var root = Path.Combine(Path.GetTempPath(), "dlss5-assistant-test-" + Guid.NewGuid().ToString("N"));
var gameDir = Path.Combine(root, "game"); var pkg = Path.Combine(root, "pkg"); var addon = Path.Combine(root, "addon-out"); Directory.CreateDirectory(gameDir); Directory.CreateDirectory(pkg); Directory.CreateDirectory(addon);
try
{
    var exe = Path.Combine(gameDir, "game.exe"); File.WriteAllBytes(exe, Encoding.ASCII.GetBytes("MZ d3d12.dll")); File.WriteAllText(Path.Combine(gameDir, "nvngx_dlss.dll"), "native");
    var source = Path.Combine(pkg, "nrchain.dll"); File.WriteAllText(source, "new"); File.WriteAllText(Path.Combine(gameDir, "ReShade.ini"), "AddOnPath = " + addon);
    File.WriteAllText(Path.Combine(pkg, "manifest.json"), Manifest(pkg, "nrchain.dll")); var game = GameInspector.Inspect(exe, "1091500", "Cyberpunk 2077"); var package = PackageManifest.Load(pkg);
    Assert(Deployer.Plan(game, package).Files.Single().Destination == Path.Combine(gameDir, "nrchain.dll"), "game-root mapping"); Assert(Deployer.Install(game, package).Ok, "install failed"); Assert(File.ReadAllText(Path.Combine(gameDir, "nrchain.dll")) == "new", "installed content");
    Deployer.Uninstall(game); Assert(!File.Exists(Path.Combine(gameDir, "nrchain.dll")), "clean uninstall"); Assert(!File.Exists(Path.Combine(gameDir, ".dlss5-install-assistant", "installed.json")), "state cleaned");
    Assert(Deployer.Install(game, package).Ok, "reinstall after state cleanup failed"); File.WriteAllText(Path.Combine(gameDir, "nrchain.dll"), "user-edited"); Deployer.Uninstall(game); Assert(File.ReadAllText(Path.Combine(gameDir, "nrchain.dll")) == "user-edited", "uninstall overwrote user edit");
    File.Delete(Path.Combine(gameDir, "nrchain.dll")); Assert(Deployer.Install(game, package).Ok, "second reinstall failed"); File.WriteAllText(Path.Combine(gameDir, "nrchain.dll"), "restore-user-edit"); Deployer.RestoreLatest(game); Assert(File.ReadAllText(Path.Combine(gameDir, "nrchain.dll")) == "restore-user-edit", "restore overwrote user edit"); File.Delete(Path.Combine(gameDir, "nrchain.dll")); Deployer.Uninstall(game); Assert(!File.Exists(Path.Combine(gameDir, "nrchain.dll")), "uninstall restored user-deleted file");

    File.Delete(Path.Combine(gameDir, "nrchain.dll")); Environment.SetEnvironmentVariable("DLSS5_TEST_FAIL_STAGE", "before-move"); try { Deployer.Install(game, package); throw new Exception("failure injection did not fail"); } catch (IOException) { } finally { Environment.SetEnvironmentVariable("DLSS5_TEST_FAIL_STAGE", null); } Assert(!File.Exists(Path.Combine(gameDir, "nrchain.dll")), "failed install left new file");
    using (var cancelled = new CancellationTokenSource()) { cancelled.Cancel(); try { Deployer.Install(game, package, cancelled.Token); throw new Exception("cancellation was ignored"); } catch (OperationCanceledException) { } } Assert(!File.Exists(Path.Combine(gameDir, "nrchain.dll")), "cancelled install left new file");
    foreach (var stage in new[] { "before-record", "after-backup", "before-move", "after-move" }) { Environment.SetEnvironmentVariable("DLSS5_TEST_FAIL_STAGE", stage); try { Deployer.Install(game, package); throw new Exception("failure injection was ignored: " + stage); } catch (IOException) { } finally { Environment.SetEnvironmentVariable("DLSS5_TEST_FAIL_STAGE", null); } Assert(!File.Exists(Path.Combine(gameDir, "nrchain.dll")), "failure stage left new file: " + stage); }
    File.WriteAllText(Path.Combine(gameDir, "nrchain.dll"), "known"); var allowed = Hash(Path.Combine(gameDir, "nrchain.dll")); File.WriteAllText(Path.Combine(pkg, "manifest.json"), Manifest(pkg, "nrchain.dll", allowed: new[] { allowed })); package = PackageManifest.Load(pkg); Assert(Deployer.Install(game, package).Ok, "known collision was rejected");
    var addonManifest = Manifest(pkg, "nrchain.dll", "addon-build", "reshade-addon"); File.WriteAllText(Path.Combine(pkg, "manifest.json"), addonManifest); package = PackageManifest.Load(pkg); Deployer.Uninstall(game); Assert(Deployer.Install(game, package).Ok, "addon mapping install failed"); var mapped = Path.Combine(addon, "nrchain.dll"); Assert(File.Exists(mapped) && File.ReadAllText(Path.Combine(gameDir, "nrchain.dll")) == "known", "addon wrote game root"); Deployer.Uninstall(game); Assert(!File.Exists(mapped), "addon uninstall failed");

    File.WriteAllText(Path.Combine(pkg, "manifest.json"), Manifest(pkg, "nrchain.dll", "bad", "game-root", "..\\escape.dll")); try { PackageManifest.Load(pkg); throw new Exception("manifest traversal accepted"); } catch (InvalidDataException) { }
    var outside = Path.Combine(root, "outside"); Directory.CreateDirectory(outside); var junction = Path.Combine(gameDir, "junction"); var mk = Process.Start(new ProcessStartInfo("cmd.exe", $"/c mklink /J \"{junction}\" \"{outside}\"") { UseShellExecute = false, CreateNoWindow = true }); mk?.WaitForExit(); if (Directory.Exists(junction) && (File.GetAttributes(junction) & FileAttributes.ReparsePoint) != 0) { Directory.CreateDirectory(Path.Combine(pkg, "junction")); File.Copy(source, Path.Combine(pkg, "junction", "evil.dll"), true); File.WriteAllText(Path.Combine(pkg, "manifest.json"), Manifest(pkg, "nrchain.dll", "junction", "game-root", manifestPath: "junction\\evil.dll")); package = PackageManifest.Load(pkg); try { Deployer.Plan(game, package); throw new Exception("junction accepted"); } catch (InvalidDataException) { } }
    var hd2 = GameInspector.Inspect(exe, "553850", "Helldivers 2"); try { Deployer.Plan(hd2, package); throw new Exception("HD2 accepted"); } catch (InvalidDataException) { }
}
catch (Exception ex) { Console.Error.WriteLine("FAIL " + ex.Message); Environment.ExitCode = 1; }
finally { try { Directory.Delete(root, true); } catch { } }
if (Environment.ExitCode == 0) Console.WriteLine("PASS installer transaction, mapping, collision, traversal, junction, reinstall");

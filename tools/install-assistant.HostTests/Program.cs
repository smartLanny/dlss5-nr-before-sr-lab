using Dlss5InstallAssistant;
var folder = Path.Combine(AppContext.BaseDirectory, "cache-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(folder);
var bad = Path.Combine(folder, "bad.exe"); File.WriteAllText(bad, "wrong setup");
try { await ReShadeHost.ImportSetupAsync(bad, folder); throw new Exception("accepted bad setup"); }
catch (InvalidDataException) { Console.WriteLine("PASS wrong setup refused"); }
await ReShadeHost.ImportSetupAsync(args[0], folder);
Console.WriteLine("PASS local official setup import");
var prepared = await ReShadeHost.PrepareAsync(folder);
if (prepared.HostSha256 != ReShadeHost.PinnedHostSha256) throw new Exception("wrong host");
Console.WriteLine("PASS official headless production PrepareAsync " + prepared.HostSha256);
var twice = await Task.WhenAll(ReShadeHost.PrepareAsync(folder), ReShadeHost.PrepareAsync(folder));
if (twice.Any(x => x.HostPath != prepared.HostPath)) throw new Exception("cache race");
Console.WriteLine("PASS cached and concurrent preparation");
File.WriteAllText(prepared.HostPath, "corrupt host");
if (ReShadeHost.TryGetCached(folder) != null) throw new Exception("accepted corrupt host");
Console.WriteLine("PASS corrupt host not reused");
var repaired = await ReShadeHost.PrepareAsync(folder);
if (repaired.HostSha256 != ReShadeHost.PinnedHostSha256) throw new Exception("cache repair failed");
Console.WriteLine("PASS corrupt cache rebuilt through official setup");
using var cancel = new CancellationTokenSource(); cancel.Cancel();
try { await ReShadeHost.PrepareAsync(folder, cancel.Token); throw new Exception("ignored cancellation"); }
catch (OperationCanceledException) { Console.WriteLine("PASS cancellation"); }
Console.WriteLine("ALL PASS: " + folder);

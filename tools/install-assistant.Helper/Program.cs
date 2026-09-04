using Dlss5InstallAssistant;

if (args.Length < 3) return 2;
var operation = args[0];
var game = GameInspector.Inspect(args[1], string.IsNullOrWhiteSpace(args[2]) ? null : args[2]);
try
{
    switch (operation.ToLowerInvariant())
    {
        case "--install":
            if (args.Length < 4) return 2;
            var package = PackageManifest.Load(args[3]);
            Deployer.Install(game, package);
            return 0;
        case "--uninstall": Deployer.Uninstall(game); return 0;
        case "--restore": Deployer.RestoreLatest(game); return 0;
        case "--recover": Deployer.Recover(game); return 0;
        default: return 2;
    }
}
catch { return 1; }

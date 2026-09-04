using Dlss5InstallAssistant;
return args.Length == 1 ? await ElevationHelper.RunRequestAsync(args[0]) : 2;

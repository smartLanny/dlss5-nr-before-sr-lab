using Dlss5InstallAssistant;
return args.Length == 2 ? await ElevationHelper.RunRequestAsync(args[0], args[1]) : 2;

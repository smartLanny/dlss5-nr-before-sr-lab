# Standalone startup diagnostics

`scripts/startup-diagnostics.cmd` is the double-click entry point, packaged as `启动诊断.cmd` beside `startup-diagnostics.ps1` in the application root. It invokes the system Windows PowerShell by absolute path with `-ExecutionPolicy Bypass`, keeps the console visible and pauses after completion. This policy argument applies only to the child process; it does not change persistent system policy. The helper does not depend on Electron, Node.js, PATH, or the Manager reaching `main.js`. When supplied separately, the two helper files also run without successful portable extraction.

The PowerShell helper checks:

- the expected installed Manager EXE and its file version;
- `resources/app.asar` and the files present in the actual Electron 33 distribution used by this product;
- at least one locale `.pak`;
- Setup/portable/external executable names and sizes in the diagnostic tool's directory or an explicitly supplied source directory;
- TEMP directory writeability with a short-lived probe and available space;
- the count of processes with the Manager executable's exact base name, without reading command lines or terminating anything;
- a limited Windows Application event-log query for Event ID 1000 from the last 24 hours, exporting only entries whose `AppName` matches the Manager executable;
- up to four newest `.log`, `.txt` or `.jsonl` files no larger than 64 KiB from the product's dedicated `startup` directory.

The report includes at most a 24 KiB tail from each eligible startup log and is capped at 128 KiB overall. A partial first line is discarded. Known HOME/LOCALAPPDATA/APPDATA/TEMP/username values are replaced, and arbitrary absolute paths in startup log content are removed as `<PATH>`.

The Event ID 1000 query reads newest first, examines at most 100 events and exports at most three matching summaries. Its loop has a 1,500 ms budget with a 150 ms timeout per read. Each summary contains only the time, matching application name, redacted fault-module name and exception code; full event messages, XML and unrelated crash summaries are not exported. An unavailable log or no match is reported as `UNKNOWN`, not as evidence of successful startup.

The helper does not start the Manager, elevate, install Visual C++ runtimes, delete configuration or Singleton files, enumerate process command lines, inspect games, or include configuration files outside the dedicated startup log directory. The file list reflects the actual Electron package; `version.dll` is not present in this Electron 33 distribution and is not invented as a requirement.

Automated use:

```powershell
& "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
  -NoLogo -NoProfile -ExecutionPolicy Bypass `
  -File scripts\startup-diagnostics.ps1 -NoUI `
  -OutputPath C:\Temp\xiaofeng-startup.txt
```

Fixture parameters are `-InstallPath`, `-TempPath`, `-StartupLogPath`, `-SourcePath`, `-ExecutableName` and `-OutputPath`. An explicit output path is attempted first; otherwise the helper tries its own directory, Desktop, then TEMP. Exit code `0` means the report was written without failed checks, `2` means it was written with failures, and `3` means no report could be written. An `OK` result only confirms the checked file or basic condition; it does not prove Electron created a window.

## Focused verification

`test/startup-diagnostics-helper.test.js` checks the script's UTF-8 BOM and runs the actual system Windows PowerShell 5.1 executable with the same process-level execution policy as the CMD. The regression uses temporary Chinese paths, a uniquely named fixture EXE that is never executed, and a Chinese startup log. It verifies readable report text and removal of temporary write probes. Missing ASAR and invalid TEMP fixtures remain failures.

The 2026-09-09 focused run passed all nine package/helper checks, including Windows PowerShell `5.1.26100.9168`. It excluded the separate optional installed-Manager check and inspected only temporary installation fixtures:

```powershell
node --test --test-concurrency=1 `
  --test-name-pattern="external|package verifier|helper and CMD|system Windows|fixture report|missing ASAR" `
  test/external-package.test.js test/startup-diagnostics-helper.test.js
```

`verify-external-package.cjs` now requires both root diagnostic files and the `windows-registry-values.ps1` / `launcher-locations.ps1` resources, alongside the existing NVAPI and FG resources. Missing, empty or directory replacements for these helpers reject the package. This verifies package contents; it does not establish that a packaged Manager can create a window.

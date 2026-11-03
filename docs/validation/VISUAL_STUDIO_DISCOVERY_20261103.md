# Hosted Visual C++ discovery: preserve the native query result

## Observed failure and limits

[Run 41577288340](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/41577288340),
source `0a573587008dec49443b881c1da3725effbb5479`, passed public portable
checks (73 pass, 0 fail, 4 explicit skips), both host compiler jobs, process
receipt contracts and 131 actual PowerShell 5.1 WARP evidence checks. Dependency
preparation then reported `The standard Windows image lacks the required Visual
C++ tools`. It did not run the native WARP loader, Core build or packaging.

That message combined two distinct conditions: a nonzero `vswhere` exit status
and no selected installation. The script recorded neither the query's output
nor its immediate exit status. The log does **not** establish which condition
occurred or prove that Visual C++ was absent.

Both this runner and the preceding
[run 41575689326](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/41575689326)
reported `windows-2022`, image `20261025.346.1`, OS `10.0.20348` and runner
`2.338.2`. The preceding run prepared its full dependencies with
`C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat`.
Its later canonical step was deliberately cancelled for a source update; it
does not establish full validation of either source.

Failed-run evidence artifact `13204392166` is 4,330 bytes, SHA256
`a8651b067700a3841780f3a62fc7469b832791b4f2dae170363cffb117264656`.
The original failure remains a failure.

## Discovery contract

The original native invocation fed `vswhere` directly into
`Select-Object -First 1`. Microsoft documents that this form can stop its
upstream command as soon as enough objects have arrived:
[PowerShell 5.1 Select-Object](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.utility/select-object?view=powershell-5.1).
Microsoft's [Find VC example](https://github.com/microsoft/vswhere/wiki/Find-VC)
instead captures the complete query before inspecting the installation path.

Discovery must wait for the native query to finish, snapshot its exit status
immediately, then inspect the captured output in memory. A nonzero exit, an
empty selection, multiple paths and a missing environment script are distinct
failures. Preserve the discovery record even on failure, and check the toolchain
before downloading the large dependencies. There is no hardcoded-path fallback,
compiler installation, runner-image switch or acceptance-gate bypass.

A separate PowerShell 7.4.6/Linux experiment used a real native producer that
flushed one path, waited 150 ms, and wrote a normal-exit marker. All three
`Select-Object -First 1` trials returned before that marker; full capture and
`-Wait` trials all preserved it. The observed exit code was still zero in every
trial. This demonstrates the pipeline lifetime problem, **not** the cause of
the failed Windows query. Actual Windows discovery and the complete candidate
still require their own successful run on the repaired source.

The production-function fixture imports the actual discovery function and runs
real child processes. PowerShell 7.4.6/Linux passed 131 checks across nine
discovery outcomes; both the early-pipeline and ignored-nonzero-exit mutations
were rejected. Completion is sampled immediately when the function returns.
If another PowerShell host fully waits even with the old pipeline, that
platform observation is recorded rather than incorrectly rejected. The
nonzero-exit control is always required. The same fixture runs in the initial
Windows PowerShell contract step, before the actual inventory query and inputs.

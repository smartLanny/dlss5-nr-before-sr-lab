param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory = $true)][string]$EvidenceRoot,
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$ReShadeSourceRoot = $env:DLSS5_RESHADE_ROOT,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)

function Get-FrozenCommit {
    $commit = (& git -C $ProjectRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[a-f0-9]{40}$') { throw 'Cannot resolve source commit.' }
    $changes = @(& git -C $ProjectRoot status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $changes.Count -ne 0) {
        throw 'Freeze all source changes before a verified release build. Existing work is preserved.'
    }
    return $commit
}

function Get-ProductFiles {
    return @(Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'build') -File |
        Where-Object { $_.Extension -in @('.addon64', '.dll', '.ini') } |
        Sort-Object Name | ForEach-Object {
            [pscustomobject]@{name=$_.Name; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
        })
}

$commit = Get-FrozenCommit
$receiptPath = Join-Path $EvidenceRoot 'source-build.json'
if ($VerifyOnly) {
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ($receipt.schema -ne 'dlss5-reproducible-build-v1' -or
        $receipt.sourceCommit -ne $commit -or -not $receipt.byteIdentical) { throw 'Build receipt does not identify frozen HEAD.' }
    $current = Get-ProductFiles
    if ($current.Count -ne $receipt.products.Count) { throw 'Build output inventory changed.' }
    foreach ($file in $receipt.products) {
        $found = @($current | Where-Object { $_.name -ceq $file.name })
        if ($found.Count -ne 1 -or $found[0].sha256 -cne $file.sha256 -or $found[0].bytes -ne $file.bytes) {
            throw "Build output no longer matches verified source: $($file.name)"
        }
    }
    Write-Output "Verified frozen source and all products: $commit"
    return
}

if (Test-Path -LiteralPath $EvidenceRoot) { throw 'Choose a new evidence directory; prior results will not be overwritten.' }
New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null
$ToolchainRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$ReShadeSourceRoot = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
$compilerHash = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash
$dependencyFiles = @()
foreach ($relative in @('include', 'deps\imgui')) {
    $base = Join-Path $ReShadeSourceRoot $relative
    $paths = @(& rg --files $base -g '*.h' -g '*.hpp')
    if ($LASTEXITCODE -ne 0) { throw "Cannot inventory dependency headers: $base" }
    foreach ($path in ($paths | Sort-Object)) {
        $dependencyFiles += [pscustomobject]@{name=[IO.Path]::GetRelativePath($ReShadeSourceRoot,$path);sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
}
$first = @()
foreach ($pass in 1,2) {
    if ((Get-FrozenCommit) -ne $commit) { throw 'Source changed during verified build.' }
    $log = Join-Path $EvidenceRoot "build-$pass.log"
    & (Join-Path $ProjectRoot 'scripts\build.ps1') -ToolchainRoot $ToolchainRoot -ReShadeSourceRoot $ReShadeSourceRoot *> $log
    if ($LASTEXITCODE -ne 0) { throw "Build pass $pass failed. See $log" }
    if ((Get-FrozenCommit) -ne $commit) { throw 'Source changed during build; products are not releasable.' }
    $current = Get-ProductFiles
    if (@($current | Where-Object { $_.name -like '*.addon64' }).Count -ne 2 -or
        @($current | Where-Object { $_.name -eq 'nrchain_nvngx.dll' }).Count -ne 1) {
        throw 'Expected exactly two language addons and the bridge.'
    }
    if ($pass -eq 1) {
        $first = $current
        $copy = Join-Path $EvidenceRoot 'first-build'
        New-Item -ItemType Directory -Path $copy | Out-Null
        foreach ($file in $current) { Copy-Item -LiteralPath (Join-Path $ProjectRoot ('build\'+$file.name)) -Destination $copy }
    } else {
        if (($first | ConvertTo-Json -Compress) -cne ($current | ConvertTo-Json -Compress)) {
            throw 'Two complete builds differ. Do not package either result.'
        }
    }
}
if ((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash -ne $compilerHash) { throw 'Compiler changed while building.' }
foreach ($header in $dependencyFiles) {
    if ((Get-FileHash -LiteralPath (Join-Path $ReShadeSourceRoot $header.name) -Algorithm SHA256).Hash -ne $header.sha256) {
        throw 'Dependency headers changed while building.'
    }
}
$receipt = [ordered]@{
    schema='dlss5-reproducible-build-v1'; sourceCommit=$commit;
    sourceTree=(& git -C $ProjectRoot rev-parse 'HEAD^{tree}').Trim();
    sourceClean=$true; byteIdentical=$true; buildPasses=2;
    compilerVersion=((& $compiler --version) | Select-Object -First 1);
    compilerSha256=$compilerHash; headers=$dependencyFiles; products=$current;
    generatedUtc=[DateTime]::UtcNow.ToString('o');
    scope='Source identity and reproducible compilation only. Rendering, installer, and real-game acceptance are recorded separately.'
}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8
Write-Output "Two byte-identical builds from frozen source $commit. Receipt: $receiptPath"

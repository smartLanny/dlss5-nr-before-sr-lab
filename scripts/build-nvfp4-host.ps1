param(
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$WithoutBackend
)
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
$Compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$CCompiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang.exe'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Output=(Resolve-Path -LiteralPath $OutputDirectory).Path
$ArgsList=@('-std=c++17','-O2','-static','-municode',('-I'+$Root))
$Objects=@()
if (-not $WithoutBackend) {
    $MinHook=Join-Path $Root 'third_party/minhook'
    foreach($Leaf in @('buffer','hook','trampoline','hde/hde64')) {
        $Object=Join-Path $Output (($Leaf -replace '/','-')+'.o')
        & $CCompiler -O2 '-I' (Join-Path $MinHook 'include') -c (Join-Path $MinHook ('src/'+$Leaf+'.c')) -o $Object
        if($LASTEXITCODE){throw "MinHook compile failed: $Leaf"}
        $Objects+=$Object
    }
    $ArgsList+=@('-DNR_HOST_BACKEND',('-I'+(Join-Path $MinHook 'include')),(Join-Path $Root 'src/nvfp4/backend.cpp'))
    $ArgsList+=$Objects
}
$Exe=Join-Path $Output $(if($WithoutBackend){'nr-host-fp8.exe'}else{'nr-host.exe'})
& $Compiler @ArgsList (Join-Path $Root 'tests/nvfp4_host/nr_host.cpp') -ld3d12 -ldxgi -ladvapi32 -lbcrypt -o $Exe
if($LASTEXITCODE){throw 'Host compilation failed.'}
Write-Host "Built $Exe"

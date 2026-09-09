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
$SourceInputs=@('tests/nvfp4_host/nr_host.cpp','scripts/build-nvfp4-host.ps1')
if(-not $WithoutBackend){
    $SourceInputs+=@(Get-ChildItem -LiteralPath (Join-Path $Root 'src/nvfp4') -File | Where-Object { $_.Extension -in @('.cpp','.h','.inl') } | ForEach-Object {'src/nvfp4/'+$_.Name})
    $SourceInputs+=@('kernels/nvfp4/nvfp4_kernel_abi.h','kernels/nvfp4/nvfp4_fused_abi.h','kernels/nvfp4_expansion/expansion_abi.h')
    $SourceInputs+=@(Get-ChildItem -LiteralPath (Join-Path $Root 'third_party/minhook') -Recurse -File | Where-Object { $_.Extension -in @('.c','.h') } | ForEach-Object {$_.FullName.Substring($Root.Length+1).Replace('\','/')})
}
$SourceHashes=[ordered]@{}
foreach($SourceInput in $SourceInputs){$SourceHashes[$SourceInput]=(Get-FileHash -LiteralPath (Join-Path $Root $SourceInput) -Algorithm SHA256).Hash.ToLowerInvariant()}
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
foreach($SourceInput in $SourceInputs){if((Get-FileHash -LiteralPath (Join-Path $Root $SourceInput) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $SourceHashes[$SourceInput]){throw 'Host source changed during compilation; preserve this build as unqualified.'}}
@{schema=1;kind='owned signed Feature18 host';source_commit=(& git -C $Root rev-parse HEAD).Trim();
  source_dirty=(@(& git -C $Root status --porcelain).Count -ne 0);sources=$SourceHashes;
  compiler_sha256=(Get-FileHash -LiteralPath $Compiler -Algorithm SHA256).Hash.ToLowerInvariant();
  artifact=[ordered]@{file=[IO.Path]::GetFileName($Exe);sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()};
  gpu_executed=$false;without_backend=$WithoutBackend.IsPresent} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'host-build.json') -Encoding UTF8
Write-Host "Built $Exe"

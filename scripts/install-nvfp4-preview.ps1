param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$PackageDirectory=$PSScriptRoot,
    [switch]$Restore
)
# Explicit user-run deployment/restore helper. Packaging never invokes it.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$game=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\','/')
$prefix=$game+[IO.Path]::DirectorySeparatorChar
$ledger=Join-Path $game '.nr-nvfp4-preview-install.json'
function InGame([string]$path) {
    $value=[IO.Path]::GetFullPath($path)
    if(-not $value.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Path is outside the selected game directory: $value"}
    return $value
}
function Hash([string]$path){return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()}
foreach($exe in Get-ChildItem -LiteralPath $game -File -Filter '*.exe'){
    if(Get-Process -Name $exe.BaseName -ErrorAction SilentlyContinue){throw "Close $($exe.BaseName) before changing the preview installation."}
}
if($Restore){
    if(-not(Test-Path -LiteralPath $ledger)){throw 'No preview installation receipt exists; nothing was changed.'}
    $receipt=Get-Content -LiteralPath $ledger -Raw -Encoding UTF8 | ConvertFrom-Json
    if($receipt.game_directory -ne $game){throw 'Receipt belongs to another game directory.'}
    foreach($entry in $receipt.entries){
        $target=InGame $entry.target
        if($entry.backup){$null=InGame $entry.backup}
        if(Test-Path -LiteralPath $target){
            $current=Hash $target
            if($current -ne $entry.installed_sha256 -and $current -ne $entry.original_sha256){throw "A later update changed $target; preserve it and resolve the receipt manually."}
        }
        if($entry.backup -and (Test-Path -LiteralPath $entry.backup) -and (Hash $entry.backup) -ne $entry.original_sha256){throw 'Backup hash mismatch.'}
        if($entry.original_sha256 -and (-not $entry.backup -or -not(Test-Path -LiteralPath $entry.backup))){
            if(-not(Test-Path -LiteralPath $target) -or (Hash $target) -ne $entry.original_sha256){throw "Original backup is missing for $target; preserve the receipt instead of claiming restoration."}
        }
    }
    foreach($entry in $receipt.entries){
        $target=InGame $entry.target
        if($entry.backup -and (Test-Path -LiteralPath $entry.backup)){
            if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}
            Move-Item -LiteralPath (InGame $entry.backup) -Destination $target
        }elseif(-not $entry.original_sha256 -and (Test-Path -LiteralPath $target)){
            if((Hash $target) -eq $entry.installed_sha256){Remove-Item -LiteralPath $target}
        }
    }
    $receipt.status='restored'
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ledger -Encoding UTF8
    Move-Item -LiteralPath $ledger -Destination ($ledger+'.restored-'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
    Write-Host 'Restored the previous Core; settings were preserved.'
    exit
}
if(Test-Path -LiteralPath $ledger){throw 'A preview receipt already exists. Restore it before another installation.'}
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$manifest=Get-Content -LiteralPath (Join-Path $package 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($manifest.default_precision -ne 'original FP8' -or $manifest.auto_qualified){throw 'This helper is only for the disabled-by-default source preview.'}
$payload=Join-Path $package 'core-preview'
$addons=@(Get-ChildItem -LiteralPath $payload -File -Filter '*.addon64')
if($addons.Count -ne 1){throw 'Expected one Chinese experimental Core.'}
$files=@($addons[0],(Get-Item -LiteralPath (Join-Path $payload 'nrchain_nvngx.dll')))
if(-not(Test-Path -LiteralPath (Join-Path $game 'nr_before_sr.ini'))){$files+=Get-Item -LiteralPath (Join-Path $payload 'nr_before_sr.ini')}
$backup=InGame (Join-Path $game ('.nr-nvfp4-backups/'+[Guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$entries=@()
foreach($old in Get-ChildItem -LiteralPath $game -File -Filter '*.addon64'){
    if($old.Name -match '^DLSS5-(AI渲染超分版|AI-Rendering-Upscaling)-'){
        $entries+=@{target=(InGame $old.FullName);backup=(InGame (Join-Path $backup $old.Name));original_sha256=(Hash $old.FullName);installed_sha256=$null;source=$null}
    }
}
foreach($file in $files){
    $relative='core-preview/'+$file.Name
    $expected=$manifest.files.PSObject.Properties[$relative].Value
    if(-not $expected -or (Hash $file.FullName) -ne $expected){throw "Package hash mismatch: $relative"}
    $target=InGame (Join-Path $game $file.Name)
    $entry=$entries | Where-Object {$_.target -eq $target} | Select-Object -First 1
    if(-not $entry){$entry=@{target=$target;backup=$null;original_sha256=$null;installed_sha256=$null;source=$null};if(Test-Path -LiteralPath $target){$entry.backup=InGame (Join-Path $backup $file.Name);$entry.original_sha256=Hash $target};$entries+=$entry}
    $entry.installed_sha256=$expected;$entry.source=$file.FullName
}
$receipt=@{schema=1;game_directory=$game;status='in-progress';source_commit=$manifest.source_commit;entries=$entries}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ledger -Encoding UTF8
foreach($entry in $entries){
    if($entry.backup){Move-Item -LiteralPath (InGame $entry.target) -Destination (InGame $entry.backup)}
    if($entry.source){Copy-Item -LiteralPath $entry.source -Destination (InGame $entry.target);if((Hash $entry.target) -ne $entry.installed_sha256){throw 'Installed file hash mismatch; preserve the receipt for restore.'}}
}
$receipt.status='installed'
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ledger -Encoding UTF8
Write-Host 'Installed the source preview. Auto remains unqualified and uses original FP8. No game was launched.'

param([string]$AuditDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resourceRoot = Join-Path $projectRoot 'resources/fg-mfgunlock'
$manifest = Get-Content -LiteralPath (Join-Path $resourceRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($role in @('addon', 'license', 'readme')) {
  $row = $manifest.files.$role
  $target = Join-Path $resourceRoot $row.file
  if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ieq $row.sha256) { continue }
  $temp = Join-Path $resourceRoot ([guid]::NewGuid().ToString() + '.download')
  try {
    if ($AuditDirectory) {
      $localName = @{ addon = 'renodx-mfgunlock-0.6.1.addon64'; license = 'LICENSE-0.6.1.txt'; readme = 'README-0.6.1.md' }[$role]
      Copy-Item -LiteralPath (Join-Path $AuditDirectory $localName) -Destination $temp
    } else {
      Invoke-WebRequest -Uri $row.url -OutFile $temp
    }
    if ((Get-FileHash -LiteralPath $temp -Algorithm SHA256).Hash -ine $row.sha256) { throw "MFG Unlock source hash mismatch: $role" }
    Move-Item -LiteralPath $temp -Destination $target -Force
  } finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp }
  }
}
node (Join-Path $PSScriptRoot 'verify-fg-mfgunlock.js')
if ($LASTEXITCODE -ne 0) { throw 'MFG Unlock resource verification failed' }

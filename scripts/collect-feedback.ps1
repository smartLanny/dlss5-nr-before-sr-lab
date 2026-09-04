param(
    [switch]$NoPause,
    [string]$SourceDirectory = $PSScriptRoot,
    [string]$OutputRoot = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'DLSS5-Feedback')
)

# Windows PowerShell 5.1 compatible. Local files only; no process launch,
# elevation, configuration changes, network upload, or memory-dump collection.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression

$LogLimit = 8MB
$ConfigLimit = 1MB
$BinaryLimit = 32
$HashLimit = 1GB
$DirectoryEntryLimit = 256
$Buffer = New-Object byte[] 65536
$Records = New-Object 'System.Collections.Generic.List[object]'
$Directories = New-Object 'System.Collections.Generic.List[object]'
$Binaries = New-Object 'System.Collections.Generic.List[object]'
$SeenDirectories = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$SeenFiles = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$CollectedNames = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$HashBytes = [long]0
$BinaryCount = 0
$CollectedFiles = 0
$Partial = $false
$Archive = $null
$ZipFile = $null

function Add-Record([string]$Path, [string]$Kind, [string]$Status, [string]$Detail = '') {
    $Records.Add([pscustomobject]@{ path = $Path; kind = $Kind; status = $Status; detail = $Detail })
}

function Add-JsonEntry([string]$Name, $Value) {
    $entry = $Archive.CreateEntry($Name, [IO.Compression.CompressionLevel]::Optimal)
    $stream = $entry.Open()
    try {
        $json = ConvertTo-Json -InputObject $Value -Depth 12
        $bytes = [Text.Encoding]::UTF8.GetBytes($json)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}

function Is-Reparse([string]$Path) {
    return ([IO.File]::GetAttributes($Path) -band [IO.FileAttributes]::ReparsePoint) -ne 0
}

function Add-Directory([string]$Base, [string]$Relative) {
    if ([string]::IsNullOrEmpty($Base)) { return }
    $path = $Base
    $blocked = $false
    try {
        if ([IO.Directory]::Exists($path) -and (Is-Reparse $path)) { $blocked = $true }
        foreach ($part in ($Relative -split '\\' | Where-Object { $_ })) {
            $path = Join-Path $path $part
            if ([IO.Directory]::Exists($path) -and (Is-Reparse $path)) { $blocked = $true }
        }
        $path = [IO.Path]::GetFullPath($path)
        if (-not $SeenDirectories.Add($path)) { return }
        $status = if ($blocked) { 'skipped-reparse-point' } elseif ([IO.Directory]::Exists($path)) { 'available' } else { 'missing' }
        $Directories.Add([pscustomobject]@{ id = ('dir{0:d2}' -f $Directories.Count); path = $path; status = $status })
    } catch {
        Add-Record $path 'directory' 'unreadable' $_.Exception.Message
        $script:Partial = $true
    }
}

function Add-BoundedFile([string]$Path, [string]$EntryName, [string]$Kind, [long]$Limit) {
    if (-not [IO.File]::Exists($Path)) { Add-Record $Path $Kind 'missing'; return }
    if (-not $SeenFiles.Add($Path)) { return }
    $inputStream = $null
    $outputStream = $null
    try {
        if (Is-Reparse $Path) { Add-Record $Path $Kind 'skipped-reparse-point'; $script:Partial = $true; return }
        $inputStream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $originalBytes = $inputStream.Length
        $wanted = [Math]::Min($originalBytes, $Limit)
        $start = if ($Kind -eq 'log') { $originalBytes - $wanted } else { [long]0 }
        [void]$inputStream.Seek($start, [IO.SeekOrigin]::Begin)
        $entry = $Archive.CreateEntry($EntryName, [IO.Compression.CompressionLevel]::Optimal)
        $outputStream = $entry.Open()
        $remaining = $wanted
        while ($remaining -gt 0) {
            $read = $inputStream.Read($Buffer, 0, [int][Math]::Min($Buffer.Length, $remaining))
            if ($read -eq 0) { break }
            $outputStream.Write($Buffer, 0, $read)
            $remaining -= $read
        }
        $status = if ($remaining -ne 0) { 'partial-source-short-read' } elseif ($originalBytes -gt $Limit) {
            if ($Kind -eq 'log') { 'capped-tail' } else { 'capped-prefix' }
        } else { 'collected' }
        if ($status -ne 'collected') { $script:Partial = $true }
        $Records.Add([pscustomobject]@{ path = $Path; kind = $Kind; status = $status;
            entry = $EntryName; source_bytes = $originalBytes; offset = $start;
            copied_bytes = $wanted - $remaining; limit_bytes = $Limit })
        $script:CollectedFiles++
        [void]$CollectedNames.Add([IO.Path]::GetFileName($Path))
    } catch {
        Add-Record $Path $Kind 'unreadable-or-partial-entry' $_.Exception.Message
        $script:Partial = $true
    } finally {
        if ($outputStream) { $outputStream.Dispose() }
        if ($inputStream) { $inputStream.Dispose() }
    }
}

function Add-BinaryMetadata([string]$Path) {
    $stream = $null
    $sha = $null
    try {
        if (Is-Reparse $Path) { Add-Record $Path 'binary' 'skipped-reparse-point'; return }
        $info = New-Object IO.FileInfo $Path
        $length = $info.Length
        $lastWrite = $info.LastWriteTimeUtc.Ticks
        if ($length -gt ($HashLimit - $script:HashBytes)) {
            Add-Record $Path 'binary' 'skipped-total-hash-byte-cap'
            $script:Partial = $true
            return
        }
        $script:BinaryCount++
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        # Bound the hash even if another process grows the file while reading it.
        $remaining = $length
        $sha = [Security.Cryptography.SHA256]::Create()
        while ($remaining -gt 0) {
            $read = $stream.Read($Buffer, 0, [int][Math]::Min($Buffer.Length, $remaining))
            if ($read -eq 0) { break }
            [void]$sha.TransformBlock($Buffer, 0, $read, $Buffer, 0)
            $remaining -= $read
            $script:HashBytes += $read
        }
        [void]$sha.TransformFinalBlock($Buffer, 0, 0)
        $info.Refresh()
        $stable = $remaining -eq 0 -and $stream.Length -eq $length -and
            $info.Length -eq $length -and $info.LastWriteTimeUtc.Ticks -eq $lastWrite
        $hash = if ($stable) { [BitConverter]::ToString($sha.Hash).Replace('-', '') } else { $null }
        $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
        $Binaries.Add([pscustomobject]@{ path = $Path; bytes = $length;
            status = $(if ($stable) { 'hashed' } else { 'changed-during-read' });
            sha256 = $hash; file_version = $version.FileVersion; product_version = $version.ProductVersion })
        if (-not $stable) { $script:Partial = $true }
    } catch {
        Add-Record $Path 'binary' 'metadata-unavailable' $_.Exception.Message
        $script:Partial = $true
    } finally {
        if ($sha) { $sha.Dispose() }
        if ($stream) { $stream.Dispose() }
    }
}

try {
    $source = [IO.Path]::GetFullPath($SourceDirectory)
    $parentInfo = [IO.Directory]::GetParent($source.TrimEnd('\', '/'))
    $parent = if ($parentInfo) { $parentInfo.FullName } else { '' }
    # This is a fixed allow-list, never a recursive filesystem search.
    $relativeDirectories = @('', '_storage_', 'plugins', 'addons', 'reshade-addons',
        '_storage_\plugins', '_storage_\addons', '_storage_\reshade-addons')
    foreach ($base in @($source, $parent)) {
        foreach ($relative in $relativeDirectories) { Add-Directory $base $relative }
    }
    $destinationRoot = [IO.Path]::GetFullPath($OutputRoot)
    $destination = Join-Path $destinationRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    [void][IO.Directory]::CreateDirectory($destination)
    $zipPath = Join-Path $destination 'feedback-offline.zip'
    $ZipFile = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $Archive = New-Object IO.Compression.ZipArchive($ZipFile, [IO.Compression.ZipArchiveMode]::Create, $true)
    $logNames = @('nr-before-sr.log', 'nr-before-sr.previous.log', 'ReShade.log')
    $configNames = @('nr_before_sr.ini', 'ReShade.ini')
    foreach ($directory in $Directories) {
        if ($directory.status -ne 'available') { continue }
        foreach ($name in $logNames) {
            Add-BoundedFile (Join-Path $directory.path $name) ('files/' + $directory.id + '/' + $name) 'log' $LogLimit
        }
        foreach ($name in $configNames) {
            Add-BoundedFile (Join-Path $directory.path $name) ('files/' + $directory.id + '/' + $name) 'config' $ConfigLimit
        }
        $enumerator = $null
        try {
            $enumerator = [IO.Directory]::EnumerateFiles($directory.path).GetEnumerator()
            $examined = 0
            while ($enumerator.MoveNext()) {
                if (++$examined -gt $DirectoryEntryLimit) {
                    Add-Record $directory.path 'directory' 'capped-file-enumeration'
                    $Partial = $true
                    break
                }
                $file = [string]$enumerator.Current
                $extension = [IO.Path]::GetExtension($file)
                if ($extension -ine '.dll' -and $extension -ine '.addon64') { continue }
                if ($BinaryCount -ge $BinaryLimit) {
                    Add-Record $directory.path 'binary' 'capped-binary-count'
                    $Partial = $true
                    break
                }
                Add-BinaryMetadata $file
            }
        } catch {
            Add-Record $directory.path 'directory' 'enumeration-unavailable' $_.Exception.Message
            $Partial = $true
        } finally { if ($enumerator -is [IDisposable]) { $enumerator.Dispose() } }
    }
    if ($CollectedFiles -eq 0) { $Partial = $true; Add-Record $source 'collection' 'no-logs-or-configs-found' }
    foreach ($name in ($logNames + $configNames)) {
        if (-not $CollectedNames.Contains($name)) {
            $Partial = $true
            Add-Record $name 'collection' 'missing-in-all-allowed-directories'
        }
    }
    if ($BinaryCount -eq 0) { Add-Record $source 'binary' 'none-found' }
    $system = [ordered]@{ os_version = [Environment]::OSVersion.VersionString;
        process_64_bit = [Environment]::Is64BitProcess; powershell_version = $PSVersionTable.PSVersion.ToString();
        gpu_status = 'unavailable'; gpus = @() }
    try {
        $controllers = @(Get-CimInstance -ClassName Win32_VideoController -Property Name, DriverVersion, DriverDate, PNPDeviceID -OperationTimeoutSec 3 | Select-Object -First 8)
        $system.gpus = @($controllers | ForEach-Object { [pscustomobject]@{
            name = $_.Name; driver_version = $_.DriverVersion; driver_date = [string]$_.DriverDate; device_id = $_.PNPDeviceID } })
        $system.gpu_status = if ($controllers.Count) { 'collected' } else { 'none-reported' }
    } catch { $system.gpu_error = $_.Exception.Message; $Partial = $true }
    Add-JsonEntry 'system.json' $system
    Add-JsonEntry 'binaries.json' @($Binaries.ToArray())
    $manifest = [ordered]@{ schema = 1; collector_version = 'beta0.3.7-r2';
        created_utc = [DateTime]::UtcNow.ToString('o'); source_directory = $source;
        status = $(if ($Partial) { 'partial' } else { 'complete-with-listed-missing-files' });
        upload_performed = $false; configuration_modified = $false; dumps_collected = $false;
        binary_contents_included = $false; hash_bytes = $HashBytes; binary_count = $BinaryCount;
        limits = @{ log_bytes = $LogLimit; config_bytes = $ConfigLimit; binary_count = $BinaryLimit;
            total_hash_bytes = $HashLimit; directory_entries = $DirectoryEntryLimit; subdirectory_depth = 2 };
        directories = @($Directories.ToArray()); files = @($Records.ToArray()) }
    Add-JsonEntry 'manifest.json' $manifest
    $Archive.Dispose(); $Archive = $null
    $ZipFile.Dispose(); $ZipFile = $null
    Write-Host 'Local feedback package saved (nothing uploaded):'
    Write-Host $zipPath
    Write-Host 'Missing files and limits are listed in manifest.json. Share the ZIP when reporting the issue.'
    if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }
} catch {
    Write-Host ('Feedback collection could not finish: ' + $_.Exception.Message)
    if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }
    exit 1
} finally {
    if ($Archive) { $Archive.Dispose() }
    if ($ZipFile) { $ZipFile.Dispose() }
}

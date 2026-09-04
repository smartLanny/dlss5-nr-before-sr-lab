param(
    [switch]$NoPause,
    [string]$SourceDirectory = $PSScriptRoot,
    [string]$OutputRoot = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'DLSS5-Feedback'),
    [ValidateRange(1, 168)]
    [int]$LookbackHours = 72,
    [string]$EventLogXmlPath = '',
    [string[]]$WerRoots = @(),
    [string[]]$DumpRoots = @()
)

# Windows PowerShell 5.1 compatible. This explicitly named crash collector only
# copies crash evidence that already exists. It does not enable LocalDumps,
# write the registry, create a dump, launch the game, or upload anything.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression

$CollectorVersion = 'beta0.4.1-r2-crash-evidence-2'
$MaxLogBytes = 8MB
$MaxEventCandidates = 256
$MaxEventMatches = 16
$MaxWerRootEntries = 256
$MaxWerDirectories = 64
$MaxWerDirectoryEntries = 64
$MaxWerCandidates = 256
$MaxWerFiles = 8
$MaxWerBytes = 4MB
$MaxDumpRootEntries = 256
$MaxDumpFiles = 2
$MaxDumpBytes = 192MB
$MaxEvidenceBytes = 256MB
$Buffer = New-Object byte[] 65536
$CrashRecords = New-Object 'System.Collections.Generic.List[object]'
$EventSummaries = New-Object 'System.Collections.Generic.List[object]'
$RootSummaries = New-Object 'System.Collections.Generic.List[object]'
$SeenEvidenceFiles = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$Archive = $null
$ZipFile = $null
$EvidenceBytes = [long]0
$WerMatched = 0
$WerCollected = 0
$DumpMatched = 0
$DumpCollected = 0
$CollectionPartial = $false
$TemporaryRoot = $null

function Add-CrashRecord([string]$Path, [string]$Kind, [string]$Status,
        [string]$Detail = '', [string]$Entry = '', [long]$Bytes = 0, [string]$Sha256 = '') {
    $CrashRecords.Add([pscustomobject]@{
        path = $Path; kind = $Kind; status = $Status; detail = $Detail
        entry = $Entry; bytes = $Bytes; sha256 = $Sha256
    }) | Out-Null
}

function Add-JsonEntry([string]$Name, $Value) {
    $entry = $script:Archive.CreateEntry($Name, [IO.Compression.CompressionLevel]::Optimal)
    $stream = $entry.Open()
    try {
        $json = ConvertTo-Json -InputObject $Value -Depth 16
        $bytes = [Text.Encoding]::UTF8.GetBytes($json)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}

function Add-TextEntry([string]$Name, [string]$Value) {
    $entry = $script:Archive.CreateEntry($Name, [IO.Compression.CompressionLevel]::Optimal)
    $stream = $entry.Open()
    try {
        $bytes = [Text.UTF8Encoding]::new($true).GetBytes($Value)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}

function Is-Reparse([string]$Path) {
    try {
        return ([IO.File]::GetAttributes($Path) -band [IO.FileAttributes]::ReparsePoint) -ne 0
    } catch { return $true }
}

function Read-BoundedText([string]$Path, [long]$Limit, [switch]$Tail) {
    $stream = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $wanted = [Math]::Min($stream.Length, $Limit)
        if ($Tail -and $stream.Length -gt $wanted) {
            [void]$stream.Seek($stream.Length - $wanted, [IO.SeekOrigin]::Begin)
        }
        $bytes = New-Object byte[] ([int]$wanted)
        $offset = 0
        while ($offset -lt $wanted) {
            $read = $stream.Read($bytes, $offset, [int]($wanted - $offset))
            if ($read -eq 0) { break }
            $offset += $read
        }
        if ($offset -ne $bytes.Length) { [Array]::Resize([ref]$bytes, $offset) }
        # ReShade logs are UTF-8/ASCII. WER text commonly carries a UTF BOM;
        # StreamReader's BOM detection is used by Read-WerKeyValues below.
        return [Text.Encoding]::UTF8.GetString($bytes)
    } finally { if ($stream) { $stream.Dispose() } }
}

function Get-AllowListedLogDirectories([string]$Source) {
    $result = New-Object 'System.Collections.Generic.List[string]'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $parentInfo = [IO.Directory]::GetParent($Source.TrimEnd('\', '/'))
    $parent = if ($parentInfo) { $parentInfo.FullName } else { '' }
    $relativeDirectories = @('', '_storage_', 'plugins', 'addons', 'reshade-addons',
        '_storage_\plugins', '_storage_\addons', '_storage_\reshade-addons')
    foreach ($base in @($Source, $parent)) {
        if ([string]::IsNullOrWhiteSpace($base)) { continue }
        foreach ($relative in $relativeDirectories) {
            try {
                $path = if ($relative) { Join-Path $base $relative } else { $base }
                $path = [IO.Path]::GetFullPath($path)
                if (-not $seen.Add($path) -or -not [IO.Directory]::Exists($path) -or (Is-Reparse $path)) { continue }
                $result.Add($path) | Out-Null
            } catch { }
        }
    }
    return @($result.ToArray())
}

function Get-NormalizedExecutablePath([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return '' }
    try {
        $candidate = [Environment]::ExpandEnvironmentVariables($Value.Trim())
        if ($candidate.Length -ge 2 -and $candidate[0] -eq '"' -and
            $candidate[$candidate.Length - 1] -eq '"') {
            $candidate = $candidate.Substring(1, $candidate.Length - 2)
        }
        if (-not [IO.Path]::IsPathRooted($candidate) -or
            [IO.Path]::GetExtension($candidate) -ine '.exe') { return '' }
        return [IO.Path]::GetFullPath($candidate)
    } catch { return '' }
}

function Find-ReShadeTarget([string]$Source) {
    $logs = New-Object 'System.Collections.Generic.List[object]'
    foreach ($directory in (Get-AllowListedLogDirectories $Source)) {
        $path = Join-Path $directory 'ReShade.log'
        try {
            if (-not [IO.File]::Exists($path) -or (Is-Reparse $path)) { continue }
            $info = New-Object IO.FileInfo $path
            $logs.Add([pscustomobject]@{ path = $info.FullName; write_utc = $info.LastWriteTimeUtc }) | Out-Null
        } catch { }
    }
    if ($logs.Count -eq 0) {
        return [pscustomobject]@{ status = 'unavailable'; executable = ''; path = ''; source_log = ''; candidates = @() }
    }
    $newest = @($logs.ToArray() | Sort-Object write_utc -Descending)[0]
    try { $text = Read-BoundedText $newest.path $MaxLogBytes -Tail } catch {
        return [pscustomobject]@{ status = 'unreadable'; executable = ''; path = ''; source_log = $newest.path; candidates = @() }
    }
    $pattern = "(?im)^\d{2}:\d{2}:\d{2}:\d{3}\s+\[\s*\d+\]\s+\|\s+INFO\s+\|\s+Initializing\b[^\r\n]*\sinto\s+'(?<path>(?:[A-Za-z]:\\|\\\\)[^\r\n]+\.exe)'\s+\(0x[0-9A-F]+\)\s+\.\.\.\s*$"
    $paths = New-Object 'System.Collections.Generic.List[string]'
    $pathSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($match in [regex]::Matches($text, $pattern)) {
        $candidate = Get-NormalizedExecutablePath $match.Groups['path'].Value
        if ($candidate -and $pathSet.Add($candidate)) { $paths.Add($candidate) | Out-Null }
    }
    if ($paths.Count -eq 0) {
        return [pscustomobject]@{ status = 'unavailable'; executable = ''; path = ''; source_log = $newest.path; candidates = @() }
    }
    if ($paths.Count -ne 1) {
        return [pscustomobject]@{ status = 'ambiguous'; executable = ''; path = ''; source_log = $newest.path; candidates = @($paths.ToArray()) }
    }
    $selectedPath = $paths[$paths.Count - 1]
    return [pscustomobject]@{ status = 'identified'; executable = [IO.Path]::GetFileName($selectedPath);
        path = $selectedPath; source_log = $newest.path; candidates = @($paths.ToArray()) }
}

function Get-EventDataMap($EventNode) {
    $map = [ordered]@{}
    $index = 0
    foreach ($node in @($EventNode.SelectNodes("*[local-name()='EventData']/*[local-name()='Data']"))) {
        $name = [string]$node.GetAttribute('Name')
        if ([string]::IsNullOrWhiteSpace($name)) { $name = 'Data' + $index }
        if (-not $map.Contains($name)) { $map[$name] = [string]$node.InnerText }
        $index++
    }
    return $map
}

function Get-FileNameSafe([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return '' }
    try { return [IO.Path]::GetFileName($Value.Trim()) } catch { return '' }
}

function Get-EventTargetMatch([int]$Id, $Data, [string]$Target, [string]$TargetPath) {
    if ($Data.Contains('AppPath') -and -not [string]::IsNullOrWhiteSpace([string]$Data['AppPath'])) {
        $candidatePath = Get-NormalizedExecutablePath ([string]$Data['AppPath'])
        if ($candidatePath -and [string]::Equals(
                $candidatePath, $TargetPath, [StringComparison]::OrdinalIgnoreCase)) {
            return 'path-exact'
        }
        return ''
    }
    $values = New-Object 'System.Collections.Generic.List[string]'
    foreach ($key in @('AppName')) {
        if ($Data.Contains($key)) { $values.Add([string]$Data[$key]) | Out-Null }
    }
    if ($Id -eq 1001 -and $Data.Contains('P1')) {
        $values.Add([string]$Data['P1']) | Out-Null
    }
    $hasPath = $false
    foreach ($value in $values) {
        $candidatePath = Get-NormalizedExecutablePath $value
        if (-not $candidatePath) { continue }
        if (-not [string]::Equals($candidatePath, $TargetPath,
                [StringComparison]::OrdinalIgnoreCase)) { return '' }
        $hasPath = $true
    }
    if ($hasPath) { return 'path-exact' }
    foreach ($value in $values) {
        $candidate = Get-FileNameSafe $value
        if ([string]::Equals($candidate, $Target, [StringComparison]::OrdinalIgnoreCase)) { return 'name-only' }
    }
    return ''
}

function Convert-EventNode($EventNode, [string]$Target, [string]$TargetPath,
        [datetime]$CutoffUtc, [datetime]$ReferenceUtc) {
    try {
        $system = $EventNode.SelectSingleNode("*[local-name()='System']")
        if (-not $system) { return $null }
        $providerNode = $system.SelectSingleNode("*[local-name()='Provider']")
        $idNode = $system.SelectSingleNode("*[local-name()='EventID']")
        $timeNode = $system.SelectSingleNode("*[local-name()='TimeCreated']")
        if (-not $providerNode -or -not $idNode -or -not $timeNode) { return $null }
        $provider = [string]$providerNode.GetAttribute('Name')
        $id = [int]$idNode.InnerText
        $providerOk = ($id -eq 1000 -and $provider -ceq 'Application Error') -or
            ($id -eq 1001 -and $provider -ceq 'Windows Error Reporting')
        if (-not $providerOk) { return $null }
        $time = [DateTimeOffset]::Parse([string]$timeNode.GetAttribute('SystemTime'),
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeUniversal).UtcDateTime
        if ($time -lt $CutoffUtc -or $time -gt $ReferenceUtc.AddMinutes(5)) { return $null }
        $data = Get-EventDataMap $EventNode
        $targetMatch = Get-EventTargetMatch $id $data $Target $TargetPath
        if (-not $targetMatch) { return $null }
        $selected = [ordered]@{}
        $keys = if ($id -eq 1000) {
            @('AppName', 'AppVersion', 'AppTimeStamp', 'ModuleName', 'ModuleVersion',
                'ModuleTimeStamp', 'ExceptionCode', 'FaultingOffset', 'ProcessId',
                'ProcessCreationTime', 'AppPath', 'ModulePath', 'IntegratorReportId')
        } else {
            @('EventName', 'Response', 'CabId', 'P1', 'P2', 'P3', 'P4', 'P5',
                'P6', 'P7', 'P8', 'P9', 'P10', 'ReportId', 'ReportStatus',
                'HashedBucket', 'CabGuid')
        }
        foreach ($key in $keys) {
            if (-not $data.Contains($key)) { continue }
            $value = [string]$data[$key]
            if ($value.Length -gt 4096) { $value = $value.Substring(0, 4096) }
            $selected[$key] = $value
        }
        $recordNode = $system.SelectSingleNode("*[local-name()='EventRecordID']")
        return [pscustomobject]@{ provider = $provider; event_id = $id;
            record_id = $(if ($recordNode) { [string]$recordNode.InnerText } else { '' });
            time_utc = $time.ToString('o'); target_match = $targetMatch; data = $selected }
    } catch { return $null }
}

function Collect-EventSummaries([string]$Target, [string]$TargetPath,
        [datetime]$CutoffUtc, [datetime]$ReferenceUtc) {
    $nodes = New-Object 'System.Collections.Generic.List[object]'
    $status = 'unavailable'
    try {
        if (-not [string]::IsNullOrWhiteSpace($EventLogXmlPath)) {
            $eventPath = [IO.Path]::GetFullPath($EventLogXmlPath)
            if (-not [IO.File]::Exists($eventPath) -or (Is-Reparse $eventPath)) {
                Add-CrashRecord $eventPath 'event-log' 'missing-or-reparse'
                $script:CollectionPartial = $true
                return 'fixture-unavailable'
            }
            if ((New-Object IO.FileInfo $eventPath).Length -gt 8MB) {
                Add-CrashRecord $eventPath 'event-log' 'skipped-byte-cap' '8 MiB XML cap'
                $script:CollectionPartial = $true
                return 'fixture-byte-cap'
            }
            [xml]$document = [IO.File]::ReadAllText($eventPath)
            if ($document.DocumentElement.LocalName -eq 'Event') {
                $nodes.Add($document.DocumentElement) | Out-Null
            } else {
                foreach ($node in @($document.SelectNodes("//*[local-name()='Event']"))) {
                    if ($nodes.Count -ge $MaxEventCandidates) { $script:CollectionPartial = $true; break }
                    $nodes.Add($node) | Out-Null
                }
            }
            $status = 'xml-read'
        } else {
            $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = @(1000, 1001);
                    StartTime = $CutoffUtc } -MaxEvents $MaxEventCandidates -ErrorAction Stop)
            foreach ($event in $events) {
                try {
                    [xml]$document = $event.ToXml()
                    $nodes.Add($document.DocumentElement) | Out-Null
                } catch { }
            }
            $status = 'windows-application-log-read'
        }
        foreach ($node in $nodes) {
            $summary = Convert-EventNode $node $Target $TargetPath $CutoffUtc $ReferenceUtc
            if (-not $summary) { continue }
            $EventSummaries.Add($summary) | Out-Null
            if ($EventSummaries.Count -ge $MaxEventMatches) {
                $script:CollectionPartial = $true
                Add-CrashRecord 'Windows Application log' 'event-log' 'capped-match-count' ([string]$MaxEventMatches)
                break
            }
        }
    } catch {
        Add-CrashRecord 'Windows Application log' 'event-log' 'unavailable' $_.Exception.Message
        $script:CollectionPartial = $true
        $status = 'unavailable'
    }
    return $status
}

function Read-WerKeyValues([string]$Path) {
    $stream = $null
    $reader = $null
    try {
        $info = New-Object IO.FileInfo $Path
        if ($info.Length -gt $MaxWerBytes) { return $null }
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8, $true, 4096, $true)
        $text = $reader.ReadToEnd()
        $values = [ordered]@{}
        foreach ($line in ($text -split "`r?`n")) {
            if ($line -notmatch '^(?<key>[^=]{1,128})=(?<value>.*)$') { continue }
            if (-not $values.Contains($Matches['key'])) { $values[$Matches['key']] = $Matches['value'] }
        }
        return $values
    } catch { return $null } finally {
        if ($reader) { $reader.Dispose() }
        if ($stream) { $stream.Dispose() }
    }
}

function Get-WerTargetMatch($Values, [string]$Target, [string]$TargetPath) {
    if ($Values.Contains('AppPath') -and -not [string]::IsNullOrWhiteSpace([string]$Values['AppPath'])) {
        $candidatePath = Get-NormalizedExecutablePath ([string]$Values['AppPath'])
        if ($candidatePath -and [string]::Equals(
                $candidatePath, $TargetPath, [StringComparison]::OrdinalIgnoreCase)) {
            return 'path-exact'
        }
        return ''
    }
    $candidates = New-Object 'System.Collections.Generic.List[string]'
    foreach ($key in @('AppName', 'Sig[0].Value')) {
        if ($Values.Contains($key)) { $candidates.Add([string]$Values[$key]) | Out-Null }
    }
    $hasPath = $false
    foreach ($value in $candidates) {
        $candidatePath = Get-NormalizedExecutablePath $value
        if (-not $candidatePath) { continue }
        if (-not [string]::Equals($candidatePath, $TargetPath,
                [StringComparison]::OrdinalIgnoreCase)) { return '' }
        $hasPath = $true
    }
    if ($hasPath) { return 'path-exact' }
    foreach ($value in $candidates) {
        $candidate = Get-FileNameSafe $value
        if ([string]::Equals($candidate, $Target, [StringComparison]::OrdinalIgnoreCase)) { return 'name-only' }
    }
    return ''
}

function Add-EvidenceFile([string]$Path, [string]$EntryName, [string]$Kind, [long]$IndividualLimit) {
    $full = ''
    try { $full = [IO.Path]::GetFullPath($Path) } catch {
        Add-CrashRecord $Path $Kind 'invalid-path'; $script:CollectionPartial = $true; return $false
    }
    if (-not $SeenEvidenceFiles.Add($full)) { return $false }
    if (-not [IO.File]::Exists($full)) { Add-CrashRecord $full $Kind 'missing'; return $false }
    if (Is-Reparse $full) { Add-CrashRecord $full $Kind 'skipped-reparse-point'; $script:CollectionPartial = $true; return $false }
    $input = $null
    $output = $null
    $sha = $null
    $entry = $null
    try {
        $info = New-Object IO.FileInfo $full
        $length = $info.Length
        $writeTicks = $info.LastWriteTimeUtc.Ticks
        if ($length -gt $IndividualLimit) {
            Add-CrashRecord $full $Kind 'skipped-individual-byte-cap' ([string]$IndividualLimit)
            $script:CollectionPartial = $true
            return $false
        }
        if ($length -gt ($MaxEvidenceBytes - $script:EvidenceBytes)) {
            Add-CrashRecord $full $Kind 'skipped-total-byte-cap' ([string]$MaxEvidenceBytes)
            $script:CollectionPartial = $true
            return $false
        }
        $input = [IO.File]::Open($full, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $entry = $script:Archive.CreateEntry($EntryName, [IO.Compression.CompressionLevel]::Optimal)
        $output = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        $remaining = $length
        while ($remaining -gt 0) {
            $read = $input.Read($Buffer, 0, [int][Math]::Min($Buffer.Length, $remaining))
            if ($read -eq 0) { break }
            $output.Write($Buffer, 0, $read)
            [void]$sha.TransformBlock($Buffer, 0, $read, $Buffer, 0)
            $remaining -= $read
        }
        [void]$sha.TransformFinalBlock((New-Object byte[] 0), 0, 0)
        $output.Dispose(); $output = $null
        $info.Refresh()
        $stable = $remaining -eq 0 -and $input.Length -eq $length -and
            $info.Length -eq $length -and $info.LastWriteTimeUtc.Ticks -eq $writeTicks
        if (-not $stable) {
            $entry.Delete()
            Add-CrashRecord $full $Kind 'changed-during-copy'
            $script:CollectionPartial = $true
            return $false
        }
        $hash = [BitConverter]::ToString($sha.Hash).Replace('-', '')
        $script:EvidenceBytes += $length
        Add-CrashRecord $full $Kind 'collected' '' $EntryName $length $hash
        return $true
    } catch {
        if ($output) { $output.Dispose(); $output = $null }
        if ($entry) { try { $entry.Delete() } catch { } }
        Add-CrashRecord $full $Kind 'unreadable' $_.Exception.Message
        $script:CollectionPartial = $true
        return $false
    } finally {
        if ($sha) { $sha.Dispose() }
        if ($output) { $output.Dispose() }
        if ($input) { $input.Dispose() }
    }
}

function Test-RecentFile([IO.FileInfo]$Info, [datetime]$CutoffUtc, [datetime]$ReferenceUtc) {
    return $Info.LastWriteTimeUtc -ge $CutoffUtc -and $Info.LastWriteTimeUtc -le $ReferenceUtc.AddMinutes(5)
}

function Get-BoundedImmediateFiles([string]$Directory, [int]$Limit, [string]$Kind) {
    $files = New-Object 'System.Collections.Generic.List[IO.FileInfo]'
    $enumerator = $null
    try {
        $enumerator = [IO.Directory]::EnumerateFiles($Directory).GetEnumerator()
        $examined = 0
        while ($enumerator.MoveNext()) {
            if (++$examined -gt $Limit) {
                Add-CrashRecord $Directory $Kind 'capped-directory-entries' ([string]$Limit)
                $script:CollectionPartial = $true
                break
            }
            try { $files.Add((New-Object IO.FileInfo ([string]$enumerator.Current))) | Out-Null } catch { }
        }
    } catch {
        Add-CrashRecord $Directory $Kind 'enumeration-unavailable' $_.Exception.Message
        $script:CollectionPartial = $true
    } finally { if ($enumerator -is [IDisposable]) { $enumerator.Dispose() } }
    return @($files.ToArray() | Sort-Object LastWriteTimeUtc -Descending)
}

function Collect-DumpsFromDirectory([string]$Directory, [string]$Target, [datetime]$CutoffUtc,
        [datetime]$ReferenceUtc, [switch]$MatchedWerDirectory) {
    if ($script:DumpCollected -ge $MaxDumpFiles) { return }
    foreach ($file in (Get-BoundedImmediateFiles $Directory $MaxDumpRootEntries 'dump-directory')) {
        if ($script:DumpCollected -ge $MaxDumpFiles) {
            Add-CrashRecord $Directory 'dump' 'capped-match-count' ([string]$MaxDumpFiles)
            $script:CollectionPartial = $true
            break
        }
        if ($file.Extension -notin @('.dmp', '.mdmp', '.hdmp')) { continue }
        if (-not (Test-RecentFile $file $CutoffUtc $ReferenceUtc)) { continue }
        if (-not $MatchedWerDirectory) {
            $pattern = '^' + [regex]::Escape($Target) + '(?:\.[0-9]+){0,2}\.(?:dmp|mdmp|hdmp)$'
            if ($file.Name -notmatch $pattern) { continue }
        }
        $script:DumpMatched++
        $entryName = 'crash/dumps/{0:d2}-{1}' -f $script:DumpCollected, $file.Name
        if (Add-EvidenceFile $file.FullName $entryName 'existing-dump' $MaxDumpBytes) {
            $script:DumpCollected++
        }
    }
}

function Collect-WerEvidence([string]$Target, [string]$TargetPath,
        [datetime]$CutoffUtc, [datetime]$ReferenceUtc, [string[]]$Roots) {
    $candidates = New-Object 'System.Collections.Generic.List[IO.FileInfo]'
    $rootDirectories = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $directoryCount = 0
    foreach ($rootValue in $Roots) {
        if ([string]::IsNullOrWhiteSpace($rootValue)) { continue }
        try { $root = [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($rootValue)) } catch {
            $RootSummaries.Add([pscustomobject]@{ kind = 'wer'; path = $rootValue; status = 'invalid-path' }) | Out-Null
            $script:CollectionPartial = $true; continue
        }
        if (-not [IO.Directory]::Exists($root)) {
            $RootSummaries.Add([pscustomobject]@{ kind = 'wer'; path = $root; status = 'missing' }) | Out-Null
            continue
        }
        if (Is-Reparse $root) {
            $RootSummaries.Add([pscustomobject]@{ kind = 'wer'; path = $root; status = 'skipped-reparse-point' }) | Out-Null
            $script:CollectionPartial = $true; continue
        }
        $rootDirectories.Add($root.TrimEnd('\', '/')) | Out-Null
        $RootSummaries.Add([pscustomobject]@{ kind = 'wer'; path = $root; status = 'examined' }) | Out-Null
        foreach ($file in (Get-BoundedImmediateFiles $root $MaxWerRootEntries 'wer-root')) {
            if ($file.Extension -ieq '.wer' -and $candidates.Count -lt $MaxWerCandidates) { $candidates.Add($file) | Out-Null }
        }
        $enumerator = $null
        try {
            $enumerator = [IO.Directory]::EnumerateDirectories($root).GetEnumerator()
            $examined = 0
            while ($enumerator.MoveNext()) {
                if (++$examined -gt $MaxWerRootEntries -or $directoryCount -ge $MaxWerDirectories) {
                    Add-CrashRecord $root 'wer-root' 'capped-directory-count' ([string]$MaxWerDirectories)
                    $script:CollectionPartial = $true
                    break
                }
                $directory = [string]$enumerator.Current
                $directoryCount++
                if (Is-Reparse $directory) {
                    Add-CrashRecord $directory 'wer-directory' 'skipped-reparse-point'
                    $script:CollectionPartial = $true
                    continue
                }
                foreach ($file in (Get-BoundedImmediateFiles $directory $MaxWerDirectoryEntries 'wer-directory')) {
                    if ($file.Extension -ieq '.wer' -and $candidates.Count -lt $MaxWerCandidates) {
                        $candidates.Add($file) | Out-Null
                    }
                }
                if ($candidates.Count -ge $MaxWerCandidates) {
                    Add-CrashRecord $root 'wer-root' 'capped-candidate-count' ([string]$MaxWerCandidates)
                    $script:CollectionPartial = $true
                    break
                }
            }
        } catch {
            Add-CrashRecord $root 'wer-root' 'enumeration-unavailable' $_.Exception.Message
            $script:CollectionPartial = $true
        } finally { if ($enumerator -is [IDisposable]) { $enumerator.Dispose() } }
    }
    foreach ($file in @($candidates.ToArray() | Sort-Object LastWriteTimeUtc -Descending)) {
        if ($script:WerCollected -ge $MaxWerFiles) {
            Add-CrashRecord 'WER reports' 'wer-report' 'capped-match-count' ([string]$MaxWerFiles)
            $script:CollectionPartial = $true
            break
        }
        if (-not (Test-RecentFile $file $CutoffUtc $ReferenceUtc) -or (Is-Reparse $file.FullName)) { continue }
        $values = Read-WerKeyValues $file.FullName
        if (-not $values) {
            if ($file.Length -gt $MaxWerBytes) {
                Add-CrashRecord $file.FullName 'wer-report' 'skipped-individual-byte-cap' ([string]$MaxWerBytes)
                $script:CollectionPartial = $true
            }
            continue
        }
        $targetMatch = Get-WerTargetMatch $values $Target $TargetPath
        if (-not $targetMatch) { continue }
        $script:WerMatched++
        $entryName = 'crash/wer/{0:d2}-{1}' -f $script:WerCollected, $file.Name
        if (Add-EvidenceFile $file.FullName $entryName 'wer-report' $MaxWerBytes) {
            $script:WerCollected++
            $reportDirectory = [IO.Path]::GetFullPath($file.DirectoryName).TrimEnd('\', '/')
            if ($rootDirectories.Contains($reportDirectory)) {
                # A WER root is shared by many applications. A matching report placed
                # directly there must not authorize collection of unrelated sibling dumps.
                Collect-DumpsFromDirectory $file.DirectoryName $Target $CutoffUtc $ReferenceUtc
            } else {
                Collect-DumpsFromDirectory $file.DirectoryName $Target $CutoffUtc $ReferenceUtc -MatchedWerDirectory
            }
        }
    }
}

function Get-DefaultWerRoots {
    $roots = New-Object 'System.Collections.Generic.List[string]'
    foreach ($base in @($env:LOCALAPPDATA, $env:ProgramData)) {
        if ([string]::IsNullOrWhiteSpace($base)) { continue }
        $roots.Add((Join-Path $base 'Microsoft\Windows\WER\ReportArchive')) | Out-Null
        $roots.Add((Join-Path $base 'Microsoft\Windows\WER\ReportQueue')) | Out-Null
    }
    return @($roots.ToArray())
}

function Get-DefaultDumpRoots {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { return @() }
    return @((Join-Path $env:LOCALAPPDATA 'CrashDumps'))
}

function Read-ManifestFromArchive {
    $entry = $script:Archive.GetEntry('manifest.json')
    if (-not $entry) { throw 'Base feedback archive has no manifest.json.' }
    $stream = $entry.Open()
    $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8, $true)
    try { $value = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose(); $stream.Dispose() }
    $entry.Delete()
    return $value
}

try {
    $source = [IO.Path]::GetFullPath($SourceDirectory)
    if (-not [IO.Directory]::Exists($source)) { throw "Source directory does not exist: $source" }
    $destinationRoot = [IO.Path]::GetFullPath($OutputRoot)
    [void][IO.Directory]::CreateDirectory($destinationRoot)
    $destination = Join-Path $destinationRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-crash-' +
        [Guid]::NewGuid().ToString('N').Substring(0, 8))
    [void][IO.Directory]::CreateDirectory($destination)
    $zipPath = Join-Path $destination 'crash-feedback-offline.zip'

    $TemporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('DLSS5-CrashFeedback-' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($TemporaryRoot)
    $baseCollector = Join-Path $PSScriptRoot 'collect-feedback.ps1'
    if (-not [IO.File]::Exists($baseCollector)) { throw 'collect-feedback.ps1 must be beside this script.' }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $baseCollector -NoPause `
        -SourceDirectory $source -OutputRoot $TemporaryRoot | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Base feedback collector failed with exit code $LASTEXITCODE." }
    $basePackages = New-Object 'System.Collections.Generic.List[string]'
    $baseDirectoryEnumerator = [IO.Directory]::EnumerateDirectories($TemporaryRoot).GetEnumerator()
    try {
        $baseDirectoryCount = 0
        while ($baseDirectoryEnumerator.MoveNext()) {
            if (++$baseDirectoryCount -gt 4) { throw 'Base collector produced too many output directories.' }
            $candidatePackage = Join-Path ([string]$baseDirectoryEnumerator.Current) 'feedback-offline.zip'
            if ([IO.File]::Exists($candidatePackage) -and -not (Is-Reparse $candidatePackage)) {
                $basePackages.Add($candidatePackage) | Out-Null
            }
        }
    } finally { if ($baseDirectoryEnumerator -is [IDisposable]) { $baseDirectoryEnumerator.Dispose() } }
    if ($basePackages.Count -ne 1) { throw "Expected one base feedback package, found $($basePackages.Count)." }
    [IO.File]::Copy($basePackages[0], $zipPath, $false)

    $referenceUtc = [DateTime]::UtcNow
    $cutoffUtc = $referenceUtc.AddHours(-$LookbackHours)
    $target = Find-ReShadeTarget $source
    $eventStatus = 'not-queried-without-exact-target'

    $ZipFile = [IO.File]::Open($zipPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
    $Archive = New-Object IO.Compression.ZipArchive($ZipFile, [IO.Compression.ZipArchiveMode]::Update, $true)
    $baseManifest = Read-ManifestFromArchive

    if ($target.status -eq 'identified') {
        $eventStatus = Collect-EventSummaries $target.executable $target.path $cutoffUtc $referenceUtc
        $effectiveWerRoots = if ($WerRoots.Count) { @($WerRoots) } else { @(Get-DefaultWerRoots) }
        $effectiveDumpRoots = if ($DumpRoots.Count) { @($DumpRoots) } else { @(Get-DefaultDumpRoots) }
        Collect-WerEvidence $target.executable $target.path $cutoffUtc $referenceUtc $effectiveWerRoots
        foreach ($rootValue in $effectiveDumpRoots) {
            if ([string]::IsNullOrWhiteSpace($rootValue)) { continue }
            try { $root = [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($rootValue)) } catch {
                $RootSummaries.Add([pscustomobject]@{ kind = 'dump'; path = $rootValue; status = 'invalid-path' }) | Out-Null
                $CollectionPartial = $true; continue
            }
            if (-not [IO.Directory]::Exists($root)) {
                $RootSummaries.Add([pscustomobject]@{ kind = 'dump'; path = $root; status = 'missing' }) | Out-Null
                continue
            }
            if (Is-Reparse $root) {
                $RootSummaries.Add([pscustomobject]@{ kind = 'dump'; path = $root; status = 'skipped-reparse-point' }) | Out-Null
                $CollectionPartial = $true; continue
            }
            $RootSummaries.Add([pscustomobject]@{ kind = 'dump'; path = $root; status = 'examined' }) | Out-Null
            Collect-DumpsFromDirectory $root $target.executable $cutoffUtc $referenceUtc
        }
    }

    Add-JsonEntry 'crash/events.json' @($EventSummaries.ToArray())
    $evidenceCount = $EventSummaries.Count + $WerCollected + $DumpCollected
    $crashStatus = if ($target.status -ne 'identified') {
        'target-' + $target.status
    } elseif ($evidenceCount -eq 0) {
        'no-matching-crash-evidence'
    } else {
        'matching-crash-evidence-collected'
    }
    $crashManifest = [ordered]@{
        schema = 1; collector_version = $CollectorVersion; mode = 'existing-crash-evidence-only'
        status = $crashStatus; collection_partial = $CollectionPartial
        target_status = $target.status; target_executable = $target.executable
        target_path = $target.path; target_source_log = $target.source_log
        target_candidates = @($target.candidates)
        identity_matching = [ordered]@{
            reshade_log = 'one unique normalized absolute EXE path required'
            event_and_wer = 'absolute AppPath must equal target_path; basename fallback only when AppPath is absent'
            standalone_dumps = 'exact basename and recent timestamp only'
            standalone_dump_limitation = 'CrashDumps filenames do not identify the source EXE path; another installation with the same EXE basename cannot be distinguished.'
        }
        reference_utc = $referenceUtc.ToString('o'); cutoff_utc = $cutoffUtc.ToString('o')
        lookback_hours = $LookbackHours; event_query_status = $eventStatus
        matches = @{ event_summaries = $EventSummaries.Count; wer_reports_found = $WerMatched;
            wer_reports_collected = $WerCollected; dumps_found = $DumpMatched; dumps_collected = $DumpCollected }
        limits = @{ event_candidates = $MaxEventCandidates; event_matches = $MaxEventMatches;
            wer_root_entries = $MaxWerRootEntries; wer_directories = $MaxWerDirectories;
            wer_directory_entries = $MaxWerDirectoryEntries; wer_candidates = $MaxWerCandidates;
            wer_files = $MaxWerFiles; wer_file_bytes = $MaxWerBytes;
            dump_root_entries = $MaxDumpRootEntries; dump_files = $MaxDumpFiles;
            dump_file_bytes = $MaxDumpBytes; total_evidence_bytes = $MaxEvidenceBytes;
            directory_depth = 1 }
        evidence_bytes = $EvidenceBytes; roots = @($RootSummaries.ToArray()); files = @($CrashRecords.ToArray())
        registry_modified = $false; dump_creation_performed = $false; upload_performed = $false
    }
    Add-JsonEntry 'crash-evidence.json' $crashManifest
    $targetLabel = if ($target.executable) { $target.executable } else { '未能从最新 ReShade.log 唯一确认' }
    $explanation = (@(
        'DLSS5 崩溃反馈说明', '',
        ('目标程序：' + $targetLabel),
        ('收集范围：最近 ' + $LookbackHours + ' 小时'),
        ('系统异常摘要：' + $EventSummaries.Count),
        ('匹配 WER 报告：' + $WerCollected),
        ('已有转储：' + $DumpCollected),
        ('结果：' + $crashStatus), '',
        '没有匹配材料不代表游戏没有崩溃，只表示 Windows 当前可读取的位置没有留下符合当前目标身份规则的近期记录。',
        '单独的 CrashDumps 文件名不含程序完整路径；若电脑上另有同名 EXE，工具无法仅凭该 dump 区分，限制已写入 crash-evidence.json。',
        '本工具不会打开系统转储、不会修改注册表、不会启动游戏，也不会上传文件。已有转储可能包含进程内存和本机路径；发送 ZIP 前请按自己的隐私要求决定是否分享。',
        '详细范围、缺失原因、文件哈希和上限见 crash-evidence.json 与 manifest.json。'
    ) -join "`r`n") + "`r`n"
    Add-TextEntry '崩溃分析说明.txt' $explanation

    $baseManifest | Add-Member -NotePropertyName package_kind -NotePropertyValue 'crash-feedback-offline' -Force
    $baseManifest | Add-Member -NotePropertyName crash_evidence -NotePropertyValue $crashManifest -Force
    $baseManifest.collector_version = $CollectorVersion
    $baseManifest.dumps_collected = ($DumpCollected -gt 0)
    Add-JsonEntry 'manifest.json' $baseManifest

    $Archive.Dispose(); $Archive = $null
    $ZipFile.Dispose(); $ZipFile = $null
    Write-Host 'Crash feedback package saved locally (nothing uploaded):'
    Write-Host $zipPath
    Write-Host 'It only includes bounded recent evidence under the identity rules recorded in crash-evidence.json.'
    if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }
} catch {
    Write-Host ('Crash feedback collection could not finish: ' + $_.Exception.Message)
    if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }
    exit 1
} finally {
    if ($Archive) { $Archive.Dispose() }
    if ($ZipFile) { $ZipFile.Dispose() }
    if ($TemporaryRoot -and [IO.Directory]::Exists($TemporaryRoot)) {
        try {
            $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
            $resolvedTemporary = [IO.Path]::GetFullPath($TemporaryRoot)
            if ($resolvedTemporary.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
                [IO.Path]::GetFileName($resolvedTemporary).StartsWith('DLSS5-CrashFeedback-', [StringComparison]::Ordinal)) {
                [IO.Directory]::Delete($resolvedTemporary, $true)
            }
        } catch { }
    }
}

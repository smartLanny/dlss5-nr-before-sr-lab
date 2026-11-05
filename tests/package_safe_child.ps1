param([Parameter(Mandatory=$true)][string]$Packager)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Extract the actual production function without running the rest of packaging.
# This gate uses the current host's real System.IO.Path and PowerShell provider.
$Tokens = $null
$Errors = $null
$Ast = [Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path -LiteralPath $Packager).Path, [ref]$Tokens, [ref]$Errors)
if ($Errors.Count) { throw 'Production packager failed PowerShell parsing.' }
$Functions = @($Ast.FindAll({param($Node)
    $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -eq 'Safe-Child'
}, $true))
if ($Functions.Count -ne 1) { throw 'Expected exactly one production Safe-Child.' }
Invoke-Expression $Functions[0].Extent.Text

$Sandbox = Join-Path ([IO.Path]::GetTempPath()) ('safe-child-' + [guid]::NewGuid().ToString('N'))
$Parent = Join-Path $Sandbox 'source'
New-Item -ItemType Directory -Path $Parent -Force | Out-Null
$Checks = 0
try {
    $Native = [IO.Path]::GetFullPath($Parent)
    $Parents = @($Parent, ($Parent + '/'), (Join-Path $Parent '.'),
        (Join-Path $Sandbox 'unused/../source'))
    $Children = @('carrier/LICENSE', 'core/config/nr_before_sr.ini')
    if ([IO.Path]::DirectorySeparatorChar -eq '\') {
        $Parents += @($Parent.Replace('\', '/'), ($Parent.Replace('\', '/') + '/'))
        $Children += 'carrier\LICENSE'
    }
    foreach ($Root in $Parents) {
        foreach ($Child in $Children) {
            $Actual = Safe-Child $Root $Child
            $Expected = [IO.Path]::GetFullPath((Join-Path $Native $Child))
            if ($Actual -ne $Expected) { throw "Canonical child differs: $Root / $Child" }
            ++$Checks
        }
    }
    # Absolute names, either traversal separator, a sibling sharing the root
    # prefix, and the root itself all remain outside the allowed file subtree.
    $Outside = Join-Path $Sandbox 'source-other/file'
    foreach ($Child in @('', '..', '../source-other/file', '..\source-other\file',
        'carrier/../../outside', 'carrier\..\..\outside', 'carrier/../LICENSE', $Outside, '.')) {
        $Rejected = $false
        try { Safe-Child $Parent $Child | Out-Null }
        catch {
            if ($_.Exception.Message -notmatch '^(Unsafe manifest path|Manifest path escaped its source root):') { throw }
            $Rejected = $true
        }
        if (-not $Rejected) { throw "Unsafe child was accepted: $Child" }
        ++$Checks
    }
    Write-Host "PASS production Safe-Child: $Checks native path and rejection checks"
} finally {
    Remove-Item -LiteralPath $Sandbox -Recurse -Force
}

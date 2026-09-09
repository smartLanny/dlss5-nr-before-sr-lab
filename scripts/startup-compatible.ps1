[CmdletBinding()]
param(
  [Parameter(Position = 0)][string]$AppPath,
  [switch]$InspectOnly,
  [switch]$AcceptNoSandbox
)

$ErrorActionPreference = 'Stop'

function Resolve-CompatibleTarget {
  param([string]$Requested, [string]$Directory = $PSScriptRoot)
  if (-not [string]::IsNullOrWhiteSpace($Requested)) {
    $item = Get-Item -LiteralPath $Requested -ErrorAction Stop
    if ($item.PSIsContainer -or $item.Extension -ine '.exe') { throw '请选择管理器 EXE，不能选择文件夹、快捷方式或游戏。' }
    if ($item.Name -match '(?i)setup|uninstall|unins|安装版|卸载') { throw '请选择便携版或已安装的管理器主程序，不能选择 Setup 安装器。' }
    return $item.FullName
  }
  $candidates = @(Get-ChildItem -LiteralPath $Directory -File -Filter '*.exe' | Where-Object {
    ($_.Name -eq 'DLSS 5 AI 超分管理器.exe' -or $_.Name -match '(?i)DLSS5-Manager-.+-portable[.]exe$') -and
    $_.Name -notmatch '(?i)setup|uninstall|unins|安装版|卸载'
  })
  if ($candidates.Count -ne 1) {
    throw '没有找到唯一的管理器。请把要启动的便携版或已安装主程序 EXE 拖到“兼容启动.cmd”上。不要拖 Setup 安装器。'
  }
  return $candidates[0].FullName
}

function Get-CompatibleTargetInfo {
  param([string]$File)
  $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($File)
  if ($version.ProductName -ne 'DLSS 5 AI 超分管理器') {
    throw '所选文件的产品信息不是 DLSS 5 AI 超分管理器，已停止启动。'
  }
  # NSIS Setup and portable wrappers can share a product name. The known
  # portable leaf is required unless this is the unpacked Electron main EXE.
  $name = [IO.Path]::GetFileName($File)
  if ($name -ne 'DLSS 5 AI 超分管理器.exe' -and $name -notmatch '(?i)DLSS5-Manager-.+-portable[.]exe$') {
    throw '请保留发行包的原始便携版文件名，或选择已安装的“DLSS 5 AI 超分管理器.exe”。'
  }
  return [pscustomobject]@{ File = $name; Product = $version.ProductName; Version = $version.FileVersion; Mode = 'temporary-no-sandbox' }
}

function Invoke-CompatibleStartup {
  param([string]$Requested, [switch]$Inspect, [switch]$Accepted)
  $target = Resolve-CompatibleTarget $Requested
  $info = Get-CompatibleTargetInfo $target
  if ($Inspect) { Write-Host ($info | ConvertTo-Json -Compress); return 0 }

  Write-Host ''
  Write-Host 'DLSS 5 AI 超分管理器 · 临时兼容启动' -ForegroundColor Cyan
  Write-Host ('文件：{0}  版本：{1}' -f $info.File, $info.Version)
  Write-Host '仅用于普通启动失败的排障。本次将关闭管理器所有 Chromium 子进程的沙箱，降低进程隔离保护。' -ForegroundColor Yellow
  Write-Host '不会修改游戏、系统策略或快捷方式；直接打开原 EXE 仍使用正常启动。'
  Write-Host '请先关闭已打开的管理器。此入口不会结束任何现有进程。'
  if (-not $Accepted) {
    $answer = Read-Host '输入 Y 同意本次兼容启动，其他输入取消'
    if ($answer -notmatch '^[Yy]$') { Write-Host '已取消，未启动程序。'; return 2 }
  }

  # Pass only the fixed opt-in switch. Never copy environment flags, launch a
  # shell command built from the EXE path, persist flags, or retry automatically.
  $launched = Start-Process -FilePath $target -ArgumentList '--no-sandbox' -WorkingDirectory ([IO.Path]::GetDirectoryName($target)) -WindowStyle Hidden -PassThru
  Write-Host ('已发起兼容启动（启动进程 {0}）。这不代表界面已经加载成功。' -f $launched.Id)
  Write-Host '请确认管理器窗口正常出现；若仍打不开，运行“启动诊断.cmd”并提供本次报告。'
  return 0
}

# Dot-sourcing exposes the same target validation and launch functions to the
# focused tests without launching a program or changing the machine.
if ($MyInvocation.InvocationName -ne '.') {
  try { exit (Invoke-CompatibleStartup -Requested $AppPath -Inspect:$InspectOnly -Accepted:$AcceptNoSandbox) }
  catch {
    Write-Host ('无法发起兼容启动：{0}' -f $_.Exception.Message) -ForegroundColor Red
    exit 1
  }
}

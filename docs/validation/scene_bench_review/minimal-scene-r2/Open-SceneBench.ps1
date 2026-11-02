param(
    [Parameter(Mandatory=$true)][string]$Packet,
    [string]$OutputRoot = (Join-Path $PSScriptRoot 'results'),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$benchPacket = Get-Content -LiteralPath $Packet -Raw | ConvertFrom-Json
$benchForm = New-Object System.Windows.Forms.Form
$benchForm.Text = 'DLSS5 受控 3D 实验台'
$benchForm.ClientSize = New-Object System.Drawing.Size(430,220)
$benchForm.FormBorderStyle = 'FixedDialog'
$benchForm.MaximizeBox = $false
$benchForm.StartPosition = 'CenterScreen'
$benchTip = New-Object System.Windows.Forms.ToolTip
function Add-Label([string]$Text,[int]$Top) {
    $label = New-Object System.Windows.Forms.Label
    $label.Text=$Text; $label.Location=New-Object System.Drawing.Point(20,$Top)
    $label.Size=New-Object System.Drawing.Size(390,24)
    $benchForm.Controls.Add($label)
    return $label
}
$null=Add-Label '开发实验：低分 NR→RR，自动比较 NR 关闭和开启。' 18
$routeLabel=Add-Label '路线' 56
$routeLabel.Width=60
$routeChoice=New-Object System.Windows.Forms.ComboBox
$routeChoice.DropDownStyle='DropDownList'
$routeChoice.Location=New-Object System.Drawing.Point(85,52)
$routeChoice.Width=120
foreach($route in $benchPacket.supported_routes){$null=$routeChoice.Items.Add($route)}
if($routeChoice.Items.Count){$routeChoice.SelectedIndex=0}
$benchForm.Controls.Add($routeChoice)
$kindChoice=New-Object System.Windows.Forms.ComboBox
$kindChoice.DropDownStyle='DropDownList'
$kindChoice.Location=New-Object System.Drawing.Point(220,52)
$kindChoice.Width=185
$null=$kindChoice.Items.Add('画质 + 开销')
$null=$kindChoice.Items.Add('只看画质')
$null=$kindChoice.Items.Add('只测开销')
$kindChoice.SelectedIndex=0
$benchForm.Controls.Add($kindChoice)
$null=Add-Label '输出 1080p · 输入 720p · NR 单层' 91
$benchTip.SetToolTip($routeChoice,'当前为同源 Core 宿主的 NR→RR 开发实验。产品默认 RR→NR 与 SR 尚待接入。仅列出已经验证支持的路线。')
$benchTip.SetToolTip($kindChoice,'画质采集和性能测量分开运行。测开销时关闭画面读回；显存为测试进程的采样用量。')
$benchStatus=Add-Label '测试期间请勿运行游戏或其他 GPU 测试。' 124
$benchStatus.Height=40
$runButton=New-Object System.Windows.Forms.Button
$runButton.Text='运行对照'
$runButton.Location=New-Object System.Drawing.Point(20,172)
$runButton.Size=New-Object System.Drawing.Size(385,30)
$runButton.Enabled=(@($benchPacket.supported_modes) -contains 'capture') -and (@($benchPacket.supported_modes) -contains 'perf')
if(-not $runButton.Enabled){$benchStatus.Text='采集与测量尚未完成验证。';$runButton.Text='准备中'}
$benchForm.Controls.Add($runButton)
$runButton.Add_Click({
    $runButton.Enabled=$false
    $benchOutput=Join-Path $OutputRoot ('run-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $benchKind=@('all','capture','perf')[$kindChoice.SelectedIndex]
    try {
        # Hide the UI during measurement; CPU media export starts only after all
        # native arms finish. No game is launched or stopped by this program.
        $benchForm.Hide()
        & $Python -B (Join-Path $PSScriptRoot 'scene_suite.py') --packet $Packet --out $benchOutput --kind $benchKind --route ([string]$routeChoice.SelectedItem) --execute
        if($LASTEXITCODE -ne 0){throw '本次对照未完成，已有结果保留。'}
        $benchStatus.Text='已完成。结果已打开。'
        Start-Process -FilePath (Join-Path $benchOutput 'report.html')
    } catch {
        $benchStatus.Text=$_.Exception.Message
    } finally {
        $benchForm.Show();$runButton.Enabled=$true
    }
})
$null=$benchForm.ShowDialog()

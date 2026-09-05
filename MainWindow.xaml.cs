using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using Microsoft.Win32;

namespace Dlss5InstallAssistant;

public partial class MainWindow : Window
{
    readonly ObservableCollection<GameCandidate> games = new();
    PackageManifest? package;
    CancellationTokenSource? cancellation;
    bool busy;
    public MainWindow() { InitializeComponent(); Games.ItemsSource = games; }
    GameCandidate? Selected => Games.SelectedItem as GameCandidate;
    void Note(string text) { LogBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {text}\n"); LogBox.ScrollToEnd(); }
    void SetBusy(bool value)
    {
        busy = value; ControlsPanel.IsEnabled = !value; ActionPanel.IsEnabled = !value; Games.IsEnabled = !value;
        CancelInstallButton.IsEnabled = value;
    }
    async Task Run(Func<CancellationToken, Task> action)
    {
        if (busy) return; SetBusy(true); cancellation = new();
        try { await action(cancellation.Token); }
        catch (OperationCanceledException) { Note("操作已取消；已开始的事务会安全回滚。"); }
        catch (Exception ex) { Note("未完成：" + ex.Message); }
        finally { cancellation.Dispose(); cancellation = null; SetBusy(false); Games.Items.Refresh(); }
    }
    async void ScanSteam(object sender, RoutedEventArgs e) => await Run(async token =>
    {
        var found = await Task.Run(() => SteamScanner.Scan().ToList(), token);
        foreach (var game in found) Add(game); Note($"发现 {found.Count} 个候选；首版自动安装支持赛博朋克2077和巫师3的 D3D12 版本。");
    });
    async void ChooseExe(object sender, RoutedEventArgs e)
    {
        if (busy) return; var picker = new OpenFileDialog { Filter = "游戏程序 (*.exe)|*.exe", Multiselect = true };
        if (picker.ShowDialog() != true) return;
        await Run(async token => { foreach (var path in picker.FileNames) Add(await Task.Run(() => GameInspector.Inspect(path, null), token)); Note("已添加。手选同样核对游戏身份与原生 D3D12 SR。"); });
    }
    void Add(GameCandidate game) { if (!games.Any(x => SafeFiles.PathEquals(x.Executable, game.Executable))) games.Add(game); }
    async void ImportPackage(object sender, RoutedEventArgs e)
    {
        if (busy) return; var picker = new OpenFileDialog { Filter = "完整组件包 (*.zip;manifest.json)|*.zip;manifest.json|组件 ZIP (*.zip)|*.zip|已解压清单 (manifest.json)|manifest.json" }; if (picker.ShowDialog() != true) return;
        package = null; PackageLabel.Text = "尚未导入安装包";
        await Run(async token => { var loaded = await Task.Run(() => PackageImport.LoadSelected(picker.FileName), token); package = loaded;
            var addon = loaded.Files.Single(x => x.Role == "addon");
            var purpose = loaded.Edition == "enhanced-recommended" ? "增强主测 / 推荐" : "稳妥兼容 / 遇到问题再选";
            PackageLabel.Text = $"{loaded.Build} · {purpose} · {loaded.GpuFamily} · {loaded.Language} · 源码 {loaded.PayloadSourceCommit[..8]} · 插件 {addon.Sha256[..12]} · 签名及文件校验通过";
            Note(PackageLabel.Text); });
    }
    async void ImportReShade(object sender, RoutedEventArgs e)
    {
        if (busy) return; var picker = new OpenFileDialog { Filter = "官方 ReShade 6.8 FullAddon 安装程序|ReShade_Setup_6.8.0_Addon.exe|程序 (*.exe)|*.exe" }; if (picker.ShowDialog() != true) return;
        await Run(async token => { await ReShadeHost.ImportSetupAsync(picker.FileName, ElevationHelper.CacheRoot, token); Note("官方 ReShade 6.8 FullAddon 已在本地缓存准备好；尚未写入游戏。"); });
    }
    async Task<DeploymentPlan> PreparePlan(GameCandidate game, PackageManifest selectedPackage, CancellationToken token)
    {
        Note("核对游戏和官方 ReShade 6.8 FullAddon；只在缓存中准备宿主。");
        var host = await ReShadeHost.PrepareAsync(ElevationHelper.CacheRoot, token);
        var plan = await Task.Run(() => Deployer.Inspect(game, selectedPackage, host), token);
        var addon = selectedPackage.Files.Single(x => x.Role == "addon");
        var purpose = selectedPackage.Edition == "enhanced-recommended" ? "增强主测 / 推荐" : "稳妥兼容 / 遇到问题再选";
        PlanBox.Text = $"版本：{selectedPackage.Build}（{purpose}）{Environment.NewLine}源码提交：{selectedPackage.PayloadSourceCommit}{Environment.NewLine}插件 SHA256：{addon.Sha256}{Environment.NewLine}{Environment.NewLine}" +
            string.Join(Environment.NewLine, plan.Files.Select(f => $"{f.Action switch { FileAction.Reuse => "保留 / 复用", FileAction.Remove => "备份后移除旧版", _ => "备份后写入" }}  {f.Destination}{(string.IsNullOrWhiteSpace(f.Detail) ? "" : Environment.NewLine + "    " + f.Detail)}"));
        return plan;
    }
    async void Inspect(object sender, RoutedEventArgs e)
    { if (Selected is not { } game || package is not { } selectedPackage) { Note("请选择游戏并导入对应显卡的签名安装包。"); return; } await Run(async token => { await PreparePlan(game, selectedPackage, token); Note("安装计划已显示，尚未写入游戏。"); }); }
    async void Install(object sender, RoutedEventArgs e)
    {
        if (Selected is not { } game || package is not { } selectedPackage) { Note("请选择游戏并导入对应显卡的签名安装包。"); return; }
        await Run(async token =>
        {
            var plan = await PreparePlan(game, selectedPackage, token);
            OperationResult result;
            try { result = await Task.Run(() => Deployer.Apply(plan, token), token); }
            catch (UnauthorizedAccessException) { Note("游戏目录需要管理员权限；等待系统授权和安装结果。"); result = await ElevationHelper.TryRunForAsync("install", game, selectedPackage.Root, token); }
            Note(result.Message); game.Status = Launcher.Describe(game);
        });
    }
    void CancelInstall(object sender, RoutedEventArgs e) { cancellation?.Cancel(); Note("已请求取消；请等待当前文件操作和回滚结束。"); }
    async Task Mutate(string operation)
    {
        if (Selected is not { } game) { Note("请选择游戏。"); return; }
        await Run(async token =>
        {
            OperationResult result;
            try { result = await Task.Run(() => operation switch { "recover" => Deployer.Recover(game), "restore" => Deployer.RestoreLatest(game), _ => Deployer.Uninstall(game) }, token); }
            catch (UnauthorizedAccessException) { result = await ElevationHelper.TryRunForAsync(operation, game, null, token); }
            Note(result.Message); game.Status = Launcher.Describe(game);
        });
    }
    async void Uninstall(object sender, RoutedEventArgs e) => await Mutate("uninstall");
    async void Restore(object sender, RoutedEventArgs e) => await Mutate("restore");
    async void Recover(object sender, RoutedEventArgs e) => await Mutate("recover");
    async void Launch(object sender, RoutedEventArgs e)
    { if (Selected is not { } game) return; await Run(async token => { Note("已请求启动，等待确认所选游戏进程；期间不会更改安装文件。"); await Launcher.StartAsync(game, token); Note("已确认游戏进程；稍后刷新可确认实际加载与 NR 提交。"); }); }
    async void RefreshStatus(object sender, RoutedEventArgs e) => await Run(async token =>
    { foreach (var game in games) { try { game.Status = await Task.Run(() => Launcher.Describe(game), token); } catch (Exception ex) { game.Status = "无法验证：" + ex.Message; } } Note("状态已刷新。只有实际加载路径、插件哈希和当前进程一致才匹配此次部署。"); });
}

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using Microsoft.Win32;

namespace Dlss5InstallAssistant;

public partial class MainWindow : Window
{
    readonly ObservableCollection<GameCandidate> games = new();
    PackageManifest? package;
    public MainWindow() { InitializeComponent(); Games.ItemsSource = games; }
    void Note(string s) { LogBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {s}\n"); LogBox.ScrollToEnd(); }
    void ScanSteam(object s, RoutedEventArgs e) { foreach (var g in SteamScanner.Scan()) Add(g); Note($"Steam 扫描完成：发现 {games.Count} 个候选。扫描不提权，也不修改文件。"); }
    void RefreshStatus(object s, RoutedEventArgs e) { foreach (var g in games) { var status = Launcher.ReadStatus(g); g.Status = status is null ? "已部署/待运行时确认" : $"已生效（{status.BuildId}，PID {status.ProcessId}）"; } Games.Items.Refresh(); Note("已刷新当前运行时状态；过期或非当前进程状态不会计为生效。"); }
    void ChooseExe(object s, RoutedEventArgs e) { var d = new OpenFileDialog { Filter="游戏程序 (*.exe)|*.exe", Multiselect=true }; if (d.ShowDialog()!=true) return; foreach(var p in d.FileNames) Add(GameInspector.Inspect(p, null)); Note("已添加手选 EXE；是否兼容仍以运行时状态为准。"); }
    void Add(GameCandidate g) { if (!games.Any(x=>String.Equals(x.Executable,g.Executable,StringComparison.OrdinalIgnoreCase))) games.Add(g); }
    void ImportPackage(object s, RoutedEventArgs e) { var d=new OpenFileDialog { Filter="安装包清单 (manifest.json)|manifest.json|JSON (*.json)|*.json" }; if(d.ShowDialog()!=true)return; try { package=PackageManifest.Load(Path.GetDirectoryName(d.FileName)!); Note($"已验证安装包：{package.Id}，{package.Files.Count} 个文件。"); } catch(Exception ex){ Note("安装包拒绝："+ex.Message); } }
    GameCandidate? Selected => Games.SelectedItem as GameCandidate;
    void Install(object s, RoutedEventArgs e) { if(Selected is null || package is null){Note("请先选择游戏并导入本地安装包。");return;} try { var r=Deployer.Install(Selected,package); Note(r.Message); Selected.Status=r.Message; Games.Items.Refresh(); }catch(Exception ex){Note("安装未完成，已尝试回滚："+ex.Message);} }
    void Uninstall(object s, RoutedEventArgs e) { if(Selected is null){Note("请先选择游戏。");return;} try { Note(Deployer.Uninstall(Selected)); }catch(Exception ex){Note("卸载失败："+ex.Message);} }
    void Restore(object s, RoutedEventArgs e) { if(Selected is null){Note("请先选择游戏。");return;} try { Note(Deployer.RestoreLatest(Selected)); }catch(Exception ex){Note("恢复失败："+ex.Message);} }
    void Launch(object s, RoutedEventArgs e) { if(Selected is null){Note("请先选择游戏。");return;} try { Launcher.Start(Selected); Note("已请求启动；安装状态与运行时生效状态分开显示。"); }catch(Exception ex){Note("启动失败："+ex.Message);} }
}

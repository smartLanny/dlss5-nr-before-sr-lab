using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;

namespace Dlss5InstallAssistant;

internal static class SafeFiles
{
    internal static bool PathEquals(string? a, string? b) => !string.IsNullOrWhiteSpace(a) && !string.IsNullOrWhiteSpace(b) && Path.GetFullPath(a).TrimEnd(Path.DirectorySeparatorChar).Equals(Path.GetFullPath(b).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase);
    internal static void RejectReparse(string path)
    {
        for (var current = Path.GetFullPath(path); current != null; current = Path.GetDirectoryName(current))
        {
            FileAttributes attributes;
            try { attributes = File.GetAttributes(current); } catch (FileNotFoundException) { continue; } catch (DirectoryNotFoundException) { continue; }
            if ((attributes & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("不写入符号链接或 junction：" + current);
        }
    }
    internal static string CanonicalExistingPath(string path)
    {
        RejectReparse(path); if (!OperatingSystem.IsWindows()) return Path.GetFullPath(path);
        using var handle = OpenPath(NativePath(path), 0, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero);
        if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), "无法解析目标文件身份。");
        var text = new StringBuilder(32768); var length = GetFinalPath(handle, text, (uint)text.Capacity, 0);
        if (length == 0 || length >= text.Capacity) throw new IOException("无法解析目标完整路径。");
        var value = text.ToString(); return value.StartsWith(@"\\?\UNC\") ? @"\\" + value[8..] : value.StartsWith(@"\\?\") ? value[4..] : value;
    }
    internal static string NativePath(string path) { var full = Path.GetFullPath(path); return full.StartsWith(@"\\?\") ? full : full.StartsWith(@"\\") ? @"\\?\UNC\" + full[2..] : @"\\?\" + full; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "CreateFileW")]
    static extern SafeFileHandle OpenPath(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "GetFinalPathNameByHandleW")]
    static extern uint GetFinalPath(SafeFileHandle handle, StringBuilder path, uint length, uint flags);
    internal static string? ExistingHash(string path) { RejectReparse(path); if (Directory.Exists(path)) throw new IOException("目标是目录：" + path); return File.Exists(path) ? PackageManifest.Hash(path) : null; }
    internal static void AtomicText(string path, string text)
    {
        RejectReparse(path); Directory.CreateDirectory(Path.GetDirectoryName(path)!); var tmp = path + ".tmp-" + Guid.NewGuid().ToString("N");
        try { using (var fs = new FileStream(tmp, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough)) { var b = Encoding.UTF8.GetBytes(text); fs.Write(b); fs.Flush(true); } File.Move(tmp, path, true); }
        finally { if (File.Exists(tmp)) File.Delete(tmp); }
    }
}

// Locks the same target across normal/elevated processes and prevents directory
// replacement while a transaction uses it. Every ancestor is checked and pinned.
internal sealed class TargetLease : IDisposable
{
    readonly List<SafeFileHandle> directories = new(); readonly HashSet<string> pinned = new(StringComparer.OrdinalIgnoreCase);
    FileStream? stream;
    public TargetLease(GameCandidate game)
    {
        try
        {
            Pin(game.Directory);
            var state = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DLSS5InstallAssistant", "locks");
            SafeFiles.RejectReparse(state); Directory.CreateDirectory(state); Pin(state);
            var identity = SafeFiles.CanonicalExistingPath(game.Directory).TrimEnd(Path.DirectorySeparatorChar).ToUpperInvariant();
            var key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(identity)));
            var path = Path.Combine(state, key + ".lock"); SafeFiles.RejectReparse(path);
            try { stream = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
            catch (IOException ex) { throw new IOException("此游戏已有安装、恢复或启动操作，请等待完成。", ex); }
        }
        catch { Dispose(); throw; }
    }
    public void Pin(string directory)
    {
        var ancestors = new Stack<string>(); for (var p = Path.GetFullPath(directory); p != null; p = Path.GetDirectoryName(p)) ancestors.Push(p);
        while (ancestors.TryPop(out var path))
        {
            SafeFiles.RejectReparse(path); if (!Directory.Exists(path) || !pinned.Add(path)) continue;
            if (!OperatingSystem.IsWindows()) continue;
            var handle = CreateFile(SafeFiles.NativePath(path), 0, 3, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero);
            if (handle.IsInvalid) { var error = Marshal.GetLastWin32Error(); handle.Dispose(); throw new Win32Exception(error, "无法锁定目标目录：" + path); }
            directories.Add(handle); SafeFiles.RejectReparse(path);
        }
    }
    public void Dispose() { stream?.Dispose(); for (var i = directories.Count - 1; i >= 0; i--) directories[i].Dispose(); directories.Clear(); }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "CreateFileW")]
    static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
}

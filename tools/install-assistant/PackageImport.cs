using System.IO.Compression;

namespace Dlss5InstallAssistant;

internal static class PackageImport
{
    const int MaxEntries = 24;
    const long MaxArchiveBytes = 512L * 1024 * 1024;
    const long MaxEntryBytes = 256L * 1024 * 1024;
    const long MaxExpandedBytes = 512L * 1024 * 1024;

    internal static string CacheRoot => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "DLSS5InstallAssistant", "imported-packages");

    internal static PackageManifest LoadSelected(string selectedPath) =>
        LoadSelected(selectedPath, CacheRoot, PackageManifest.ReleasePublicKey);

    internal static PackageManifest LoadSelected(string selectedPath, string cacheRoot,
                                                 string trustKey)
    {
        selectedPath = Path.GetFullPath(selectedPath);
        SafeFiles.RejectReparse(selectedPath);
        if (!File.Exists(selectedPath))
            throw new FileNotFoundException("没有找到选择的组件包。", selectedPath);

        if (Path.GetFileName(selectedPath).Equals("manifest.json",
                                                 StringComparison.OrdinalIgnoreCase))
            return PackageManifest.LoadWithTrust(Path.GetDirectoryName(selectedPath)!, trustKey);
        if (!Path.GetExtension(selectedPath).Equals(".zip", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("请选择完整组件 ZIP，或已解压目录中的 manifest.json。");

        var archiveInfo = new FileInfo(selectedPath);
        if (archiveInfo.Length <= 0 || archiveInfo.Length > MaxArchiveBytes)
            throw new InvalidDataException("组件 ZIP 超过 512 MiB 限制或为空。");

        cacheRoot = Path.GetFullPath(cacheRoot);
        SafeFiles.RejectReparse(cacheRoot);
        Directory.CreateDirectory(cacheRoot);
        SafeFiles.RejectReparse(cacheRoot);

        var archiveHash = PackageManifest.Hash(selectedPath);
        var cached = Path.Combine(cacheRoot, "sha256-" + archiveHash);
        if (Directory.Exists(cached))
        {
            SafeFiles.RejectReparse(cached);
            try { return PackageManifest.LoadWithTrust(cached, trustKey); }
            catch (Exception ex) when (ex is IOException or InvalidDataException or
                                           UnauthorizedAccessException or
                                           System.Security.Cryptography.CryptographicException)
            {
                // Never remove a cache directory we did not create in this call.
                cached += "-" + Guid.NewGuid().ToString("N");
            }
        }

        var stage = Path.Combine(cacheRoot, ".import-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        var created = new List<string>();
        try
        {
            using var file = new FileStream(selectedPath, FileMode.Open, FileAccess.Read,
                                            FileShare.Read);
            using var zip = new ZipArchive(file, ZipArchiveMode.Read, leaveOpen: false);
            if (zip.Entries.Count is 0 or > MaxEntries)
                throw new InvalidDataException("组件 ZIP 文件数量不在允许范围内。");

            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            long expanded = 0;
            foreach (var entry in zip.Entries)
            {
                var name = PackageManifest.NormalizeLeaf(entry.FullName);
                if (!names.Add(name))
                    throw new InvalidDataException("组件 ZIP 包含重复文件：" + name);
                if (entry.Length < 0 || entry.Length > MaxEntryBytes ||
                    expanded > MaxExpandedBytes - entry.Length)
                    throw new InvalidDataException("组件 ZIP 解压后超过 512 MiB 限制。");
                if (entry.Length > 16L * 1024 * 1024 && entry.CompressedLength > 0 &&
                    entry.Length / entry.CompressedLength > 1000)
                    throw new InvalidDataException("组件 ZIP 压缩比例异常，已拒绝解压。");
                expanded += entry.Length;

                var destination = PackageManifest.SafePath(stage, name);
                SafeFiles.RejectReparse(destination);
                using var input = entry.Open();
                using var output = new FileStream(destination, FileMode.CreateNew,
                                                  FileAccess.Write, FileShare.None,
                                                  64 * 1024, FileOptions.WriteThrough);
                input.CopyTo(output);
                output.Flush(true);
                if (output.Length != entry.Length)
                    throw new InvalidDataException("组件 ZIP 解压长度不匹配：" + name);
                created.Add(destination);
            }

            _ = PackageManifest.LoadWithTrust(stage, trustKey);
            try
            {
                if (!Directory.Exists(cached))
                {
                    Directory.Move(stage, cached);
                    stage = cached;
                }
            }
            catch (IOException)
            {
                // A concurrent importer may have created the hash cache. The
                // already verified isolated directory remains a safe package root.
            }
            return PackageManifest.LoadWithTrust(stage, trustKey);
        }
        catch
        {
            // This staging directory is flat and every listed file was created by
            // this call, so cleanup does not cross a mutable directory tree.
            foreach (var path in created.AsEnumerable().Reverse())
            {
                try { SafeFiles.RejectReparse(path); File.Delete(path); } catch { }
            }
            try { Directory.Delete(stage, false); } catch { }
            throw;
        }
    }
}

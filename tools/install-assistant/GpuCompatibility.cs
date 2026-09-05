using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

namespace Dlss5InstallAssistant;

internal static class GpuCompatibility
{
    const int DisplayDeviceMirroringDriver = 0x00000008;
    static readonly Regex GeForceRtxModel = new(@"\bGeForce\s+RTX\s+(?<model>\d{4})\b",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    internal static Func<IReadOnlyList<string>>? TestAdapterNames { get; set; }

    internal static void EnsurePackageMatches(string expectedFamily)
    {
        var adapters = (TestAdapterNames?.Invoke() ?? EnumerateAdapterNames())
            .Where(x => !string.IsNullOrWhiteSpace(x)).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var families = adapters.Select(Classify).Where(x => x != null).Cast<string>()
            .Distinct(StringComparer.Ordinal).ToArray();

        if (families.Length == 0)
            throw new InvalidDataException("无法确认本机 NVIDIA GeForce RTX 代际，已停止安装。请下载与显卡对应的完整组件包，或提交安装助手日志确认显卡型号。");
        if (families.Length > 1)
            throw new InvalidDataException("检测到 RTX 50 与 RTX 30/40 两类显卡，无法安全自动选择运行库，已停止安装。");
        if (!families[0].Equals(expectedFamily, StringComparison.Ordinal))
            throw new InvalidDataException($"安装包适用于 {expectedFamily}，本机检测为 {families[0]}，已停止安装。请改用对应显卡的完整组件包。");
    }

    static string? Classify(string name)
    {
        var match = GeForceRtxModel.Match(name);
        if (!match.Success || !int.TryParse(match.Groups["model"].Value, out var model)) return null;
        if (model is >= 5000 and <= 5999) return "RTX50";
        if (model is >= 3000 and <= 4999) return "RTX30-40";
        return null;
    }

    static IReadOnlyList<string> EnumerateAdapterNames()
    {
        if (!OperatingSystem.IsWindows()) return Array.Empty<string>();
        var result = new List<string>();
        for (uint index = 0; index < 64; index++)
        {
            var device = new DisplayDevice { Size = Marshal.SizeOf<DisplayDevice>() };
            if (!EnumDisplayDevices(null, index, ref device, 0)) break;
            if ((device.StateFlags & DisplayDeviceMirroringDriver) == 0) result.Add(device.Description ?? "");
        }
        return result;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DisplayDevice
    {
        public int Size;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string? Name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string? Description;
        public int StateFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string? DeviceId;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string? RegistryKey;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool EnumDisplayDevices(string? device, uint index, ref DisplayDevice displayDevice, uint flags);
}

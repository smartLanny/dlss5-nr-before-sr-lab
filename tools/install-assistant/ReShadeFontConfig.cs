using System.Text;

namespace Dlss5InstallAssistant;

internal static class ReShadeFontConfig
{
    internal const string MicrosoftYaHei = @"C:\Windows\Fonts\msyh.ttc";
    internal const string SimSun = @"C:\Windows\Fonts\simsun.ttc";

    // A narrow test seam keeps the deployment tests independent of the fonts
    // installed on the build machine. Production always uses File.Exists.
    internal static Func<string, bool>? TestFileExists { get; set; }

    internal static bool TryAddChineseOverlayFont(byte[] original, out byte[] updated,
                                                   out string? selectedFont)
    {
        updated = original;
        selectedFont = SelectInstalledFont();
        if (selectedFont == null || original.Length > 4 * 1024 * 1024)
            return false;

        if (!TryDecode(original, out var text, out var encoding, out var preamble))
            return false;

        var lines = ScanLines(text);
        var styleSections = new List<(int Header, int End)>();
        var fontLines = new List<(LineSpan Line, int EqualsIndex)>();
        var activeStyle = -1;
        var ambiguousSection = false;

        for (var i = 0; i < lines.Count; ++i)
        {
            var line = text[lines[i].Start..lines[i].ContentEnd];
            var trimmed = line.Trim();
            if (trimmed.Length == 0 || trimmed.StartsWith(';') || trimmed.StartsWith('#'))
                continue;

            if (trimmed.StartsWith('[') && !trimmed.EndsWith(']'))
            {
                ambiguousSection = true;
                continue;
            }
            if (trimmed.StartsWith('[') && trimmed.EndsWith(']'))
            {
                if (activeStyle >= 0)
                {
                    var current = styleSections[activeStyle];
                    styleSections[activeStyle] = (current.Header, lines[i].Start);
                    activeStyle = -1;
                }

                if (trimmed[1..^1].Trim().Equals("STYLE", StringComparison.OrdinalIgnoreCase))
                {
                    styleSections.Add((lines[i].Start, text.Length));
                    activeStyle = styleSections.Count - 1;
                }
                continue;
            }

            if (activeStyle < 0)
                continue;
            var equals = line.IndexOf('=');
            if (equals >= 0 && line[..equals].Trim().Equals("Font", StringComparison.OrdinalIgnoreCase))
                fontLines.Add((lines[i], equals));
        }

        // Ambiguous duplicate sections or keys are treated as user-managed.
        if (ambiguousSection || styleSections.Count > 1 || fontLines.Count > 1)
            return false;
        if (fontLines.Count == 1)
        {
            var entry = fontLines[0];
            var line = text[entry.Line.Start..entry.Line.ContentEnd];
            if (!string.IsNullOrWhiteSpace(line[(entry.EqualsIndex + 1)..]))
                return false;
            text = text[..entry.Line.Start] + line[..(entry.EqualsIndex + 1)] + selectedFont +
                   text[entry.Line.ContentEnd..];
        }
        else if (styleSections.Count == 1)
        {
            var insertion = styleSections[0].End;
            var newline = PreferredNewline(text);
            var prefix = insertion > 0 && text[insertion - 1] is not ('\r' or '\n') ? newline : "";
            text = text[..insertion] + prefix + "Font=" + selectedFont + newline + text[insertion..];
        }
        else
        {
            var newline = PreferredNewline(text);
            var separator = text.Length == 0 ? "" :
                text[^1] is '\r' or '\n' ? newline : newline + newline;
            text += separator + "[STYLE]" + newline + "Font=" + selectedFont + newline;
        }

        updated = Encode(text, encoding, preamble);
        return !updated.AsSpan().SequenceEqual(original);
    }

    static string? SelectInstalledFont()
    {
        var exists = TestFileExists ?? File.Exists;
        if (exists(MicrosoftYaHei)) return MicrosoftYaHei;
        if (exists(SimSun)) return SimSun;
        return null;
    }

    static string PreferredNewline(string text)
    {
        var lf = text.IndexOf('\n');
        if (lf >= 0) return lf > 0 && text[lf - 1] == '\r' ? "\r\n" : "\n";
        return text.Contains('\r') ? "\r" : "\r\n";
    }

    static List<LineSpan> ScanLines(string text)
    {
        var lines = new List<LineSpan>();
        for (var start = 0; start < text.Length;)
        {
            var end = start;
            while (end < text.Length && text[end] is not ('\r' or '\n')) ++end;
            var next = end;
            if (next < text.Length && text[next] == '\r') ++next;
            if (next < text.Length && text[next] == '\n') ++next;
            lines.Add(new(start, end));
            start = next;
        }
        if (text.Length == 0) lines.Add(new(0, 0));
        return lines;
    }

    static bool TryDecode(byte[] bytes, out string text, out Encoding encoding,
                          out byte[] preamble)
    {
        text = "";
        preamble = Array.Empty<byte>();
        var offset = 0;
        if (bytes.AsSpan().StartsWith(new byte[] { 0xEF, 0xBB, 0xBF }))
        {
            encoding = new UTF8Encoding(false, true); offset = 3;
            preamble = bytes[..offset];
        }
        else if (bytes.AsSpan().StartsWith(new byte[] { 0xFF, 0xFE, 0x00, 0x00 }) ||
                 bytes.AsSpan().StartsWith(new byte[] { 0x00, 0x00, 0xFE, 0xFF }))
        {
            encoding = Encoding.Latin1;
            return false; // Preserve uncommon UTF-32 files rather than guessing.
        }
        else if (bytes.AsSpan().StartsWith(new byte[] { 0xFF, 0xFE }))
        {
            encoding = new UnicodeEncoding(false, false, true); offset = 2;
            preamble = bytes[..offset];
        }
        else if (bytes.AsSpan().StartsWith(new byte[] { 0xFE, 0xFF }))
        {
            encoding = new UnicodeEncoding(true, false, true); offset = 2;
            preamble = bytes[..offset];
        }
        else
        {
            if (bytes.AsSpan().IndexOf((byte)0) >= 0)
            {
                encoding = Encoding.Latin1;
                return false; // Preserve an unmarked wide-character file.
            }
            // Latin-1 is deliberately used as a byte-preserving transport here.
            // ReShade keys and the inserted path are ASCII, so UTF-8 and legacy
            // non-ASCII values round-trip without reinterpretation.
            encoding = Encoding.Latin1;
        }

        try { text = encoding.GetString(bytes, offset, bytes.Length - offset); return true; }
        catch (DecoderFallbackException) { return false; }
    }

    static byte[] Encode(string text, Encoding encoding, byte[] preamble)
    {
        var body = encoding.GetBytes(text);
        if (preamble.Length == 0) return body;
        var result = new byte[preamble.Length + body.Length];
        preamble.CopyTo(result, 0); body.CopyTo(result, preamble.Length);
        return result;
    }

    readonly record struct LineSpan(int Start, int ContentEnd);
}

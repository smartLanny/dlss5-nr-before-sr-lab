#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

// CPU-only policies shared by the runtime collector and host regressions.
namespace nr_feedback_text {
constexpr std::uint64_t kCoreZipLimit = 8ull * 1024 * 1024;
constexpr std::uint64_t kScanLimit = 32ull * 1024 * 1024;
constexpr std::size_t kLineLimit = 4096;
constexpr std::size_t kSummaryLimit = 256 * 1024;
inline unsigned CoreHotkey(unsigned key) {
    // F6 owns master enable, F7 owns image capture. Never bind both actions.
    return key >= 112 && key <= 123 && key != 117 && key != 118 ? key : 119;
}
inline std::string SessionId(unsigned long pid, std::uint64_t tick, std::uint64_t serial) {
    return "DLSS5_CORE_" + std::to_string(pid) + "_" + std::to_string(tick) + "_" + std::to_string(serial);
}
inline std::string Lower(std::string value) {
    for (char &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
inline bool Hex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
inline bool Word(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
inline std::string Sanitize(std::string_view input) {
    // Log excerpts are intentionally ASCII. This avoids invalid UTF-8 when a
    // producer writes half a multibyte character or a bounded line is cut.
    std::string line;
    line.reserve(std::min(input.size(), kLineLimit));
    for (char c : input.substr(0, kLineLimit)) {
        const auto u = static_cast<unsigned char>(c);
        if (u == '\r' || u == '\n') continue;
        line += u >= 32 && u < 127 ? c : (u == '\t' ? ' ' : '?');
    }
    const auto lower = Lower(line);
    for (const char *secret : {"authorization:", "password=", "access_token=", "github_pat_", "ghp_", "private key"})
        if (lower.find(secret) != std::string::npos) return "[sensitive log line omitted]";
    const bool duplicate_init_abort =
        lower.find("another reshade instance") != std::string::npos &&
        lower.find("aborting initialization") != std::string::npos;
    // Paths may contain spaces. Redact to the next explicit field separator,
    // not just the first space; otherwise usernames/game paths leak in tails.
    for (std::size_t i = 0; i < line.size();) {
        const bool drive = i + 2 < line.size() && std::isalpha(static_cast<unsigned char>(line[i])) &&
            line[i + 1] == ':' && (line[i + 2] == '\\' || line[i + 2] == '/');
        const bool unc = i + 1 < line.size() && line[i] == '\\' && line[i + 1] == '\\';
        if (!drive && !unc) { ++i; continue; }
        const auto end = line.find(" | ", i);
        line.replace(i, (end == std::string::npos ? line.size() : end) - i, "[local-path]");
        i += 12;
    }
    // Hide pointer-sized hex values, but preserve HRESULTs (8 digits), shader
    // hashes and complete SHA-1/SHA-256 identities (40/64 digits).
    for (std::size_t i = 0; i < line.size();) {
        if (i && Word(line[i - 1])) { ++i; continue; }
        std::size_t begin = i;
        if (i + 2 < line.size() && line[i] == '0' && (line[i + 1] == 'x' || line[i + 1] == 'X')) begin += 2;
        std::size_t end = begin;
        while (end < line.size() && Hex(line[end])) ++end;
        const auto n = end - begin;
        if (n >= 12 && n <= 16 && (end == line.size() || !Word(line[end]))) {
            line.replace(i, end - i, "[address]"); i += 9;
        } else ++i;
    }
    // ReShade places the abort text AFTER the loaded module's private path.
    // Preserve only a fixed marker, never reconstruct any removed suffix.
    // This remains a historical text clue, not a trusted/current state claim.
    if (duplicate_init_abort && Lower(line).find("aborting initialization") == std::string::npos) {
        constexpr std::string_view marker = " [Aborting initialization]";
        if (line.size() > kLineLimit - marker.size()) line.resize(kLineLimit - marker.size());
        line += marker;
    }
    return line;
}
struct Window {
    std::deque<std::string> lines;
    std::size_t bytes = 0;
    void Push(std::string line, std::size_t limit) {
        line += '\n';
        if (line.size() > limit) return;
        while (bytes + line.size() > limit) { bytes -= lines.front().size(); lines.pop_front(); }
        bytes += line.size(); lines.push_back(std::move(line));
    }
    std::string Text() const { std::string out; out.reserve(bytes); for (const auto &s : lines) out += s; return out; }
};
struct Summary {
    std::string head, first;
    Window recent, tail;
    std::vector<std::string> first_keys;
    std::uint64_t lines = 0, long_lines = 0;
    void Push(std::string_view raw, bool truncated = false) {
        ++lines; long_lines += truncated;
        auto line = Sanitize(raw);
        if (truncated) line += " [line truncated]";
        if (head.size() + line.size() + 1 <= 32768) head += line + '\n';
        tail.Push(line, 65536);
        const auto lower = Lower(line);
        const bool important = lower.find("bypass") != std::string::npos || lower.find("error") != std::string::npos ||
            lower.find("fail") != std::string::npos || lower.find("hook") != std::string::npos ||
            lower.find("feature") != std::string::npos || lower.find("colordiag") != std::string::npos ||
            lower.find("another reshade instance") != std::string::npos ||
            lower.find("device_removed") != std::string::npos || lower.find("device_hung") != std::string::npos ||
            lower.find("device removed") != std::string::npos || lower.find("device hung") != std::string::npos;
        if (!important) return;
        recent.Push(line, 65536);
        // Retain the FIRST occurrence of each bypass reason even when the
        // latest tail is dominated by thousands of later successful frames.
        auto p = lower.find("reason=");
        std::string key;
        if (p != std::string::npos) {
            const auto end = lower.find_first_of(" ,;\t", p + 7);
            key = lower.substr(p, end == std::string::npos ? end : end - p);
        } else if (lower.find("bypass transition") != std::string::npos) key = "first-bypass-transition";
        else if (lower.find("another reshade instance") != std::string::npos) key = "first-duplicate-reshade";
        else if (lower.find("device_removed") != std::string::npos || lower.find("device_hung") != std::string::npos ||
                 lower.find("device removed") != std::string::npos || lower.find("device hung") != std::string::npos) key = "first-device-failure";
        else if (lower.find("fail") != std::string::npos || lower.find("error") != std::string::npos) key = "first-error";
        else if (lower.find("feature 18 create") != std::string::npos) key = "first-feature18-create";
        if (!key.empty() && first_keys.size() < 32 && first.size() + line.size() + 1 <= 65536 &&
            std::find(first_keys.begin(), first_keys.end(), key) == first_keys.end()) {
            first_keys.push_back(key); first += line + '\n';
        }
    }
    std::string Text(std::uint64_t source_bytes, std::uint64_t scanned, bool complete) const {
        return "format=DLSS5-LogSummary-v1\nsource.bytes=" + std::to_string(source_bytes) +
            "\nscan.bytes=" + std::to_string(scanned) + "\nscan.complete=" + (complete ? "1" : "0") +
            "\nlines.scanned=" + std::to_string(lines) + "\nlines.truncated=" + std::to_string(long_lines) +
            "\nprivacy=ASCII excerpts; local paths and pointer-sized values redacted; hashes cover excerpts only\n"
            "\n[head]\n" + head + "\n[first events]\n" + first +
            "\n[recent important]\n" + recent.Text() + "\n[tail]\n" + tail.Text();
    }
};
} // namespace nr_feedback_text

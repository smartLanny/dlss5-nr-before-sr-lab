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
constexpr std::uint64_t kTailScanLimit = 64ull * 1024;
constexpr std::uint64_t kPerSourceScanMs = 400;
constexpr std::uint64_t kTailReserveMs = 100;
constexpr std::size_t kLineLimit = 4096;
constexpr std::size_t kSummaryLimit = 24 * 1024;
constexpr std::size_t kHeadLimit = 4 * 1024;
constexpr std::size_t kFirstLimit = 6 * 1024;
constexpr std::size_t kFirstEventLimit = 1024;
constexpr std::size_t kRecentLimit = 6 * 1024;
constexpr std::size_t kTailLimit = 6 * 1024;
static_assert(kHeadLimit + kFirstLimit + kRecentLimit + kTailLimit + 2048 <= kSummaryLimit,
    "summary section budgets must leave room for truthful coverage metadata");
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
inline bool DiagnosticLine(const std::string &lower) {
    return lower.find("bypass") != std::string::npos || lower.find("reason=") != std::string::npos ||
        lower.find("error") != std::string::npos || lower.find("fail") != std::string::npos ||
        lower.find("[native-carrier]") != std::string::npos || lower.find("[native-contract]") != std::string::npos ||
        lower.find("hook") != std::string::npos || lower.find("feature") != std::string::npos ||
        lower.find("colordiag") != std::string::npos || lower.find("transition") != std::string::npos ||
        lower.find("another reshade instance") != std::string::npos ||
        lower.find("device_removed") != std::string::npos || lower.find("device_hung") != std::string::npos ||
        lower.find("device removed") != std::string::npos || lower.find("device hung") != std::string::npos ||
        lower.find("unavailable") != std::string::npos || lower.find("unsupported") != std::string::npos ||
        lower.find("timeout") != std::string::npos || lower.find("rejected") != std::string::npos;
}
inline std::string RepeatKey(const std::string &line) {
    auto lower = Lower(line);
    const auto bar = lower.find(" | ");
    if (bar != std::string::npos && bar < 48 && lower.find(':') < bar &&
        std::any_of(lower.begin(), lower.begin() + bar, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) &&
        std::all_of(lower.begin(), lower.begin() + bar, [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.' || c == '-' ||
                c == '/' || c == '[' || c == ']' || c == ' ' || c == 't' || c == 'z';
        }))
        lower.replace(0, bar, "[time]");
    else if (lower.size() > 20 && std::isdigit(static_cast<unsigned char>(lower[0])) &&
             lower[4] == '-' && lower[7] == '-' && (lower[10] == 't' || lower[10] == ' ')) {
        const auto timestamp_end = lower.find(' ');
        if (timestamp_end != std::string::npos && timestamp_end <= 32) lower.replace(0, timestamp_end, "[time]");
    }
    const bool volatile_counter = lower.find("frame=") != std::string::npos ||
        lower.find("frame ") != std::string::npos || lower.find("count=") != std::string::npos ||
        lower.find("counter") != std::string::npos || lower.find("elapsed") != std::string::npos ||
        lower.find("sample=") != std::string::npos || lower.find("progress") != std::string::npos ||
        lower.find("evaluate success") != std::string::npos;
    const bool stateful_failure = lower.find("reason=") != std::string::npos ||
        lower.find("bypass") != std::string::npos || lower.find("error") != std::string::npos ||
        lower.find("fail") != std::string::npos || lower.find("device") != std::string::npos;
    if (!volatile_counter || stateful_failure) return lower;
    for (const char *field : {"frame=", "frame ", "count=", "counter=", "elapsed=", "sample="}) {
        std::size_t search = 0;
        while ((search = lower.find(field, search)) != std::string::npos) {
            const auto begin = search + std::char_traits<char>::length(field);
            auto end = begin;
            while (end < lower.size() && std::isdigit(static_cast<unsigned char>(lower[end]))) ++end;
            if (end > begin && (end == lower.size() || lower[end] == ' ' || lower[end] == ',' ||
                                lower[end] == ';' || lower[end] == '\t' || lower[end] == 'm')) {
                lower.replace(begin, end - begin, "#"); search = begin + 1;
            } else search = begin;
        }
    }
    return lower;
}
inline std::string DiagnosticKey(const std::string &lower) {
    auto p = lower.find("reason=");
    if (p != std::string::npos) {
        const auto end = lower.find_first_of(" ,;\t", p + 7);
        return lower.substr(p, end == std::string::npos ? end : end - p);
    }
    std::string kind;
    if (lower.find("bypass transition") != std::string::npos || lower.find("transition") != std::string::npos)
        kind = "transition:";
    else if (lower.find("another reshade instance") != std::string::npos) kind = "duplicate-reshade:";
    else if (lower.find("device_removed") != std::string::npos || lower.find("device_hung") != std::string::npos ||
             lower.find("device removed") != std::string::npos || lower.find("device hung") != std::string::npos)
        kind = "device-failure:";
    else if (lower.find("[native-carrier]") != std::string::npos) kind = "native-carrier:";
    else if (lower.find("[native-contract]") != std::string::npos) kind = "native-contract:";
    else if (lower.find("fail") != std::string::npos || lower.find("error") != std::string::npos ||
             lower.find("unavailable") != std::string::npos || lower.find("unsupported") != std::string::npos ||
             lower.find("timeout") != std::string::npos || lower.find("rejected") != std::string::npos)
        kind = "failure:";
    else if (lower.find("feature 18 create") != std::string::npos) return "feature18-create";
    else return {};
    std::string shape = RepeatKey(lower);
    if (shape.size() > 192) shape.resize(192);
    return kind + shape;
}
struct Window {
    struct Entry { std::string text, key; std::uint64_t repeats = 0; };
    std::deque<Entry> lines;
    std::size_t bytes = 0;
    std::uint64_t omitted = 0, repetitions = 0;
    bool retains_oldest = false;
    static std::size_t Cost(const std::string &line) { return line.size() + 96; }
    void Push(std::string line, std::size_t limit, bool retain_oldest = false) {
        retains_oldest = retain_oldest;
        auto key = RepeatKey(line);
        if (!lines.empty() && lines.back().key == key) {
            ++lines.back().repeats; ++repetitions;
            if (!retain_oldest) {
                const auto old_cost = Cost(lines.back().text), new_cost = Cost(line);
                while (bytes - old_cost + new_cost > limit && lines.size() > 1) {
                    omitted += 1 + lines.front().repeats;
                    bytes -= Cost(lines.front().text); lines.pop_front();
                }
                if (bytes - old_cost + new_cost <= limit) {
                    bytes = bytes - old_cost + new_cost;
                    lines.back().text = std::move(line);
                }
            }
            return;
        }
        const auto cost = Cost(line);
        if (cost > limit) { ++omitted; return; }
        if (retain_oldest && bytes + cost > limit) { ++omitted; return; }
        while (bytes + cost > limit && !lines.empty()) {
            omitted += 1 + lines.front().repeats;
            bytes -= Cost(lines.front().text); lines.pop_front();
        }
        bytes += cost; lines.push_back({std::move(line), std::move(key), 0});
    }
    std::string Text() const {
        std::string out; out.reserve(bytes);
        for (const auto &entry : lines) {
            out += entry.text; out += '\n';
            if (entry.repeats) out += "[repetition.suppressed=" + std::to_string(entry.repeats) +
                "; representative=" + (retains_oldest ? "first" : "latest") + "-equivalent-line]\n";
        }
        return out;
    }
};
struct Summary {
    Window head;
    std::string first;
    Window recent, tail;
    std::vector<std::string> first_keys;
    std::uint64_t lines = 0, long_lines = 0, first_omitted = 0, first_truncated = 0;
    void Push(std::string_view raw, bool truncated = false) {
        ++lines; long_lines += truncated;
        auto line = Sanitize(raw);
        if (truncated) line += " [line truncated]";
        head.Push(line, kHeadLimit, true);
        tail.Push(line, kTailLimit);
        const auto lower = Lower(line);
        if (!DiagnosticLine(lower)) return;
        recent.Push(line, kRecentLimit);
        auto key = DiagnosticKey(lower);
        if (key.empty() || std::find(first_keys.begin(), first_keys.end(), key) != first_keys.end()) return;
        std::string excerpt = line;
        constexpr std::string_view marker = " [first-event excerpt truncated]";
        if (excerpt.size() > kFirstEventLimit) {
            excerpt.resize(kFirstEventLimit - marker.size()); excerpt += marker; ++first_truncated;
        }
        if (first_keys.size() >= 32 || first.size() + excerpt.size() + 1 > kFirstLimit) { ++first_omitted; return; }
        first_keys.push_back(std::move(key)); first += excerpt + '\n';
    }
    std::string Text(std::uint64_t source_bytes, std::uint64_t prefix_scanned,
                     std::uint64_t tail_scanned, bool complete, const char *status) const {
        return "format=DLSS5-LogSummary-v1\nsource.bytes=" + std::to_string(source_bytes) +
            "\nsummary.limitBytes=" + std::to_string(kSummaryLimit) +
            "\nscan.bytes=" + std::to_string(prefix_scanned + tail_scanned) +
            "\nscan.prefixBytes=" + std::to_string(prefix_scanned) +
            "\nscan.tailBytes=" + std::to_string(tail_scanned) +
            "\nscan.status=" + (status ? status : "unknown") + "\nscan.complete=" + (complete ? "1" : "0") +
            "\nlines.scanned=" + std::to_string(lines) + "\nlines.truncated=" + std::to_string(long_lines) +
            "\nhead.linesOmitted=" + std::to_string(head.omitted) +
            "\nhead.repetitionsSuppressed=" + std::to_string(head.repetitions) +
            "\nfirst.eventsOmitted=" + std::to_string(first_omitted) +
            "\nfirst.eventsTruncated=" + std::to_string(first_truncated) +
            "\nlatest.linesOmitted=" + std::to_string(recent.omitted) +
            "\nlatest.repetitionsSuppressed=" + std::to_string(recent.repetitions) +
            "\ntail.linesOmitted=" + std::to_string(tail.omitted) +
            "\ntail.repetitionsSuppressed=" + std::to_string(tail.repetitions) +
            "\nprivacy=ASCII excerpts; local paths and pointer-sized values redacted; hashes cover excerpts only\n"
            "\n[head]\n" + head.Text() + "\n[first distinct diagnostics]\n" + first +
            "\n[latest diagnostic transitions]\n" + recent.Text() + "\n[tail]\n" + tail.Text();
    }
    std::string Text(std::uint64_t source_bytes, std::uint64_t scanned, bool complete) const {
        return Text(source_bytes, scanned, 0, complete, complete ? "complete" : "bounded-incomplete");
    }
};
} // namespace nr_feedback_text

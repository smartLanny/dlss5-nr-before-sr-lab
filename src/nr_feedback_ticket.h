#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace nr_feedback_ticket {
constexpr std::size_t kCapacity = 4096;

// Only fixed, documented metadata keys leave the collector. Never include the
// raw report, a ZIP path, module paths, arbitrary logs, or live UI parameters.
inline void Value(const char *report, const char *key, char (&out)[160]) noexcept {
    std::snprintf(out, sizeof(out), "unavailable");
    if (!report) return;
    const std::size_t n = std::strlen(key);
    for (const char *line = report; *line;) {
        const char *end = std::strchr(line, '\n');
        if (!end) end = line + std::strlen(line);
        const std::size_t length = static_cast<std::size_t>(end - line);
        if (length > n && std::memcmp(line, key, n) == 0 && line[n] == '=') {
            const char *value = line + n + 1;
            std::size_t bytes = static_cast<std::size_t>(end - value);
            while (bytes && value[bytes - 1] == '\r') --bytes;
            if (!bytes || bytes >= sizeof(out)) return;
            // Metadata values should already be sanitized, but do not trust a
            // Windows/UNC/path-shaped or multiline value even on this path.
            for (std::size_t i = 0; i < bytes; ++i) {
                const unsigned char c = static_cast<unsigned char>(value[i]);
                if (c < 32 || c >= 127 || c == '\\' || c == '/' || c == ':') {
                    std::snprintf(out, sizeof(out), "redacted-or-unavailable"); return;
                }
            }
            std::memcpy(out, value, bytes); out[bytes] = 0; return;
        }
        if (!*end) break;
        line = end + 1;
    }
}

inline bool Build(char *out, std::size_t capacity, const char *report,
                   bool required_complete) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!report || !*report) return false;
    const int initial = std::snprintf(out, capacity,
        "DLSS5 feedback / 反馈摘要\nRequired core evidence: %s\n"
        "Recorded snapshot only; not proof of game compatibility.\n",
        required_complete ? "complete" : "partial (still useful)");
    if (initial < 0 || static_cast<std::size_t>(initial) >= capacity) { out[0]=0; return false; }
    std::size_t used = static_cast<std::size_t>(initial);
    const char *keys[] = {"session", "build", "artifact.sourceCommit", "artifact.sourceDirty",
        "artifact.addonSha256", "artifact.identityStatus", "exe.leaf", "gpu", "driver.raw",
        "api", "display.extent", "snapshot.valid", "requested.config", "requested.mode",
        "requested.enabled", "requested.blend", "start.bypass", "end.bypass",
        "start.nrSuccess", "end.nrSuccess", "start.transferSubmitted", "end.transferSubmitted"};
    for (const char *key : keys) {
        char value[160]; Value(report, key, value);
        const int n = std::snprintf(out + used, capacity - used, "%s=%s\n", key, value);
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - used) { out[0]=0; return false; }
        used += static_cast<std::size_t>(n);
    }
    const int final = std::snprintf(out + used, capacity - used,
        "\nSymptom / 问题现象：\nReproduction / 复现步骤：\n"
        "Attach this session's core ZIP. For visual issues, F7 creates a NEW core/image pair; attach both new ZIPs.\n"
        "请附本次核心包；画面异常用 F7 后，请改附新生成的一对核心包和图像包。不自动上传。\n");
    if (final < 0 || static_cast<std::size_t>(final) >= capacity - used) { out[0]=0; return false; }
    return true;
}
} // namespace nr_feedback_ticket

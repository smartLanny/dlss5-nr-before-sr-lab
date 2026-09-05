#include "nr_feedback_text.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace nr_feedback_text;
unsigned checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    check(CoreHotkey(117) == 119, "F6 cannot trigger both master and capture");
    check(CoreHotkey(118) == 119, "F7 belongs to image supplement");
    for (unsigned k = 0; k < 256; ++k) {
        auto out = CoreHotkey(k);
        check(out >= 112 && out <= 123 && out != 117 && out != 118, "bounded function key policy");
        if (k >= 112 && k <= 123 && k != 117 && k != 118) check(out == k, "valid custom key retained");
    }
    check(SessionId(42, 100, 1) != SessionId(42, 100, 2), "same-tick sessions never alias");
    check(SessionId(42, 100, 1) != SessionId(43, 100, 1), "process session identity");
    check(SessionId(0xffffffffu, UINT64_MAX, UINT64_MAX).size() < 96, "fixed session buffer sufficient");
    const std::string sha40(40, 'a'), sha64(64, 'b');
    auto text = Sanitize("ptr=00001234ABCDEF00 hr=0xC0000005 sha=" + sha40 + " sha256=" + sha64);
    check(text.find("ABCDEF00") == std::string::npos, "pointer redacted");
    check(text.find("0xC0000005") != std::string::npos, "HRESULT retained");
    check(text.find(sha40) != std::string::npos && text.find(sha64) != std::string::npos, "build hashes retained");
    check(Sanitize("addonPath=C:\\Users\\Private Name\\Game\\mod.addon64 | build=fixture") ==
        "addonPath=[local-path] | build=fixture", "entire spaced path redacted, next explicit field retained");
    check(Sanitize("file=\\\\server\\private share\\test") == "file=[local-path]", "UNC path redacted");
    check(Sanitize("Authorization: token example") == "[sensitive log line omitted]", "secret line excluded");
    check(Sanitize("x=one\xff\r\n") == "x=one?", "partial UTF8 and control bytes sanitized");
    check(Sanitize(std::string(kLineLimit * 2, 'x')).size() == kLineLimit, "oversized line bounded");
    Summary s;
    s.Push("build=fixture sourceCommit=" + sha40);
    for (unsigned i = 0; i < 20000; ++i) s.Push("frame=warming no carrier yet");
    s.Push("[command-state] NR bypass: reason=null-pso-policy-disabled token=unknown");
    s.Push("[command-state] NR bypass: reason=native-hooks-unavailable");
    s.Push("error first driver code=0xc0000005");
    for (unsigned i = 0; i < 30000; ++i) s.Push("Feature 18 evaluate success count=" + std::to_string(i));
    s.Push("last frame=50000");
    auto report = s.Text(4000000, 4000000, true);
    check(report.find("sourceCommit=" + sha40) != std::string::npos, "startup identity survives tail storm");
    check(report.find("reason=null-pso-policy-disabled") != std::string::npos, "first bypass survives tail storm");
    check(report.find("reason=native-hooks-unavailable") != std::string::npos, "distinct first reason retained");
    check(report.find("error first driver") != std::string::npos, "first error retained");
    check(report.find("last frame=50000") != std::string::npos, "recent tail retained");
    check(report.size() <= kSummaryLimit, "summary hard byte bound");
    for (unsigned i = 0; i < 200; ++i) s.Push("reason=unique-" + std::to_string(i) + std::string(4096, 'x'), true);
    check(s.first_keys.size() <= 32, "bounded reason index");
    report = s.Text(64000000, kScanLimit, false);
    check(report.find("scan.complete=0") != std::string::npos, "capped scan never claims complete first-event coverage");
    check(report.size() < kSummaryLimit && s.head.size() <= 32768 && s.first.size() <= 65536 &&
        s.recent.bytes <= 65536 && s.tail.bytes <= 65536, "all windows bounded under hostile lines");
    std::printf("feedback text policies: %u checks passed\n", checks);
}

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
    const char *abort_line = "WARN | Another ReShade instance was already loaded from 'C:\\Users\\PRIVATE\\Game\\dxgi.dll'! Aborting initialization ...";
    const auto redacted_abort = Sanitize(abort_line);
    check(redacted_abort.find("PRIVATE") == std::string::npos && redacted_abort.find("dxgi.dll") == std::string::npos,
        "duplicate instance private path and suffix still redacted");
    check(redacted_abort.find("Another ReShade instance") != std::string::npos &&
        redacted_abort.find("Aborting initialization") != std::string::npos,
        "fixed abort clue survives path-tail redaction");
    check(redacted_abort.size() <= kLineLimit, "redacted abort remains line-bounded");
    check(Sanitize("Another ReShade instance was loaded from C:\\PRIVATE\\x.dll").find("Aborting initialization") == std::string::npos,
        "missing abort clause is not invented");
    check(Sanitize(std::string(abort_line) + " password=private") == "[sensitive log line omitted]",
        "secret-line omission takes priority over event retention");
    Summary s;
    s.Push("build=fixture sourceCommit=" + sha40);
    for (unsigned i = 0; i < 20000; ++i) s.Push("frame=warming no carrier yet");
    s.Push("[command-state] NR bypass: reason=null-pso-policy-disabled token=unknown");
    s.Push("[command-state] NR bypass: reason=native-hooks-unavailable");
    s.Push("error first driver code=0xc0000005");
    s.Push(abort_line);
    for (unsigned i = 0; i < 30000; ++i) s.Push("Feature 18 evaluate success count=" + std::to_string(i));
    s.Push("last frame=50000");
    auto report = s.Text(4000000, 4000000, true);
    check(report.find("sourceCommit=" + sha40) != std::string::npos, "startup identity survives tail storm");
    check(report.find("reason=null-pso-policy-disabled") != std::string::npos, "first bypass survives tail storm");
    check(report.find("reason=native-hooks-unavailable") != std::string::npos, "distinct first reason retained");
    check(report.find("error first driver") != std::string::npos, "first error retained");
    check(report.find("Aborting initialization") != std::string::npos && report.find("PRIVATE") == std::string::npos,
        "sanitized initialization abort survives tail storm");
    check(report.find("last frame=50000") != std::string::npos, "recent tail retained");
    check(report.find("count=29999") != std::string::npos, "latest equivalent counter retained");
    check(report.find("repetition.suppressed=29999") != std::string::npos,
        "repetitive diagnostic counters have an explicit suppression count");
    check(report.find("latest.repetitionsSuppressed=29999") != std::string::npos,
        "summary metadata distinguishes selection from scan completeness");
    check(report.find("representative=first-equivalent-line") != std::string::npos &&
          report.find("representative=latest-equivalent-line") != std::string::npos,
        "compressed head and rolling windows label representative age truthfully");
    check(report.size() <= kSummaryLimit, "summary hard byte bound");
    for (unsigned i = 0; i < 200; ++i) s.Push("reason=unique-" + std::to_string(i) + std::string(4096, 'x'), true);
    check(s.first_keys.size() <= 32, "bounded reason index");
    report = s.Text(64000000, kScanLimit, false);
    check(report.find("scan.complete=0") != std::string::npos, "capped scan never claims complete first-event coverage");
    check(report.size() < kSummaryLimit && s.head.bytes <= kHeadLimit && s.first.size() <= kFirstLimit &&
        s.recent.bytes <= kRecentLimit && s.tail.bytes <= kTailLimit, "all windows bounded under hostile lines");
    Summary hostile;
    hostile.Push("build=fixture");
    hostile.Push("00:00:00 | error status=0xC0000005 device removed");
    hostile.Push("00:00:01 | error status=0x887A0006 device hung");
    for (unsigned i = 0; i < 250000; ++i)
        hostile.Push("2026-09-07T12:00:00 frame=" + std::to_string(i) + " Feature 18 evaluate success count=" + std::to_string(i));
    for (unsigned i = 0; i < 1000; ++i)
        hostile.Push(std::string("bypass transition reason=") + (i & 1 ? "depth-missing" : "native-restored"));
    hostile.Push("2026-09-07T12:00:01 frame=250000 Feature 18 evaluate success count=250000");
    hostile.Push("latest diagnostic marker reason=final-safe-bypass");
    const auto compact = hostile.Text(96ull * 1024 * 1024, kScanLimit, kTailScanLimit, false, "byte-limit");
    check(compact.find("0xC0000005") != std::string::npos && compact.find("0x887A0006") != std::string::npos,
        "distinct numeric failures are never normalized together");
    check(compact.find("reason=depth-missing") != std::string::npos &&
          compact.find("reason=native-restored") != std::string::npos &&
          compact.find("reason=final-safe-bypass") != std::string::npos,
        "first distinct reasons and final transition survive an adversarial log");
    check(compact.find("count=250000") != std::string::npos,
        "latest high-volume diagnostic counter survives compression");
    check(compact.find("scan.status=byte-limit") != std::string::npos &&
          compact.find("scan.prefixBytes=33554432") != std::string::npos &&
          compact.find("scan.tailBytes=65536") != std::string::npos,
        "prefix, tail and incomplete cause are explicit");
    check(compact.size() <= kSummaryLimit, "adversarial output obeys deterministic 24 KiB limit");
    check(RepeatKey("frame=1 status=7 format=10 blend=0.25") !=
          RepeatKey("frame=2 status=8 format=11 blend=0.5"),
        "volatile frame normalization preserves status, format and blend changes");
    check(RepeatKey("frame=1 hr=0x887A0005") != RepeatKey("frame=2 hr=0x887A0006"),
        "volatile frame normalization preserves HRESULT changes");
    check(RepeatKey("00:00:01 | status=7 blend=0.25") == RepeatKey("00:00:02 | status=7 blend=0.25") &&
          RepeatKey("00:00:01 | status=7 blend=0.25") != RepeatKey("00:00:02 | status=8 blend=0.25"),
        "timestamp-only repetition collapses while numeric state transitions remain distinct");
    Window annotation_bound;
    annotation_bound.Push("frame=1 count=1", 512);
    annotation_bound.Push("frame=2 count=2", 512);
    annotation_bound.lines.back().repeats = std::numeric_limits<std::uint64_t>::max();
    check(annotation_bound.Text().size() <= annotation_bound.bytes,
        "window accounting includes maximum repeat annotation width");
    Summary first_slots;
    first_slots.Push("error huge-first status=0xC0000005 " + std::string(4000, 'x'));
    first_slots.Push("device removed second-distinct status=0x887A0006 " + std::string(3000, 'y'));
    for (unsigned i = 0; i < 10000; ++i) first_slots.Push("Feature 18 evaluate success count=" + std::to_string(i));
    const auto slotted = first_slots.Text(1024 * 1024, 1024 * 1024, true);
    check(slotted.find("huge-first") != std::string::npos && slotted.find("second-distinct") != std::string::npos,
        "one oversized first error cannot monopolize distinct diagnostic evidence");
    check(slotted.find("first.eventsTruncated=2") != std::string::npos &&
          slotted.find("[first-event excerpt truncated]") != std::string::npos,
        "per-event clipping is explicit");
    std::printf("feedback text policies: %u checks passed\n", checks);
}

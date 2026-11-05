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
void timing_metadata_privacy() {
    const std::string ticks = "123456789012345";
    std::uint64_t parsed = 0;
    check(CanonicalUnsigned("0", &parsed) && parsed == 0, "canonical zero is a typed unsigned value");
    check(CanonicalUnsigned("18446744073709551615", &parsed) && parsed == UINT64_MAX,
        "canonical unsigned range includes UINT64_MAX");
    for (const auto invalid : {"", "00", "0123456789012345", "+123456789012345", "-123456789012345",
            "123456789012345 ", " 123456789012345", "123456789012345x", "0x123456789012345",
            "18446744073709551616", "999999999999999999999", "\xEF\xBC\x91\xEF\xBC\x92"}) {
        check(!CanonicalUnsigned(invalid), "typed exception rejects noncanonical ASCII uint64");
        const auto line = std::string("end.timing.capturedTicks=") + invalid;
        check(!TimingMetadataNumber(line, line.find('=') + 1, line.size()),
            "invalid uint64 cannot acquire the typed metadata exception");
    }
    for (unsigned digits = 11; digits <= 20; ++digits) {
        const std::string value = digits == 20 ? "18446744073709551615" : "1" + std::string(digits - 1, '2');
        for (const auto prefix : {"start.timing.", "end.timing."}) {
            for (const auto scalar : {"epoch", "probePhase", "waitPhase", "qpcFrequency", "observed",
                    "excludedOldEpoch", "recentOverwritten", "epochStartedTicks", "capturedTicks",
                    "scopedRecords", "unscopedRecords", "routeUnknownRecords"}) {
                const auto line = std::string(prefix) + scalar + "=" + value;
                check(SanitizeMetadata(line) == line, "typed metadata scalar is preserved at every decimal width");
                if (digits >= 12 && digits <= 16)
                    check(Sanitize(line) == std::string(prefix) + scalar + "=[address]",
                        "raw log lookalikes retain decimal address redaction");
            }
            for (const auto stage : {"carrier", "chain-lock", "queue-lock", "submitted-fence-wait",
                    "readback-map-copy", "nr-evaluate", "native-evaluate", "native-execute", "retirement-signal"}) {
                const auto line = std::string(prefix) + stage + "=count:" + value + ",measured:" + value +
                    ",totalTicks:" + value + ",maxTicks:" + value;
                check(SanitizeMetadata(line) == line, "typed stage statistics preserve each decimal width");
                std::string histogram;
                for (unsigned i = 0; i < 12; ++i) histogram += (i ? "," : "") + value;
                const auto full = line + ",histogram:" + histogram;
                check(SanitizeMetadata(full) == full, "exact twelve-bin histogram preserves typed decimal counts");
            }
            for (unsigned index = 0; index < 16; ++index) {
                const auto line = std::string(prefix) + "recent." + std::to_string(index) +
                    "=stage:carrier,sequence:" + value + ",epoch:" + value +
                    ",startedTicks:" + value + ",durationTicks:" + value;
                check(SanitizeMetadata(line) == line, "bounded recent typed measurements preserve decimal widths");
            }
        }
    }
    const auto mixed = "end.timing.recent.0=stage:carrier,ptr:0x123456789ABCDEF0,startedTicks:" + ticks +
        ",purePointer:0000123412341234,durationTicks:" + ticks + ",fakeStartedTicks:" + ticks;
    check(SanitizeMetadata(mixed) == "end.timing.recent.0=stage:carrier,ptr:[address],startedTicks:" + ticks +
          ",purePointer:[address],durationTicks:" + ticks + ",fakeStartedTicks:[address]",
        "extra and forged fields on the same timing line retain pointer privacy");
    for (const auto prefix : {"unknown=", "end.timing.note=", "gpu=end.timing.capturedTicks=",
            "fake.end.timing.capturedTicks=", "end.timing.capturedTicks.fake=",
            "end.timing.recent.16=stage:carrier,startedTicks:",
            "end.timing.recent.00=stage:carrier,startedTicks:",
            "end.timing.recent.0=stage:unknown,startedTicks:",
            "end.timing.recent.0=stage:carrier,notstartedTicks:",
            "end.timing.carrier=notTotalTicks:", "end.timing.unknown-stage=totalTicks:",
            "end.timing.aggregateScope=device:", "end.timing.recent.0=stage:carrier,device:"}) {
        check(SanitizeMetadata(std::string(prefix) + ticks) == std::string(prefix) + "[address]",
            "unlisted schema tokens never receive a numeric exception");
    }
    check(SanitizeMetadata("end.timing.capturedTicks=0x123456789ABCDEF0") == "end.timing.capturedTicks=[address]",
        "typed field name never exempts a hexadecimal pointer");
    check(SanitizeMetadata("end.timing.capturedTicks=0000123412341234") == "end.timing.capturedTicks=[address]",
        "zero-padded pointer is not canonical decimal metadata");
    check(SanitizeMetadata("end.timing.carrier=count:" + ticks + ",count:" + ticks) ==
        "end.timing.carrier=count:[address],count:[address]", "duplicate typed fields fail closed");
    check(SanitizeMetadata("end.timing.capturedTicks=" + ticks + ",extra:" + ticks) ==
        "end.timing.capturedTicks=[address],extra:[address]", "scalar exception requires the whole scalar value");
    check(SanitizeMetadata("end.timing.carrier=histogram:" + ticks) == "end.timing.carrier=histogram:[address]",
        "histogram requires its exact typed twelve-bin tail");
    const auto private_path = "end.timing.recent.0=stage:carrier,startedTicks:" + ticks +
        ",owner:C:\\fixture-private\\game\\file | ptr=0x123456789ABCDEF0";
    check(SanitizeMetadata(private_path) == "end.timing.recent.0=stage:carrier,startedTicks:" + ticks +
        ",owner:[local-path] | ptr=[address]", "timing exception preserves independent path and pointer redaction");
    for (const auto secret : {"Authorization: fixture", "password=fixture", "access_token=fixture",
            "github_pat_fixture", "ghp_fixture", "private key fixture"}) {
        check(SanitizeMetadata("end.timing.capturedTicks=" + ticks + " | " + secret) ==
            "[sensitive log line omitted]", "credential omission still takes priority over typed metadata");
    }
}
void aux_metadata_privacy() {
    const auto record = [](const std::string &key, const std::string &value) {
        return key + "=calls:" + value + ",failures:" + value + ",measured:" + value +
            ",totalTicks:" + value + ",maxTicks:" + value;
    };
    for (unsigned digits = 11; digits <= 20; ++digits) {
        const std::string value = digits == 20 ? "18446744073709551615" : "1" + std::string(digits - 1, '2');
        for (const auto prefix : {"start.auxCreate.", "end.auxCreate."}) {
            const auto frequency = std::string(prefix) + "qpcFrequency=" + value;
            check(SanitizeMetadata(frequency) == frequency, "aux QPC frequency preserves unsigned decimal widths");
            for (unsigned shader = 0; shader < 9; ++shader)
                for (const auto operation : {"compile", "pso"}) {
                    const auto line = record(std::string(prefix) + std::to_string(shader) + "." + operation, value);
                    check(SanitizeMetadata(line) == line, "every production aux record preserves all five numeric fields");
                    if (digits >= 12 && digits <= 16)
                        check(Sanitize(line).find(value) == std::string::npos, "raw aux log lookalikes still redact decimal addresses");
                }
        }
    }
    const std::string ticks = "123456789012345";
    for (const auto key : {"fake.end.auxCreate.8.pso", "gpu=end.auxCreate.8.pso", "end.auxCreate.9.pso",
            "end.auxCreate.00.pso", "end.auxCreate.8.unknown", "end.auxCreate.8.pso.fake", "end.auxCreate.note"})
        check(SanitizeMetadata(record(key, ticks)).find(ticks) == std::string::npos,
            "unknown aux keys cannot grant numeric exemptions");
    const auto valid = record("end.auxCreate.8.pso", ticks);
    for (const auto suffix : {",fakeTicks:", ",totalTicks:", ",ptr:"})
        check(SanitizeMetadata(valid + suffix + ticks).find(ticks) == std::string::npos,
            "extra or duplicate aux fields fail closed for the whole malformed record");
    for (const auto pointer : {"0x123456789ABCDEF0", "0000123412341234"})
        check(SanitizeMetadata(record("end.auxCreate.8.pso", pointer)).find(pointer) == std::string::npos,
            "typed aux fields never exempt hexadecimal or zero-padded pointers");
    check(SanitizeMetadata("end.auxCreate.8.pso=totalTicks:" + ticks) ==
        "end.auxCreate.8.pso=totalTicks:[address]", "partial aux records cannot acquire a typed exemption");
    check(SanitizeMetadata(valid + ",").find(ticks) == std::string::npos,
        "aux numeric schema rejects a trailing comma");
    check(SanitizeMetadata("end.auxCreate.qpcFrequency=" + ticks + ",extra:" + ticks).find(ticks) == std::string::npos,
        "aux frequency exception requires the whole scalar value");
    for (const auto invalid : {"", "00", "-123456789012345", "+123456789012345", "18446744073709551616"}) {
        const auto line = record("end.auxCreate.8.pso", invalid);
        const auto begin = line.find(':') + 1;
        check(!AuxCreateMetadataNumber(line, begin, line.find(',', begin)), "noncanonical aux values cannot acquire an exemption");
    }
    check(SanitizeMetadata(valid + " | password=fixture") == "[sensitive log line omitted]",
        "secret omission retains priority over typed aux metadata");
    check(SanitizeMetadata("end.auxCreate.note=C:\\fixture-private\\file | ptr=0x123456789ABCDEF0") ==
        "end.auxCreate.note=[local-path] | ptr=[address]", "aux metadata retains independent path and pointer privacy");
}
int main() {
    timing_metadata_privacy();
    aux_metadata_privacy();
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
    check(RepeatKey("status=7 | frame=1") != RepeatKey("status=8 | frame=2") &&
          RepeatKey("error 7: detail | frame=1") != RepeatKey("error 8: detail | frame=2"),
        "numeric diagnostic prefix is not mistaken for a timestamp");
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

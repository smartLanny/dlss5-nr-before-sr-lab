#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
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
inline bool CanonicalUnsigned(std::string_view value, std::uint64_t *parsed = nullptr) {
    if (value.empty() || value.size() > 20 || (value.size() > 1 && value.front() == '0')) return false;
    std::uint64_t number = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<unsigned>(c - '0');
        if (number > (UINT64_MAX - digit) / 10) return false;
        number = number * 10 + digit;
    }
    if (parsed) *parsed = number;
    return true;
}
inline bool TimingStageKey(std::string_view key) {
    for (const auto stage : {"carrier", "chain-lock", "queue-lock", "submitted-fence-wait",
            "readback-map-copy", "nr-model-create", "nr-evaluate", "native-evaluate", "native-execute", "retirement-signal"})
        if (key == stage) return true;
    return false;
}
inline bool BoundaryMetadataNumber(std::string_view line,std::size_t begin,std::size_t end) {
    // These exact scalar fields contain source-written counters or semantic
    // lifetime tokens. Never exempt raw logs, event text or arbitrary suffixes.
    const auto equal=line.find('=');
    if(equal==std::string_view::npos || begin!=equal+1 || end!=line.size() ||
       !CanonicalUnsigned(line.substr(begin,end-begin)))return false;
    auto key=line.substr(0,equal);
    for(const auto prefix:{std::string_view("start.format.output.freshGate."),std::string_view("end.format.output.freshGate.")}) {
        if(key.substr(0,prefix.size())!=prefix)continue;
        const auto suffix=key.substr(prefix.size());
        for(const auto scalar:{"config","feature","deviceId","resourceId","preparedCall","currentCall","requestCall"})
            if(suffix==scalar)return true;
        return false;
    }
    if(key.substr(0,20)=="start.post.boundary.")key.remove_prefix(20);
    else if(key.substr(0,18)=="end.post.boundary.")key.remove_prefix(18);
    else return false;
    for(const auto scalar:{"frame","config","featureGeneration","ageMs","trace","probePhase","device","target",
        "subresource","storeTrace","storeFrame","storeProbePhase","recoveryAttempt","recoveryCarrier",
        "recoveryOrdinal","nativeUseCall","nativeEventEntries","nativeMatches",
        "nativeOtherListMatches","followingBarriers","readBarriers","writeBarriers","copyReads","copyWrites",
        "renderTargetBinds","nativeExactMatches","nativeExactOtherLists","nativeGlobals","nativeUnknownOperations",
        "exactFollowing","unknownOperations","nativeDropped","followingDropped"})
        if(key==scalar)return true;
    return false;
}
inline bool TimingMetadataNumber(std::string_view line, std::size_t begin, std::size_t end) {
    // Only the typed measurement/counter fields written by FeedbackAppendTiming
    // may exempt a decimal token from the address heuristic. Object identities,
    // arbitrary timing text, unknown keys and raw log lines keep that heuristic.
    if (begin >= end || end > line.size() || !CanonicalUnsigned(line.substr(begin, end - begin))) return false;
    const auto equal = line.find('=');
    if (equal == std::string_view::npos || begin <= equal) return false;
    auto key = line.substr(0, equal);
    if (key.substr(0, 13) == "start.timing.") key.remove_prefix(13);
    else if (key.substr(0, 11) == "end.timing.") key.remove_prefix(11);
    else return false;
    for (const auto scalar : {"epoch", "probePhase", "waitPhase", "qpcFrequency", "observed",
            "excludedOldEpoch", "recentOverwritten", "epochStartedTicks", "capturedTicks",
            "scopedRecords", "unscopedRecords", "routeUnknownRecords"}) {
        if (key == scalar) return begin == equal + 1 && end == line.size();
    }
    const bool stage = TimingStageKey(key);
    bool recent = false;
    if (key.substr(0, 7) == "recent.") {
        std::uint64_t index = 0;
        recent = CanonicalUnsigned(key.substr(7), &index) && index < 16;
    }
    if (!stage && !recent) return false;
    const auto body = line.substr(equal + 1);
    if (recent) {
        const auto comma = body.find(',');
        if (body.substr(0, 6) != "stage:" || comma == std::string_view::npos ||
            !TimingStageKey(body.substr(6, comma - 6))) return false;
    }
    // Histogram numbers are a typed twelve-element tail, not arbitrary text
    // after a "histogram" substring. Reject malformed/extra/overflow elements.
    if (stage) {
        const auto histogram = body.find(",histogram:");
        if (histogram != std::string_view::npos && begin >= equal + 1 + histogram + 11) {
            std::size_t at = equal + 1 + histogram + 11;
            unsigned count = 0;
            bool found = false;
            while (at < line.size()) {
                auto next = line.find(',', at);
                if (next == std::string_view::npos) next = line.size();
                if (!CanonicalUnsigned(line.substr(at, next - at))) return false;
                found = found || (at == begin && next == end);
                if (++count > 12) return false;
                if (next == line.size()) return found && count == 12;
                at = next + 1;
            }
            return false;
        }
    }
    const auto comma = line.rfind(',', begin - 1);
    const auto field_begin = comma == std::string_view::npos || comma < equal ? equal + 1 : comma + 1;
    const auto colon = line.find(':', field_begin);
    auto field_end = line.find(',', field_begin);
    if (field_end == std::string_view::npos) field_end = line.size();
    if (colon == std::string_view::npos || begin != colon + 1 || end != field_end) return false;
    const auto field = line.substr(field_begin, colon - field_begin);
    bool allowed = false;
    if (stage) allowed = field == "count" || field == "measured" || field == "totalTicks" || field == "maxTicks";
    else allowed = field == "sequence" || field == "epoch" || field == "startedTicks" || field == "durationTicks";
    if (!allowed) return false;
    // A second occurrence is malformed schema, even if it has a trusted name.
    unsigned matches = 0;
    for (std::size_t at = equal + 1; at < line.size();) {
        const auto next = line.find(',', at);
        const auto finish = next == std::string_view::npos ? line.size() : next;
        if (finish - at > field.size() && line.substr(at, field.size()) == field && line[at + field.size()] == ':')
            ++matches;
        if (next == std::string_view::npos) break;
        at = next + 1;
    }
    return matches == 1;
}
inline bool UiActivityMetadataNumber(std::string_view line, std::size_t begin, std::size_t end) {
    if(begin>=end || end>line.size() || !CanonicalUnsigned(line.substr(begin,end-begin)))return false;
    const auto equal=line.find('=');
    if(equal==std::string_view::npos || begin<=equal)return false;
    auto key=line.substr(0,equal);
    if(key.substr(0,17)=="start.uiActivity.")key.remove_prefix(17);
    else if(key.substr(0,15)=="end.uiActivity.")key.remove_prefix(15);
    else return false;
    for(const auto scalar:{"snapshotTick","published","overwritten"})
        if(key==scalar)return begin==equal+1 && end==line.size();
    bool record=key=="latest.top" || key=="latest.feedback";
    if(key.substr(0,7)=="recent.") {
        std::uint64_t index=0;
        record=CanonicalUnsigned(key.substr(7),&index) && index<4;
    }
    if(!record)return false;
    // Complete fixed record grammar. No free-form field, pointer, extra tuple
    // element or duplicate trusted label inherits a numeric exemption.
    struct Field { std::string_view name; unsigned count; };
    constexpr Field fields[]={{"seq",1},{"thread",1},{"call",1},{"site",1},{"atMs",1},
        {"readGap",2},{"windowGap",2},{"scopeBefore",2},{"readScope",5},{"reset",1},
        {"regressed",1},{"observe",1},{"kind",1},{"initialized",1},{"valid",2},{"sampleMs",1},
        {"counterBefore",4},{"counterNow",4},{"delta",4},{"reason",2},{"event",1},{"display",4}};
    std::size_t at=equal+1;bool found=false;
    for(unsigned field=0;field<std::size(fields);++field) {
        const auto name=fields[field].name;
        if(line.substr(at,name.size())!=name || at+name.size()>=line.size() || line[at+name.size()]!=':')return false;
        at+=name.size()+1;
        for(unsigned component=0;component<fields[field].count;++component) {
            auto finish=at;
            while(finish<line.size() && line[finish]>='0' && line[finish]<='9')++finish;
            if(!CanonicalUnsigned(line.substr(at,finish-at)))return false;
            found=found || (at==begin && finish==end);
            at=finish;
            if(component+1<fields[field].count) {
                if(at>=line.size() || line[at++]!='/')return false;
            }
        }
        if(field+1<std::size(fields)) {
            if(at>=line.size() || line[at++]!=',')return false;
        }
    }
    return found && at==line.size();
}
inline bool DiagnosticMetadataNumber(std::string_view line,std::size_t begin,std::size_t end) {
    // Only complete, source-written menu/ledger records may carry decimal
    // counters that resemble pointers. Unknown keys or malformed records keep
    // the ordinary address heuristic, including for an otherwise valid field.
    const auto equal=line.find('=');
    if(equal==std::string_view::npos || begin<=equal || begin>=end || end>line.size() ||
       !CanonicalUnsigned(line.substr(begin,end-begin)))return false;
    const auto key=line.substr(0,equal);
    struct Cursor {
        std::string_view line;
        std::size_t at,begin,end;
        bool found=false;
        bool Literal(std::string_view value) {
            if(line.substr(at,value.size())!=value)return false;
            at+=value.size();return true;
        }
        bool Number(std::uint64_t maximum=UINT64_MAX,bool nonzero=false) {
            const auto first=at;
            while(at<line.size() && line[at]>='0' && line[at]<='9')++at;
            std::uint64_t value=0;
            if(!CanonicalUnsigned(line.substr(first,at-first),&value) || value>maximum || (nonzero && !value))return false;
            found|=first==begin && at==end;
            return true;
        }
        bool Boolean(){return Number(1);}
        bool MaybeNumber(std::uint64_t maximum=UINT64_MAX) {
            return Literal("unknown") || Number(maximum);
        }
        bool Extent() {
            return Literal("unknown") || (Number(UINT32_MAX,true) && Literal("x") && Number(UINT32_MAX,true));
        }
        bool Done() const {return at==line.size() && found;}
    } c{line,equal+1,begin,end};
    const auto one_of=[](std::string_view value,std::initializer_list<std::string_view> choices){
        for(const auto choice:choices)if(value==choice)return true;
        return false;
    };
    if(key=="menuTrace.windowStartMs" || key=="menuTrace.windowDeadlineMs")
        return c.Number() && c.Done();
    constexpr std::string_view event_prefix="menuTrace.event.";
    if(key.substr(0,event_prefix.size())==event_prefix) {
        auto suffix=key.substr(event_prefix.size());
        const auto dot=suffix.find('.');
        const auto index_text=suffix.substr(0,dot);
        std::uint64_t index=0;
        if(!CanonicalUnsigned(index_text,&index) || index>=64)return false;
        if(dot!=std::string_view::npos) {
            suffix.remove_prefix(dot+1);
            return (suffix=="lastMs" ? c.Number() :
                    suffix=="coalescedCount" ? c.Number(UINT32_MAX) : false) && c.Done();
        }
        if(!c.Literal("ms:") || !c.Number() || !c.Literal(" kind:"))return false;
        constexpr std::string_view kinds[]={"esc","first","change","resource-rotation","create-succeeded",
            "create-failed","retire","reset-requested","evaluate-failed","bypass","guide-rejected"};
        bool kind_found=false;
        for(const auto kind:kinds) {
            if(c.line.substr(c.at,kind.size())==kind &&
               c.line.substr(c.at+kind.size(),3)==" R:") {
                c.at+=kind.size();kind_found=true;break;
            }
        }
        if(!kind_found || !c.Literal(" R:") || !c.Extent() ||
           !c.Literal(" N:") || !c.Extent() || !c.Literal(" O:") || !c.Extent() ||
           !c.Literal(" feature:") || !c.MaybeNumber(UINT32_MAX) ||
           !c.Literal(" featureGen:") || !c.MaybeNumber() ||
           !c.Literal(" instance:") || !c.MaybeNumber() ||
           !c.Literal(" layer:") || !c.MaybeNumber(UINT32_MAX) ||
           !c.Literal(" route:") || !c.MaybeNumber(UINT32_MAX) ||
           !c.Literal(" config:") || !c.MaybeNumber(UINT32_MAX) ||
           !c.Literal(" history:") || !c.MaybeNumber(UINT32_MAX) ||
           !c.Literal(" sourceStream:") || !c.MaybeNumber() ||
           !c.Literal(" sourceEpoch:") || !c.MaybeNumber() ||
           !c.Literal(" sourceFrame:") || !c.MaybeNumber() ||
           !c.Literal(" frameMask:") || !c.Number(UINT32_MAX) ||
           !c.Literal(" storageMask:") || !c.Number(UINT32_MAX) ||
           !c.Literal(" mappingMask:") || !c.Number(UINT32_MAX) ||
           !c.Literal(" resourceMask:") || !c.Number(UINT32_MAX) ||
           !c.Literal(" reason:") || !c.Number(UINT32_MAX) ||
           !c.Literal(" reset:") || !c.Boolean())return false;
        return c.Done();
    }
    std::string_view allocation;
    if(key.substr(0,17)=="start.allocation.")allocation=key.substr(17);
    else if(key.substr(0,15)=="end.allocation.")allocation=key.substr(15);
    else return false;
    if(one_of(allocation,{"sampleStartedTickMs","sampleFinishedTickMs","knownBytesLowerBound"}))
        return c.Number() && c.Done();
    const auto category_end=allocation.find('.');
    if(category_end==std::string_view::npos)return false;
    const auto category=allocation.substr(0,category_end);
    if(!one_of(category,{"coreSlot","experiment","capture","dual","privateSR","temporal","pool","compat","fg"}))return false;
    allocation.remove_prefix(category_end+1);
    const auto phase_end=allocation.find('.');
    const auto phase=allocation.substr(0,phase_end);
    if(!one_of(phase,{"live","retired","pending","destroying"}))return false;
    if(phase_end!=std::string_view::npos) {
        allocation.remove_prefix(phase_end+1);
        if(allocation.substr(0,5)!="heap.")return false;
        allocation.remove_prefix(5);
        if(!one_of(allocation,{"default","readback","upload","other"}))return false;
    }
    return c.Literal("bytes:") && c.Number() &&
        c.Literal(",resources:") && c.Number(UINT32_MAX) &&
        c.Literal(",unknownGroups:") && c.Number(UINT32_MAX) &&
        c.Literal(",overflow:") && c.Boolean() && c.Done();
}
inline std::string RedactSensitiveLine(std::string line, bool typed_metadata = false) {
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
            if (typed_metadata && UiActivityMetadataNumber(line, i, end)) { i = end; continue; }
            if (typed_metadata && BoundaryMetadataNumber(line, i, end)) { i = end; continue; }
            if (typed_metadata && TimingMetadataNumber(line, i, end)) { i = end; continue; }
            if (typed_metadata && DiagnosticMetadataNumber(line, i, end)) { i = end; continue; }
            line.replace(i, end - i, "[address]"); i += 9;
        } else ++i;
    }
    // ReShade places the abort text AFTER the loaded module's private path.
    // Preserve only a fixed marker, never reconstruct any removed suffix.
    // This remains a historical text clue, not a trusted/current state claim.
    if (duplicate_init_abort && Lower(line).find("aborting initialization") == std::string::npos) {
        constexpr std::string_view marker = " [Aborting initialization]";
        if (line.size() > kLineLimit - marker.size()) {
            auto end = kLineLimit - marker.size();
            while (end && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) --end;
            line.resize(end);
        }
        line += marker;
    }
    return line;
}
inline std::string Sanitize(std::string_view input) {
    // Raw log excerpts retain the established ASCII normalization policy.
    std::string line;
    line.reserve(std::min(input.size(), kLineLimit));
    for (char c : input.substr(0, kLineLimit)) {
        const auto u = static_cast<unsigned char>(c);
        if (u == '\r' || u == '\n') continue;
        line += u >= 32 && u < 127 ? c : (u == '\t' ? ' ' : '?');
    }
    return RedactSensitiveLine(std::move(line));
}
inline std::string SanitizeMetadata(std::string_view input) {
    // Metadata writers supply complete lines. Preserve valid UTF-8 scalars,
    // normalize controls/invalid input, then apply the log privacy rules with
    // exact typed timing-number exceptions. Never split a UTF-8 character.
    constexpr std::string_view marker = "[truncated]";
    constexpr std::size_t payload = kLineLimit - marker.size();
    std::string line;
    line.reserve(std::min(input.size(), kLineLimit));
    for (std::size_t offset = 0; offset < input.size();) {
        const auto first = static_cast<unsigned char>(input[offset]);
        if (first == '\r' || first == '\n') { ++offset; continue; }
        std::size_t width = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 :
            first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
        if (width > input.size() - offset) width = 0;
        for (std::size_t n = 1; n < width; ++n) {
            const auto byte = static_cast<unsigned char>(input[offset + n]);
            if (byte < 0x80 || byte > 0xBF) { width = 0; break; }
        }
        if (width > 1) {
            const auto second = static_cast<unsigned char>(input[offset + 1]);
            if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
                (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90)) width = 0;
        }
        const bool control = first < 0x20 || first == 0x7F ||
            (width == 2 && first == 0xC2 && static_cast<unsigned char>(input[offset + 1]) <= 0x9F) ||
            (width == 3 && first == 0xE2 && static_cast<unsigned char>(input[offset + 1]) == 0x80 &&
             (static_cast<unsigned char>(input[offset + 2]) == 0xA8 || static_cast<unsigned char>(input[offset + 2]) == 0xA9));
        const auto bytes = width && !control ? width : std::size_t{1};
        if (bytes > payload - line.size()) { line += marker; break; }
        if (!width || control) line += first == '\t' ? ' ' : '?';
        else line.append(input.data() + offset, width);
        offset += width ? width : 1;
    }
    line = RedactSensitiveLine(std::move(line), true);
    // Replacing very short repeated paths with a fixed privacy marker may grow
    // the line. Preserve the same bound after redaction as well.
    if (line.size() > kLineLimit) {
        auto end = payload;
        while (end && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) --end;
        line.resize(end);
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

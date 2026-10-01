#include <cassert>
#include <cstdio>
#include <string>
#include "nr_feedback_text.h"

using nr_feedback_text::Sanitize;
using nr_feedback_text::SanitizeMetadata;

static std::string Event(const std::string& n) {
    return "menuTrace.event.1=ms:"+n+
        " kind:change R:3840x2160 N:1920x1080 O:3840x2160 feature:13 featureGen:"+n+
        " instance:"+n+
        " layer:0 route:2 config:1 history:1 sourceStream:"+n+
        " sourceEpoch:"+n+" sourceFrame:"+n+
        " frameMask:0 storageMask:0 mappingMask:0 resourceMask:0 reason:0 reset:0";
}
static std::string Row(const std::string& n) {
    return "start.allocation.coreSlot.live.heap.default=bytes:"+n+
        ",resources:1,unknownGroups:0,overflow:0";
}
static void Redacted(const std::string& line) {
    assert(SanitizeMetadata(line).find("[address]")!=std::string::npos);
}
int main() {
    for(const std::string n:{"123456789012","1234567890123","12345678901234",
                             "123456789012345","1234567890123456"}) {
        for(const auto& line:{"menuTrace.windowStartMs="+n,"menuTrace.windowDeadlineMs="+n,
                "menuTrace.event.1.lastMs="+n,Event(n),Row(n),
                "end.allocation.fg.destroying=bytes:"+n+",resources:1,unknownGroups:0,overflow:1",
                "start.allocation.knownBytesLowerBound="+n,
                "end.allocation.sampleStartedTickMs="+n,
                "end.allocation.sampleFinishedTickMs="+n})
            assert(SanitizeMetadata(line)==line);
        // Raw log lines never gain typed exemptions.
        Redacted("menuTrace.windowStartMs="+n+" extra=1");
        assert(Sanitize(Event(n)).find("[address]")!=std::string::npos);
        Redacted("menuTrace.event.64.lastMs="+n);
        Redacted("menuTrace.event.64="+Event(n).substr(Event(n).find('=')+1));
        Redacted(Event(n)+" carrier_key:"+n);
        Redacted(Event(n)+" ms:"+n);
        Redacted("menuTrace.event.1="+Event(n).substr(Event(n).find('=')+1).replace(0,0,"unexpected "));
        auto bad_kind=Event(n);
        bad_kind.replace(bad_kind.find("kind:change"),11,"kind:unknown");
        Redacted(bad_kind);
        auto bad_extent=Event(n);
        bad_extent.replace(bad_extent.find("R:3840x2160"),11,"R:0x2160");
        Redacted(bad_extent);
        Redacted(Event(n).substr(0,Event(n).rfind('0'))+"2");
        Redacted("start.allocation.unknown.live=bytes:"+n+",resources:1,unknownGroups:0,overflow:0");
        Redacted("start.allocation.coreSlot.unknown=bytes:"+n+",resources:1,unknownGroups:0,overflow:0");
        Redacted("start.allocation.coreSlot.live.heap.unknown=bytes:"+n+",resources:1,unknownGroups:0,overflow:0");
        Redacted(Row(n)+",pointer:"+n);
        Redacted("start.allocation.coreSlot.live.heap.default=resources:1,bytes:"+n+",unknownGroups:0,overflow:0");
        Redacted("start.allocation.coreSlot.live.heap.default=bytes:"+n+",resources:1,unknownGroups:0,overflow:0,bytes:"+n);
        Redacted("start.allocation.sampleStartedTickMs=0x"+n);
    }
    auto unknown=Event("123456789012");
    for(const auto field:{"R:3840x2160","N:1920x1080","O:3840x2160","instance:123456789012",
                          "sourceStream:123456789012","sourceEpoch:123456789012","sourceFrame:123456789012"}) {
        const auto pos=unknown.find(field);
        const auto colon=std::string(field).find(':');
        unknown.replace(pos,std::string(field).size(),std::string(field).substr(0,colon+1)+"unknown");
    }
    assert(SanitizeMetadata(unknown)==unknown);
    assert(SanitizeMetadata("menuTrace.event.1.lastMs=000123456789012").find("[address]")!=std::string::npos);
    assert(SanitizeMetadata("menuTrace.windowStartMs=123456789012 | C:\\Private\\path").find("[local-path]")!=std::string::npos);
    assert(SanitizeMetadata("menuTrace.windowStartMs=123456789012 password=secret")=="[sensitive log line omitted]");
    std::puts("PASS: strict menu/ledger decimal schemas and raw-log privacy");
}

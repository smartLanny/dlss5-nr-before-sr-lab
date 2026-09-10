#pragma once
#include <cstdint>

// Value-only ABI for a bounded, diagnostic UNORM storage comparison. The first
// forty words remain the existing model and unique-expectation statistics.
namespace nr_store_comparison {
constexpr unsigned kLegacyWords=40,kHeaderWords=48,kHeaderBytes=192,kPointBytes=8,kMaxPoints=65535;
constexpr std::uint32_t kProtocol=0x4e525531u; // NRU1; mirrors the diagnostic HLSL.
enum Flags : std::uint32_t {
    Capacity=1u,MalformedRange=2u,HeaderMismatch=4u,CoverageIncomplete=8u,
    InvalidInputs=16u,InvalidCounters=32u,UnsupportedFormat=64u
};
struct Counters {
    std::uint32_t protocol=0,written=0,compared=0,mismatched=0,missing=0,invalid_expected=0,invalid_actual=0,flags=0;
};
static_assert(sizeof(Counters)==8*sizeof(std::uint32_t));
inline bool Unorm(unsigned kind) { return kind==2 || kind==3; }
inline unsigned CapacityForExtent(std::uint64_t width,std::uint64_t height,unsigned kind) {
    if(!Unorm(kind) || !width || !height)return 0;
    const auto columns=width/32+(width%32!=0),rows=height/32+(height%32!=0);
    if(columns>kMaxPoints || rows>kMaxPoints || columns>kMaxPoints/rows)return kMaxPoints;
    return static_cast<unsigned>(columns*rows);
}
inline unsigned PlannedSamples(std::uint64_t width,std::uint64_t height) {
    if(!width || !height)return 0;
    const auto columns=width/32+(width%32!=0),rows=height/32+(height%32!=0);
    if(columns>kMaxPoints || rows>kMaxPoints || columns>kMaxPoints/rows)return 0;
    return static_cast<unsigned>(columns*rows);
}
inline unsigned BufferWords(unsigned capacity) {
    return kHeaderWords+(capacity<=kMaxPoints?capacity:0)*2;
}
struct Result {bool known=false,match=false;std::uint32_t flags=0;};
inline Result Classify(unsigned kind,unsigned planned,unsigned capacity,unsigned actual,
        bool finite_inputs,const Counters &counts) {
    Result result;result.flags=counts.flags;
    if(!Unorm(kind))result.flags|=UnsupportedFormat;
    if(!planned || planned>kMaxPoints || capacity>kMaxPoints || planned>capacity)result.flags|=Capacity;
    if(counts.protocol!=kProtocol)result.flags|=HeaderMismatch;
    if(actual!=planned || counts.written!=planned || counts.compared!=planned || counts.missing)
        result.flags|=CoverageIncomplete;
    if(!finite_inputs || counts.invalid_expected || counts.invalid_actual)result.flags|=InvalidInputs;
    if(counts.mismatched>counts.compared || counts.compared>planned || counts.written>planned ||
       counts.missing>planned || counts.invalid_expected>planned || counts.invalid_actual>planned)
        result.flags|=InvalidCounters;
    result.known=result.flags==0;
    result.match=result.known && counts.mismatched==0;
    return result;
}
inline const char *MethodName(unsigned method) {
    return method==1?"unorm-legal-code-interval":"legacy-unique-value-hash";
}
}

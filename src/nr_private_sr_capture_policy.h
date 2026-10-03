#pragma once
#include <cstdint>

// Local capture limits and sequence proof; this does not admit rendering work.
namespace nr_private_sr_capture {
constexpr std::uint64_t kResidentCap=8ull<<30,kDiskCap=8ull<<30;
constexpr std::uint64_t kFrameCap=512ull<<20,kPhysicalReserve=4ull<<30;
constexpr std::uint64_t kDiskReserve=1ull<<30;
constexpr unsigned kAttempts=64,kSessions=8,kFrames=32,kPlanes=10;
struct Sequence {
    std::uint64_t stream=0,epoch=0,sequence=0;
};
constexpr bool Adjacent(Sequence a,Sequence b) {
    return a.stream&&a.epoch&&a.sequence&&b.stream==a.stream&&b.epoch==a.epoch&&
        a.sequence!=UINT64_MAX&&b.sequence==a.sequence+1;
}
constexpr bool Fits(std::uint64_t used,std::uint64_t add,std::uint64_t cap) {
    return used<=cap&&add<=cap-used;
}
constexpr bool CompletionProof(bool submitted,bool released,std::uint64_t queue,
    std::uint64_t recording_queue,std::uint64_t fence_target,std::uint64_t completed) {
    return submitted&&released&&queue&&queue==recording_queue&&fence_target&&
        completed!=UINT64_MAX&&completed>=fence_target;
}
struct Series {
    Sequence first{};
    unsigned requested=0,seen=0;
    std::uint32_t present=0;
    bool failed=false;
    // Completion order can differ from source order. Every position must have
    // its own successful pixels and the exact source identity, once only.
    bool Observe(unsigned index,Sequence source,bool pixels_valid) {
        if(!requested||requested>kFrames||!index||index>requested||
           !first.stream||!first.epoch||!first.sequence||
           first.sequence>UINT64_MAX-(index-1)||source.stream!=first.stream||
           source.epoch!=first.epoch||source.sequence!=first.sequence+(index-1)) {
            failed=true;return false;
        }
        const auto bit=std::uint32_t(1)<<(index-1);
        if(present&bit){failed=true;return false;}
        present|=bit;++seen;failed|=!pixels_valid;return true;
    }
    bool Complete()const {
        if(!requested||requested>kFrames)return false;
        const auto mask=requested==32?UINT32_MAX:requested?((std::uint32_t(1)<<requested)-1):0;
        return !failed&&seen==requested&&present==mask;
    }
};
}

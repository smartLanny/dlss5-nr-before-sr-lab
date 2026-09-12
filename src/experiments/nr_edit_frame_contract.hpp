#pragma once
#include <cstdint>
#include <limits>

namespace nr_edit {
// Value receipts supplied by the existing Core source/queue/lifetime observers.
// This is not a provider, ownership registry, synchronization primitive or NGX ABI.
struct FrameKey {
    std::uint64_t stream=0, epoch=0, source=0, view=0;
    std::uint64_t device=0, queue=0, recording=0, recording_generation=0;
    std::uint64_t output_lifetime=0, configuration=0, color_domain=0;
    std::uint32_t render_width=0, render_height=0, output_width=0, output_height=0;
    explicit operator bool() const noexcept {
        return stream&&epoch&&source&&view&&device&&queue&&recording&&
            recording_generation&&output_lifetime&&configuration&&color_domain&&
            render_width&&render_height&&output_width&&output_height;
    }
    bool operator==(const FrameKey&) const = default;
};
enum class PairResult { Empty, PreFailed, Mismatch, NativeFailed, Ready, Consumed };
// The first post-boundary attempt consumes the CPU pairing ticket even when
// it fails. No GPU resource is released or reused by this object.
class SameFramePair {
    FrameKey key_{};
    bool armed_=false;
    PairResult status_=PairResult::Empty;
public:
    void Begin() noexcept { key_={};armed_=false;status_=PairResult::Empty; }
    bool Arm(const FrameKey& key,bool pre_nr_succeeded) noexcept {
        Begin();
        if(!key||!pre_nr_succeeded){status_=PairResult::PreFailed;return false;}
        key_=key;armed_=true;return true;
    }
    PairResult Consume(const FrameKey& key,bool native_succeeded) noexcept {
        if(!armed_)return status_==PairResult::Empty?status_:PairResult::Consumed;
        armed_=false;
        if(!(key==key_))return status_=PairResult::Mismatch;
        if(!native_succeeded)return status_=PairResult::NativeFailed;
        return status_=PairResult::Ready;
    }
    PairResult Status() const noexcept{return status_;}
};
struct HistoryReceipt {
    std::uint64_t stream=0,epoch=0,source=0,view=0,device=0,configuration=0,color_domain=0;
    std::uint32_t width=0,height=0;
    bool finite_exposure=false;
    explicit operator bool() const noexcept {
        return stream&&epoch&&source&&view&&device&&configuration&&color_domain&&
            width&&height&&finite_exposure;
    }
};
inline bool HistoryMatches(const HistoryReceipt& previous,
        const HistoryReceipt& current,std::uint64_t motion_reference_source,
        bool source_pair_verified,bool camera_cut) noexcept {
    // Gaps are legal only if motion explicitly references the last accepted
    // residual frame. "Last captured" and "last processed" are not interchangeable.
    return previous&&current&&source_pair_verified&&!camera_cut&&
        current.source>previous.source&&motion_reference_source==previous.source&&
        current.stream==previous.stream&&current.epoch==previous.epoch&&
        current.view==previous.view&&current.device==previous.device&&
        current.configuration==previous.configuration&&
        current.color_domain==previous.color_domain&&
        current.width==previous.width&&current.height==previous.height;
}
struct FenceReceipt {
    std::uint64_t queue=0,fence=0,value=0;
    bool submitted=false,poisoned=false;
};
struct FenceCompletion {
    std::uint64_t queue=0,fence=0,value=0;
};
inline bool CanRetire(const FenceReceipt& receipt,const FenceCompletion& done) noexcept {
    return receipt.queue&&receipt.fence&&receipt.value&&receipt.submitted&&!receipt.poisoned&&
        done.queue==receipt.queue&&done.fence==receipt.fence&&
        done.value!=std::numeric_limits<std::uint64_t>::max()&&done.value>=receipt.value;
}
// Runtime integration must use the Core's stronger recording-reference and
// exact-queue predicates as well. CanRetire alone never authorizes destruction.
} // namespace nr_edit

#pragma once
#include "nr_auto_injection.hpp"

// Executable boundary dispatcher. The Core supplies three REAL callback pairs;
// absent callbacks stay unregistered rather than showing a fake working mode.
// No SR evaluation or Present call is owned by this layer.
namespace nr_auto_injection {
enum class RecordKind { RejectedBeforePrivateWork, RecordedPrivateWork, OpaqueFailure };
struct RecordResult {RecordKind kind=RecordKind::RejectedBeforePrivateWork;Fault reason=Fault::None;};
struct Provider {
    void* user=nullptr;
    Candidate (*probe)(void*,const Frame&) noexcept=nullptr; // metadata only
    RecordResult (*record)(void*,const Frame&,const Receipt&) noexcept=nullptr;
};
struct DispatchResult {
    enum class Kind { NotSelectedHere, Native, Recorded, Fatal } kind=Kind::Native;
    Point point=Point::None;
    Fault reason=Fault::None;
    Receipt receipt{};
    bool reset_history=false,synthetic=false;
};
class Dispatcher {
    Policy policy_{};
    std::array<Provider,4> providers_{};
public:
    void Set(Point point, Provider p) noexcept {
        if(Valid(point) && point!=Point::None)providers_[static_cast<unsigned>(point)]=p;
    }
    Policy& State() noexcept {return policy_;}
    DispatchResult At(Point boundary,const Frame& frame) noexcept {
        if(!Valid(boundary) || boundary==Point::None)
            return {DispatchResult::Kind::Native,Point::None,Fault::ProviderMissing,{},false,false};
        std::array<Candidate,4> available{};
        for(unsigned i=1;i<4;++i) {
            const auto& p=providers_[i];
            // Early boundaries that already passed cannot be selected using
            // a remembered texture. Later providers may be proposed/deferred.
            if(i<static_cast<unsigned>(boundary)||!p.probe||!p.record)continue;
            available[i]=p.probe(p.user,frame);available[i].registered=true;
        }
        const auto choice=policy_.Choose(frame,available);
        if(!choice)return {Hard(choice.reason)?DispatchResult::Kind::Fatal:DispatchResult::Kind::Native,
            Point::None,choice.reason,{},false,false};
        if(choice.point!=boundary)return {DispatchResult::Kind::NotSelectedHere,choice.point,Fault::None,{},choice.reset_history,choice.synthetic};
        const auto receipt=policy_.Begin(frame,choice);
        if(!receipt)return {DispatchResult::Kind::Native,Point::None,Fault::Busy,{},false,false};
        auto& provider=providers_[static_cast<unsigned>(boundary)];
        const auto result=provider.record(provider.user,frame,receipt);
        if(result.kind==RecordKind::RejectedBeforePrivateWork) {
            // No loop evaluating another NR provider at this same boundary.
            // A later boundary may acquire the released same-source claim.
            policy_.RejectBeforeWork(receipt,result.reason);
            return {Hard(result.reason)?DispatchResult::Kind::Fatal:DispatchResult::Kind::Native,
                boundary,result.reason,{},choice.reset_history,choice.synthetic};
        }
        if(result.kind==RecordKind::OpaqueFailure) {
            policy_.Complete(receipt,true,false,Hard(result.reason)?result.reason:Fault::OpaqueCall);
            return {DispatchResult::Kind::Fatal,boundary,Fault::OpaqueCall,receipt,choice.reset_history,choice.synthetic};
        }
        policy_.Recorded(receipt);
        return {DispatchResult::Kind::Recorded,boundary,Fault::None,receipt,choice.reset_history,choice.synthetic};
    }
};
} // namespace nr_auto_injection

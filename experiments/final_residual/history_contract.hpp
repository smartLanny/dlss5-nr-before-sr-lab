#pragma once
#include <cstdint>
#include <limits>

// Project-owned transaction policy. No shader, optical-flow implementation,
// SDK type, resource pointer or copied upstream implementation lives here.
namespace nr::final_residual {
using Id = std::uint64_t;
struct Source {
    Id stream=0, epoch=0, sequence=0;
    bool valid() const noexcept { return stream&&epoch&&sequence; }
};
inline bool operator==(Source a,Source b) noexcept {
    return a.stream==b.stream&&a.epoch==b.epoch&&a.sequence==b.sequence;
}
inline bool same_stream(Source a,Source b) noexcept {
    return a.stream==b.stream&&a.epoch==b.epoch;
}
enum class Backend { None, AmdOpticalFlow, NvidiaOpticalFlow };
struct Contract {
    Id device_generation=0, config_generation=0, color_domain=0;
    unsigned width=0,height=0;
    Backend backend=Backend::None;
    bool valid() const noexcept {
        return device_generation&&config_generation&&color_domain&&width&&height&&
            (backend==Backend::AmdOpticalFlow||backend==Backend::NvidiaOpticalFlow);
    }
};
inline bool operator==(const Contract& a,const Contract& b) noexcept {
    return a.device_generation==b.device_generation&&a.config_generation==b.config_generation&&
        a.color_domain==b.color_domain&&a.width==b.width&&a.height==b.height&&a.backend==b.backend;
}
struct Frame {
    Source source{};
    Contract contract{};
    Id source_time=0; // Caller-selected monotonic source time; not a Present counter.
    bool scene_reset=false;
    bool valid() const noexcept {return source.valid()&&contract.valid()&&source_time;}
};
enum class Action { Invalid, Busy, Duplicate, Stale, Quarantined, ResetBoth, Continue };
struct Plan {
    Action action=Action::Invalid;
    Id token=0, history_generation=0;
    Source reference{};
    bool work() const noexcept {return token&&(action==Action::ResetBoth||action==Action::Continue);}
};
enum class FlowStatus { Pending, Ready, Failed, SubmitFailed, DeviceLost };
struct FlowReceipt {
    FlowStatus status=FlowStatus::Pending;
    Backend backend=Backend::None;
    Id history_generation=0;
    Source current{}, reference{};
    bool reset_executed=false;
};
enum class FlowDecision { Invalid, Wait, Seed, Accumulate, ResetRequired, Quarantined };
enum class ResidualStatus { NotSubmitted, SubmittedNotAdopted, SubmittedAdopted, SubmitFailed, DeviceLost };
enum class Completion { Invalid, Adopted, ResetRequired, Quarantined };

// One instance belongs to one serialized chain owner. Caller must hold its
// source/queue lock across policy transactions. A token is never a GPU fence.
class HistoryContract {
    Frame adopted_{}, observed_{};
    Frame pending_{};
    Plan plan_{};
    Id generation_=1,next_token_=1;
    bool valid_=false,flow_started_=false,flow_ready_=false,quarantined_=false;
    void close() noexcept {pending_={};plan_={};flow_started_=flow_ready_=false;}
    void invalidate() noexcept {
        valid_=false;
        if(generation_==std::numeric_limits<Id>::max())quarantined_=true;
        else ++generation_;
    }
    void failed() noexcept {valid_=false;quarantined_=true;close();}
    bool owns(Id token) const noexcept {return token&&plan_.token==token;}
public:
    bool has_history() const noexcept {return valid_;}
    bool quarantined() const noexcept {return quarantined_;}
    Id history_generation() const noexcept {return generation_;}
    Source reference() const noexcept {return valid_?adopted_.source:Source{};}

    Plan prepare(const Frame& next) noexcept {
        if(quarantined_)return {Action::Quarantined};
        if(plan_.token)return {Action::Busy};
        if(!next.valid())return {};
        if(observed_.source.valid()&&same_stream(next.source,observed_.source)){
            if(next.source.sequence==observed_.source.sequence)return {Action::Duplicate};
            if(next.source.sequence<observed_.source.sequence)return {Action::Stale};
        }
        const bool continuous=valid_&&!next.scene_reset&&same_stream(next.source,adopted_.source)&&
            adopted_.source.sequence!=std::numeric_limits<Id>::max()&&
            next.source.sequence==adopted_.source.sequence+1&&
            next.source_time>adopted_.source_time&&next.contract==adopted_.contract;
        if(valid_&&!continuous)invalidate();
        if(quarantined_||next_token_==std::numeric_limits<Id>::max()){
            failed();return {Action::Quarantined};
        }
        pending_=next;
        plan_={continuous?Action::Continue:Action::ResetBoth,next_token_++,generation_,
            continuous?adopted_.source:Source{}};
        return plan_;
    }

    // Only legal before invoking OF. If OF was invoked, report its result and
    // finish with NotSubmitted instead, because its private reference may move.
    bool cancel_before_flow(Id token) noexcept {
        if(!owns(token)||flow_started_)return false;
        close();return true;
    }

    FlowDecision observe_flow(Id token,const FlowReceipt& flow) noexcept {
        if(!owns(token)||flow_ready_)return FlowDecision::Invalid;
        flow_started_=true;
        if(flow.status==FlowStatus::Pending)return FlowDecision::Wait;
        observed_=pending_; // This OF attempt may have advanced private history.
        if(flow.status==FlowStatus::DeviceLost||flow.status==FlowStatus::SubmitFailed){
            failed();return FlowDecision::Quarantined;
        }
        const bool reset=plan_.action==Action::ResetBoth;
        if(flow.status!=FlowStatus::Ready||flow.backend!=pending_.contract.backend||
           flow.history_generation!=plan_.history_generation||!(flow.current==pending_.source)||
           !(flow.reference==plan_.reference)||flow.reset_executed!=reset){
            invalidate();close();return FlowDecision::ResetRequired;
        }
        flow_ready_=true;
        return reset?FlowDecision::Seed:FlowDecision::Accumulate;
    }

    // Called only after the consumer's real record/submit outcome is known.
    // SubmittedAdopted means exact source work was accepted on the ordered
    // queue. Allocation reuse/destruction still requires the caller's fences.
    Completion finish(Id token,Id residual_history_generation,ResidualStatus status) noexcept {
        if(!owns(token)||!flow_ready_)return Completion::Invalid;
        if(status==ResidualStatus::DeviceLost||status==ResidualStatus::SubmitFailed){
            failed();return Completion::Quarantined;
        }
        if(status!=ResidualStatus::SubmittedAdopted||residual_history_generation!=plan_.history_generation){
            invalidate();close();return Completion::ResetRequired;
        }
        adopted_=pending_;valid_=true;close();return Completion::Adopted;
    }
};
}

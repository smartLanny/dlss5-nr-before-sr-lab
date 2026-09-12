#pragma once
#include <array>
#include <cstdint>
#include <limits>

// Pure selection / ownership bookkeeping. This does NOT turn an unavailable
// provider into a working provider. All admission bits must come from the real
// current resource/queue/source contract. Caller serializes one instance/view.
namespace nr_auto_injection {
enum class Request : unsigned { Auto, Before, After, Present, Off };
enum class Point : unsigned { None, Before, After, Present };
inline bool Valid(Point p) noexcept { return static_cast<unsigned>(p)<=3; }
inline bool Valid(Request r) noexcept { return static_cast<unsigned>(r)<=4; }
enum class Fault : unsigned {
    None, Installing, ProviderMissing, CommandState, QueueUnknown, Format,
    Guides, CurrentSourceUnknown, OwnerConflict, Busy, RecordedNotVisible,
    ProvenOverwrite, ResourceUnavailable, OpaqueCall, DeviceLost,
    SubmissionUnknown, LifecyclePoison
};
enum class Phase : unsigned { Off, Waiting, Proposed, Recorded, Confirmed, Poisoned };
inline bool Hard(Fault f) noexcept {
    return f == Fault::OpaqueCall || f == Fault::DeviceLost ||
           f == Fault::SubmissionUnknown || f == Fault::LifecyclePoison;
}
inline const char* Name(Point p) noexcept {
    switch (p) {
    case Point::Before: return "before-reconstruction";
    case Point::After: return "after-reconstruction";
    case Point::Present: return "present";
    default: return "native";
    }
}
inline const char* Name(Request r, bool zh = false) noexcept {
    switch (r) {
    case Request::Auto: return zh ? "自动" : "Auto";
    case Request::Before: return zh ? "渲染前" : "Before reconstruction";
    case Request::After: return zh ? "超分后" : "After reconstruction";
    case Request::Present: return zh ? "画面输出" : "Frame output";
    default: return zh ? "关闭" : "Off";
    }
}
inline const char* Name(Fault f) noexcept {
    switch (f) {
    case Fault::None:return "none";
    case Fault::Installing:return "installing-not-terminal";
    case Fault::ProviderMissing:return "provider-not-registered";
    case Fault::CommandState:return "inline-state-unavailable";
    case Fault::QueueUnknown:return "queue-order-unproven";
    case Fault::Format:return "unsupported-resource-contract";
    case Fault::Guides:return "temporal-guides-unavailable";
    case Fault::CurrentSourceUnknown:return "source-frame-or-view-unproven";
    case Fault::OwnerConflict:return "another-nr-or-present-owner";
    case Fault::Busy:return "in-flight-capacity";
    case Fault::RecordedNotVisible:return "write-observed-display-unproven";
    case Fault::ProvenOverwrite:return "same-source-consumer-overwrite-proven";
    case Fault::ResourceUnavailable:return "resource-budget-unavailable";
    case Fault::OpaqueCall:return "opaque-runtime-fault";
    case Fault::DeviceLost:return "device-removed";
    case Fault::SubmissionUnknown:return "submission-completion-unproven";
    case Fault::LifecyclePoison:return "lifecycle-poison";
    }
    return "unknown";
}
struct Migrated { Request request = Request::Auto; bool accepted = true; };
// New key InjectionPoint v1: 0 Auto,1 Before,2 After,3 Present,4 Off.
// Never reinterpret legacy Mode=1 as the new Before value.
inline Migrated Migrate(bool new_key_present, unsigned new_value,
                        bool enabled, bool old_mode_present, unsigned old_mode) noexcept {
    if (new_key_present) {
        if (new_value > 4) return {Request::Off, false};
        return {enabled ? static_cast<Request>(new_value) : Request::Off, true};
    }
    if (!enabled) return {Request::Off,true};
    if (!old_mode_present) return {};
    if (old_mode == 1) return {Request::After,true};
    if (old_mode == 2) return {Request::Before,true};
    return {Request::Off,false};
}
struct Stream {
    std::uint64_t device=0, device_epoch=0, swapchain=0, view=0, config=0, owner_epoch=0;
    bool operator==(const Stream& b) const noexcept {
        return device==b.device && device_epoch==b.device_epoch &&
               swapchain==b.swapchain && view==b.view && config==b.config && owner_epoch==b.owner_epoch;
    }
    explicit operator bool() const noexcept {
        // All three boundaries must share the SAME presentation lineage.
        // Before=0 followed by Present=real-swapchain would evade source claims.
        return device && device_epoch && swapchain && view && config && owner_epoch;
    }
};
struct Frame {
    Stream stream{};
    // A monotonic source sequence supplied by a verified producer contract,
    // NOT Present count, texture address, same jitter or wall-clock proximity.
    std::uint64_t source=0;
    bool source_known=false, application_frame=false, rr=false;
    bool fg=false, master=true;
    Request request=Request::Auto;
    Fault global_fault=Fault::None; // current Core-wide latch, independent of entry point
};
struct Candidate {
    bool registered=false, input_current=false, resource_valid=false;
    bool same_device=false, queue_ordered=false, unique_owner=false;
    bool inline_state_restorable=false, independent_recording=false;
    bool native_guides=false, synthetic_guides=false, allow_synthetic=false;
    bool app_frame_proven=false, fg_contract_proven=false;
    Fault blocked=Fault::None;
};
inline Fault Admissible(const Frame& f, Point p, const Candidate& c) noexcept {
    if (!Valid(p) || p==Point::None || !c.registered) return Fault::ProviderMissing;
    if (c.blocked != Fault::None) return c.blocked;
    if (!c.unique_owner) return Fault::OwnerConflict;
    if (!c.input_current || !c.app_frame_proven) return Fault::CurrentSourceUnknown;
    if (!c.same_device || !c.queue_ordered) return Fault::QueueUnknown;
    if (!c.resource_valid) return Fault::Format;
    if (!c.inline_state_restorable && !c.independent_recording) return Fault::CommandState;
    if (!c.native_guides && !(c.synthetic_guides && c.allow_synthetic)) return Fault::Guides;
    if (f.fg && !c.fg_contract_proven) return Fault::CurrentSourceUnknown;
    if (p == Point::Before && f.rr) return Fault::Guides; // do not replace RR's input contract
    if (p == Point::Present && (!f.stream.swapchain || !c.independent_recording)) return Fault::ProviderMissing;
    return Fault::None;
}
struct Receipt {
    Stream stream{};
    std::uint64_t source=0, attempt=0;
    Point point=Point::None;
    bool reset_history=false,synthetic=false;
    explicit operator bool() const noexcept { return attempt && source && point!=Point::None; }
};
struct Decision {
    Point point=Point::None;
    Fault reason=Fault::None;
    bool reset_history=false, require_feedback=false, synthetic=false;
    std::uint64_t source=0;
    explicit operator bool() const noexcept { return point!=Point::None; }
};
struct Snapshot {
    Phase phase=Phase::Waiting;
    Point proposed=Point::None, recorded=Point::None, confirmed=Point::None;
    Fault reason=Fault::None;
    std::uint64_t source=0, attempted=0, completed=0, fallback_changes=0;
    std::uint64_t recorded_source=0, confirmed_source=0;
    bool poisoned=false, needs_f8=false;
};
class Policy {
    struct Health { unsigned misses=0, successes=0; std::uint64_t retry_at=0; };
    struct Pending { Receipt receipt{}; bool occupied=false; std::uint64_t previous_claim=0; bool recorded=false; };
    struct Claim {std::uint64_t device=0,epoch=0,swapchain=0,view=0,source=0;};
    std::array<Claim,16> claims_{}; // config edits never erase same-source ownership
    Claim* ClaimFor(const Stream& s,bool create=false) noexcept {
        Claim* empty=nullptr;
        for(auto& c:claims_) {
            if(c.device==s.device&&c.epoch==s.device_epoch&&c.swapchain==s.swapchain&&c.view==s.view)return &c;
            if(!c.device&&!empty)empty=&c;
        }
        if(create&&empty){*empty={s.device,s.device_epoch,s.swapchain,s.view,0};return empty;}
        return nullptr;
    }
    bool Claimed(const Frame& f) noexcept {
        const auto* c=ClaimFor(f.stream);return c&&c->source>=f.source;
    }
    Stream stream_{};
    bool stream_valid_=false;
    std::uint64_t source_=0, last_claim_=0, next_attempt_=1;
    struct PoisonEntry {std::uint64_t device=0,epoch=0;Fault reason=Fault::None;};
    std::array<PoisonEntry,8> poison_{};
    bool poison_overflow_=false;
    Point preferred_=Point::None, last_recorded_=Point::None;
    std::array<Health,4> health_{};
    std::array<Pending,32> pending_{};
    Snapshot value_{};
    static unsigned Index(Point p) noexcept { return static_cast<unsigned>(p); }
    static std::uint64_t Add(std::uint64_t a,std::uint64_t b) noexcept {
        return a>UINT64_MAX-b ? UINT64_MAX : a+b;
    }
    void Miss(Point p,Fault why) noexcept {
        if (!Valid(p) || p==Point::None) return;
        auto& h=health_[Index(p)];
        if (why==Fault::Installing || why==Fault::ProviderMissing || why==Fault::RecordedNotVisible) return;
        h.successes=0;
        if(h.misses<32)++h.misses;
        // Only changes route selection. This NEVER permits stale state use or
        // holds an old NR image to conceal a missing frame.
        if (why==Fault::Format || why==Fault::ProvenOverwrite || h.misses>=3) {
            const unsigned shift=h.misses>7 ? 5 : (h.misses>2 ? h.misses-2 : 0);
            h.retry_at=Add(source_,std::uint64_t(4)<<shift);
            if(preferred_==p)preferred_=Point::None;
        }
    }
    void Enter(const Frame& f) noexcept {
        if(!stream_valid_ || !(stream_==f.stream)) {
            // Configuration/view change invalidates selection receipts, not an
            // opaque poison on this device generation.
            stream_=f.stream;stream_valid_=true;source_=0;last_claim_=0;
            preferred_=last_recorded_=Point::None;health_={};
            value_={};
        }
        source_=f.source;value_.source=f.source;
    }
    void Poison(const Stream& s,Fault f) noexcept {
        bool stored=false;
        for(auto& entry:poison_)if((entry.device==s.device&&entry.epoch==s.device_epoch)||!entry.device) {
            entry={s.device,s.device_epoch,f};stored=true;break;
        }
        if(!stored)poison_overflow_=true;
        if(stream_.device==s.device&&stream_.device_epoch==s.device_epoch) {
            value_.phase=Phase::Poisoned;value_.poisoned=true;value_.reason=f;value_.needs_f8=true;
        }
    }
public:
    Snapshot Read() const noexcept { return value_; }
    Decision Choose(const Frame& f, const std::array<Candidate,4>& c) noexcept {
        if(!Valid(f.request)) {
            value_.phase=Phase::Waiting;value_.reason=Fault::ProviderMissing;value_.needs_f8=true;
            return {Point::None,Fault::ProviderMissing,false,true,false,f.source};
        }
        if (!f.master || f.request==Request::Off) {
            value_.phase=Phase::Off;return {};
        }
        if(!f.stream || !f.source || !f.source_known || !f.application_frame) {
            value_.phase=Phase::Waiting;value_.reason=Fault::CurrentSourceUnknown;value_.needs_f8=true;
            return {Point::None,Fault::CurrentSourceUnknown,false,true,false,f.source};
        }
        const auto poison=PoisonFor(f.stream);
        if(poison!=Fault::None) {
            value_.phase=Phase::Poisoned;value_.poisoned=true;value_.reason=poison;value_.needs_f8=true;
            return {Point::None,poison,false,true,false,f.source};
        }
        if(stream_valid_ && stream_==f.stream && f.source<source_)
            return {Point::None,Fault::CurrentSourceUnknown,false,true,false,f.source};
        Enter(f);
        if(f.global_fault!=Fault::None) {
            if(Hard(f.global_fault))Poison(f.stream,f.global_fault);
            else {value_.phase=Phase::Waiting;value_.reason=f.global_fault;value_.needs_f8=true;}
            return {Point::None,f.global_fault,false,true,false,f.source};
        }
        if(last_claim_==f.source || Claimed(f)) return {Point::None,Fault::Busy,false,false,false,f.source};
        std::array<Point,3> order{Point::Before,Point::After,Point::Present};
        if(f.rr)order={Point::After,Point::Present,Point::None};
        if(f.request!=Request::Auto) {
            Point p=f.request==Request::Before?Point::Before:f.request==Request::After?Point::After:Point::Present;
            order={p,Point::None,Point::None};
        } else if(preferred_!=Point::None) {
            // Keep an established viable fallback. Availability flapping does
            // not promote back to the early hook every other frame.
            for(unsigned i=0;i<order.size();++i) if(order[i]==preferred_) {
                auto p=order[i];for(unsigned j=i;j>0;--j)order[j]=order[j-1];order[0]=p;break;
            }
        }
        Fault last=Fault::ProviderMissing;
        for(Point p:order) {
            if(p==Point::None)continue;
            const auto why=Admissible(f,p,c[Index(p)]);
            if(Hard(why)){Poison(f.stream,why);return {Point::None,why,false,true,false,f.source};}
            if(why==Fault::OwnerConflict) {
                value_.phase=Phase::Waiting;value_.reason=why;value_.needs_f8=true;
                return {Point::None,why,false,true,false,f.source};
            }
            if(why!=Fault::None){last=why;continue;}
            if(f.request==Request::Auto && source_<health_[Index(p)].retry_at){last=Fault::Busy;continue;}
            value_.phase=Phase::Proposed;value_.proposed=p;value_.reason=Fault::None;
            return {p,Fault::None,p!=last_recorded_,false,!c[Index(p)].native_guides && c[Index(p)].synthetic_guides,f.source};
        }
        value_.phase=Phase::Waiting;value_.reason=last;value_.needs_f8=true;
        return {Point::None,last,false,true,false,f.source};
    }
    // Reserve ownership BEFORE the first opaque Create/Evaluate or private
    // GPU write, even when no final edit will be adopted during warmup.
    Receipt Begin(const Frame& f,const Decision& d) noexcept {
        if(!stream_valid_||!(stream_==f.stream)||source_!=f.source||!d||d.source!=source_||
           value_.proposed!=d.point||last_claim_==source_||Claimed(f)||value_.poisoned)return {};
        Pending* slot=nullptr;
        for(auto& p:pending_)if(!p.occupied){slot=&p;break;}
        if(!slot){value_.reason=Fault::Busy;return {};}
        if(next_attempt_==UINT64_MAX){Poison(f.stream,Fault::SubmissionUnknown);return {};}
        auto* claim=ClaimFor(f.stream,true);
        if(!claim){value_.reason=Fault::Busy;value_.needs_f8=true;return {};}
        Receipt r{f.stream,source_,next_attempt_++,d.point,d.reset_history,d.synthetic};
        *slot={r,true,claim->source};claim->source=source_;
        last_claim_=source_;value_.attempted=r.attempt;
        return r;
    }
    // Only an admission rejection BEFORE any opaque/private work may release
    // the source claim for another boundary in the SAME frame.
    void RejectBeforeWork(const Receipt& r,Fault why) noexcept {
        auto* p=Find(r);if(!p)return;
        // Once recorded, no caller may downgrade the receipt to a pre-work rejection.
        // Keep the claim forever for this source and poison the execution generation.
        if(p->recorded) {Poison(r.stream,Fault::SubmissionUnknown);p->occupied=false;return;}
        if(Hard(why)){Poison(r.stream,why);p->occupied=false;return;}
        if(auto* c=ClaimFor(r.stream);c&&c->source==r.source)c->source=p->previous_claim;
        if(stream_==r.stream && source_==r.source) {
            last_claim_=0;Miss(r.point,why);value_.reason=why;value_.needs_f8=true;
        }
        p->occupied=false;
    }
    void Recorded(const Receipt& r) noexcept {
        auto* pending=Find(r);if(!pending)return;
        pending->recorded=true;
        if(stream_==r.stream && r.source>=value_.recorded_source) {
            value_.recorded_source=r.source;
            if(last_recorded_!=Point::None&&last_recorded_!=r.point)++value_.fallback_changes;
            last_recorded_=r.point;value_.recorded=r.point;value_.phase=Phase::Recorded;
        }
    }
    void Complete(const Receipt& r, bool exact_completion, bool edit_adopted, Fault failure=Fault::None) noexcept {
        auto* p=Find(r);if(!p)return;
        const bool was_recorded=p->recorded;
        p->occupied=false;
        if(Hard(failure) || !exact_completion) {
            Poison(r.stream,Hard(failure)?failure:Fault::SubmissionUnknown);return;
        }
        if(!was_recorded) {Poison(r.stream,Fault::SubmissionUnknown);return;}
        if(!(stream_==r.stream))return; // late old-generation proof cannot relock
        // Completion delivery can be out of order even when GPU work was ordered.
        // Never label an older completion as proof for the latest source or route.
        if(r.source<value_.confirmed_source)return;
        if(failure!=Fault::None){Miss(r.point,failure);value_.reason=failure;value_.needs_f8=true;return;}
        if(!edit_adopted){value_.phase=Phase::Recorded;return;} // Create/warmup is not visible adoption
        auto& h=health_[Index(r.point)];h.misses=0;h.retry_at=0;
        if(h.successes<3)++h.successes;
        ++value_.completed;value_.confirmed=r.point;value_.confirmed_source=r.source;
        value_.phase=Phase::Confirmed;
        if(h.successes>=3)preferred_=r.point;
    }
    // Explicit non-mutating post-consumer evidence, not luma/brightness guesses.
    void ProvenConsumerOverwrite(Point p) noexcept {Miss(p,Fault::ProvenOverwrite);value_.needs_f8=true;}
private:
    Fault PoisonFor(const Stream& s) const noexcept {
        if(poison_overflow_)return Fault::LifecyclePoison;
        for(const auto& p:poison_)if(p.device==s.device&&p.epoch==s.device_epoch)return p.reason;
        return Fault::None;
    }
    Pending* Find(const Receipt& r) noexcept {
        if(!r)return nullptr;
        for(auto& p:pending_)if(p.occupied && p.receipt.attempt==r.attempt &&
            p.receipt.source==r.source && p.receipt.point==r.point && p.receipt.reset_history==r.reset_history &&
            p.receipt.synthetic==r.synthetic && p.receipt.stream==r.stream)return &p;
        return nullptr;
    }
};
} // namespace nr_auto_injection

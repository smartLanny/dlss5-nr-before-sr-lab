#pragma once
#include <array>
#include <cstdint>
#include <limits>

// This is source-image ownership, never an FPS/Present counter. Callers provide
// a producer identity or propagate one through an exact submitted copy.
namespace nr_source_frame {
using Id = std::uint64_t;
enum class Role : unsigned { Unknown, Application, Generated, Retired };
enum class Point : unsigned { None, After, Before, Present };
enum class State : unsigned { Unknown, Ready, Claimed, Recorded, Completed, Poisoned };
enum class Reason : unsigned { None, UnknownSource, Generated, AlreadyClaimed,
    Capacity, InvalidLease, WrongQueue, Unsubmitted, RecordedCannotReject, OpaqueFailure };
struct Source {
    Id stream=0, epoch=0, sequence=0;
    explicit operator bool() const noexcept { return stream && epoch && sequence; }
};
inline bool operator==(Source a, Source b) noexcept {
    return a.stream==b.stream && a.epoch==b.epoch && a.sequence==b.sequence;
}
struct Resource {
    Id address=0, lifetime=0, device=0;
    unsigned subresource=0;
    explicit operator bool() const noexcept { return address && lifetime && device; }
};
inline bool operator==(Resource a,Resource b) noexcept {
    return a.address==b.address && a.lifetime==b.lifetime && a.device==b.device && a.subresource==b.subresource;
}
struct Recording {
    Id list=0, lifetime=0, reset=0, device=0;
    explicit operator bool() const noexcept { return list && lifetime && reset && device; }
};
inline bool operator==(Recording a,Recording b) noexcept {
    return a.list==b.list && a.lifetime==b.lifetime && a.reset==b.reset && a.device==b.device;
}
struct Label {
    Source source{};
    Role role=Role::Unknown;
    Id queue=0, submission=0;
    bool submitted=false;
    Id content=0;
    bool exact_write=false;
    bool tainted=false,processed=false,fg_consumed=false;
    unsigned encoding=0;
    bool KnownApplication() const noexcept { return bool(source) && role==Role::Application; }
};

// A bounded, pointer-free snapshot of the last Present source decision. The
// runtime publishes this separately from the thread-local lease so a later UI
// or feedback read can explain a refusal without exposing COM addresses.
enum class PresentDecision : unsigned {
    Unobserved, InvalidArguments, UnsupportedApi, QueueIdentityUnavailable,
    UnknownLabel, GeneratedLabel, RetiredLabel, NativeProducerBlocked,
    FgConsumed, Unsubmitted, WrongQueue, ClaimRejected, Accepted,
    ReceiptUnavailable, ReceiptPending, NativeFailed, ReceiptDeviceMismatch,
    ReceiptExtentMismatch, ReceiptConfigMismatch, ReceiptExpired, ReceiptConsumed, ReceiptAmbiguous,
    InlineProcessed
};
enum PresentRejectFlag : unsigned {
    PresentRejectNoApplication = 1u << 0,
    PresentRejectFgConsumed = 1u << 1,
    PresentRejectUnsubmitted = 1u << 2,
    PresentRejectWrongQueue = 1u << 3,
    PresentRejectNativeProducer = 1u << 4,
    PresentRejectProcessed = 1u << 5,
    PresentRejectReceipt = 1u << 6
};
struct PresentObservation {
    Id ordinal=0,queue_identity=0,claim_reason=0;
    PresentDecision decision=PresentDecision::Unobserved;
    unsigned reject_mask=0;
    Label lookup{},label{};
    bool native_producer_on_device=false,promotion_eligible=false,
        promotion_blocked_by_native_producer=false,claim_started=false,accepted=false;
    unsigned receipt_decision=0,receipt_age=0,receipt_device_age=0,receipt_output_width=0,receipt_output_height=0;
    unsigned receipt_depth_x=0,receipt_depth_y=0,receipt_depth_width=0,receipt_depth_height=0;
    unsigned receipt_motion_x=0,receipt_motion_y=0,receipt_motion_width=0,receipt_motion_height=0;
    float receipt_motion_scale_x=0,receipt_motion_scale_y=0;
    Id receipt_stream=0,receipt_epoch=0,receipt_sequence=0,receipt_queue=0,receipt_fence=0;
    bool receipt_cross_queue_wait=false,receipt_game_reset=false;
    bool receipt_source_jitter_known=false,receipt_output_jitter_known=false;
};
inline const char* PresentDecisionName(PresentDecision value) noexcept {
    switch(value){
    case PresentDecision::Unobserved:return "unobserved";
    case PresentDecision::InvalidArguments:return "invalid-arguments";
    case PresentDecision::UnsupportedApi:return "unsupported-api";
    case PresentDecision::QueueIdentityUnavailable:return "queue-identity-unavailable";
    case PresentDecision::UnknownLabel:return "unknown-label";
    case PresentDecision::GeneratedLabel:return "generated-label";
    case PresentDecision::RetiredLabel:return "retired-label";
    case PresentDecision::NativeProducerBlocked:return "native-producer-blocked";
    case PresentDecision::FgConsumed:return "fg-consumed";
    case PresentDecision::Unsubmitted:return "unsubmitted";
    case PresentDecision::WrongQueue:return "wrong-queue";
    case PresentDecision::ClaimRejected:return "claim-rejected";
    case PresentDecision::Accepted:return "accepted";
    case PresentDecision::ReceiptUnavailable:return "receipt-unavailable";
    case PresentDecision::ReceiptPending:return "receipt-native-result-pending";
    case PresentDecision::NativeFailed:return "receipt-native-failed";
    case PresentDecision::ReceiptDeviceMismatch:return "receipt-device-mismatch";
    case PresentDecision::ReceiptExtentMismatch:return "receipt-output-extent-mismatch";
    case PresentDecision::ReceiptConfigMismatch:return "receipt-config-mismatch";
    case PresentDecision::ReceiptExpired:return "receipt-expired";
    case PresentDecision::ReceiptConsumed:return "receipt-consumed";
    case PresentDecision::ReceiptAmbiguous:return "receipt-ambiguous-producers";
    case PresentDecision::InlineProcessed:return "inline-processed";
    }
    return "invalid";
}
inline bool PresentPromotionEligible(const Label& label,Id queue) noexcept {
    return queue&&label.role==Role::Unknown&&!label.tainted&&!label.processed&&
        !label.fg_consumed&&label.exact_write&&label.content&&label.submitted&&
        label.queue==queue;
}
inline unsigned PresentRejectMask(const Label& label,Id queue,bool native_producer) noexcept {
    const bool promotion=PresentPromotionEligible(label,queue);
    unsigned mask=0;
    if(promotion&&native_producer)mask|=PresentRejectNativeProducer;
    const bool application=label.KnownApplication()||(promotion&&!native_producer);
    if(!application)mask|=PresentRejectNoApplication;
    if(label.fg_consumed)mask|=PresentRejectFgConsumed;
    if(!label.submitted)mask|=PresentRejectUnsubmitted;
    if(label.queue!=queue)mask|=PresentRejectWrongQueue;
    return mask;
}
struct Ticket {
    Source source{};
    Id attempt=0;
    Point point=Point::None;
    explicit operator bool() const noexcept { return bool(source) && attempt && point!=Point::None; }
};
inline bool operator==(const Ticket& a,const Ticket& b) noexcept {
    return a.source==b.source && a.attempt==b.attempt && a.point==b.point;
}
// Bounded stream high-water marks survive per-frame resource reuse and config
// changes. A late completion cannot release a newer source's claim.
class Claims {
    struct Stream { Id id=0,epoch=0,claimed=0,recorded_sequence=0,fg_consumed=0; bool poisoned=false; };
    struct Pending { Ticket ticket{};Id previous=0;bool recorded=false; };
    std::array<Stream,64> streams_{};
    std::array<Pending,32> pending_{};
    Id next_=1;
    Stream* Find(Source s,bool create) noexcept {
        for(auto& v:streams_) if(v.id==s.stream && v.epoch==s.epoch)return &v;
        if(create)for(auto& v:streams_)if(!v.id){v.id=s.stream;v.epoch=s.epoch;return &v;}
        return nullptr;
    }
    Pending* Find(const Ticket& t) noexcept {
        for(auto& p:pending_)if(p.ticket && p.ticket==t)return &p;
        return nullptr;
    }
public:
    Reason reason=Reason::None;
    Id recorded=0,completed=0;
    bool Available(Source source) noexcept {
        if(!source)return false;
        const auto* s=Find(source,false);
        return !s || (!s->poisoned && source.sequence>s->claimed);
    }
    void ConsumeFg(Source source) noexcept {
        if(!source)return;
        auto* s=Find(source,true);
        if(s){if(s->claimed<source.sequence)s->claimed=source.sequence;
            if(s->fg_consumed<source.sequence)s->fg_consumed=source.sequence;}
    }
    Ticket Begin(const Label& label,Point point) noexcept {
        reason=Reason::None;
        if(label.role==Role::Generated){reason=Reason::Generated;return {};}
        if(!label.KnownApplication() || point==Point::None){reason=Reason::UnknownSource;return {};}
        auto* s=Find(label.source,true);
        if(!s){reason=Reason::Capacity;return {};}
        if(s->poisoned){reason=Reason::OpaqueFailure;return {};}
        if(label.source.sequence<=s->claimed){reason=Reason::AlreadyClaimed;return {};}
        Pending* free=nullptr;for(auto& p:pending_)if(!p.ticket){free=&p;break;}
        if(!free || next_==std::numeric_limits<Id>::max()){reason=Reason::Capacity;return {};}
        free->ticket={label.source,next_++,point};free->previous=s->claimed;free->recorded=false;
        s->claimed=label.source.sequence;return free->ticket;
    }
    bool Record(const Ticket& t) noexcept {
        auto* p=Find(t);if(!p)return false;
        if(!p->recorded){p->recorded=true;++recorded;auto* s=Find(t.source,false);
            if(s&&s->recorded_sequence<t.source.sequence)s->recorded_sequence=t.source.sequence;}return true;
    }
    bool WasRecorded(Source source) noexcept {const auto* s=Find(source,false);return s&&(s->poisoned||source.sequence<=s->recorded_sequence);}
    bool RejectBeforeWork(const Ticket& t) noexcept {
        auto* p=Find(t);if(!p)return false;
        auto* s=Find(t.source,false);
        if(p->recorded){if(s)s->poisoned=true;reason=Reason::RecordedCannotReject;*p={};return false;}
        if(s && s->claimed==t.source.sequence)s->claimed=p->previous>s->fg_consumed?p->previous:s->fg_consumed;
        *p={};return true;
    }
    bool Complete(const Ticket& t,bool success,bool exact_fence_completed) noexcept {
        auto* p=Find(t);if(!p)return false;
        if(!success || !p->recorded){auto* s=Find(t.source,false);if(s)s->poisoned=true;
            reason=Reason::OpaqueFailure;*p={};return false;}
        if(!exact_fence_completed)return false;
        ++completed;*p={};return true;
    }
    // The chain has recorded but no longer needs its CPU ticket. Keep its
    // source high-water claim; this is not completion and cannot permit retry.
    void RetainRecorded(const Ticket& t) noexcept {
        auto* p=Find(t);if(p && p->recorded)*p={};
    }
    void RetireStream(Id stream,Id epoch) noexcept {
        // Only actual producer lifetime retirement may reclaim its high-water.
        for(auto& p:pending_)if(p.ticket.source.stream==stream && p.ticket.source.epoch==epoch)p={};
        for(auto& s:streams_)if(s.id==stream && s.epoch==epoch)s={};
    }
};

// Copies are replayed in GPU submission order, not in CPU interception order.
// An opaque write is an invalidation unless an exact producer publishes a role.
class Lineage {
    enum class Op : unsigned { Set, Copy, Invalidate, InvalidateDevice, Clear };
    struct Event { Op op=Op::Invalidate;Resource destination{},source{};Label label{}; };
    // Hot identity/status is compact: queries and Reset never walk or clear the
    // 48-event payload belonging to every unrelated list. Payload slots remain
    // initialized and are readable only below their current header's count.
    struct Batch {Recording recording{};unsigned count=0;bool submitted=false,overflow=false;};
    struct Value {Resource resource{};Label label{};unsigned surface_encoding=0;};
    std::array<Batch,64> batches_{};
    std::array<std::array<Event,48>,64> batch_events_{};
    // A bit is set iff the corresponding current batch has a valid recording,
    // is unsubmitted and has events. Overflow stays pending, exactly as before.
    Id pending_batches_=0;
    auto& Events(Batch* batch) noexcept {return batch_events_[batch-batches_.data()];}
    const auto& Events(const Batch* batch) const noexcept {return batch_events_[batch-batches_.data()];}
    void ClearBatch(Batch& batch) noexcept {
        pending_batches_&=~(Id{1}<<(&batch-batches_.data()));
        batch={}; // count invalidates all initialized but inactive event bytes
    }
    std::array<Value,256> values_{};
    Id next_submission_=1;
    Id next_content_=1;
    // Relative indices, never borrowed Batch pointers. A cached hit still checks
    // the complete list/lifetime/reset/device tuple after Reset or slot reuse.
    // The caller's existing lineage lock remains the synchronization boundary.
    std::array<unsigned,4> recent_batches_{{64,64,64,64}};
    unsigned recent_batch_=0;
    // A full table cannot admit a previously absent recording until Reset or
    // ForgetList frees a slot. Cache a few exact failures without retiring or
    // evicting any submitted/unsubmitted batch; all failure returns stay intact.
    std::array<Recording,4> rejected_recordings_{};
    unsigned rejected_next_=0;
    bool batches_full_=false;
    // True iff all stored labels are fixed points of Unknown(). Every publisher
    // or retirement that can change that invariant clears this flag. Repeated
    // failed Add calls then preserve fail-closed state without rewriting 256
    // labels per draw. Batch events are never skipped, dropped or reclaimed.
    bool values_unknown_=true;
    void ForgetBatchMisses() noexcept {
        if(!batches_full_)return;
        batches_full_=false;rejected_recordings_={};rejected_next_=0;
    }
    void InvalidateAllValues() noexcept {
        if(values_unknown_)return;
        for(auto& value:values_)value.label=Unknown(value.label);
        values_unknown_=true;
    }
    void RememberBatch(unsigned index) noexcept {
        recent_batch_=(recent_batch_+1)%recent_batches_.size();
        recent_batches_[recent_batch_]=index;
    }
    static Label Unknown(Label previous) noexcept {
        Label result{};result.encoding=previous.encoding;
        result.tainted=previous.tainted||previous.processed||previous.fg_consumed||previous.role==Role::Generated||previous.role==Role::Retired;
        result.fg_consumed=previous.fg_consumed;
        return result;
    }
    unsigned Encoding(Resource resource) const noexcept {
        for(const auto& value:values_)if(value.resource==resource)return value.surface_encoding;
        return 0;
    }
    Batch* BatchFor(Recording r,bool create) noexcept {
        const auto recent=recent_batches_[recent_batch_];
        if(r && recent<batches_.size() && batches_[recent].recording==r)return &batches_[recent];
        for(unsigned i=0;i<recent_batches_.size();++i) {
            const auto index=recent_batches_[i];
            if(r && index<batches_.size() && batches_[index].recording==r) {
                recent_batch_=i;return &batches_[index];
            }
        }
        if(r && batches_full_)for(const auto& rejected:rejected_recordings_)
            if(rejected==r)return nullptr;
        for(auto& b:batches_)if(b.recording==r) {
            RememberBatch(static_cast<unsigned>(&b-batches_.data()));return &b;
        }
        if(create && !batches_full_)for(auto& b:batches_)if(!b.recording){
            b.recording=r;RememberBatch(static_cast<unsigned>(&b-batches_.data()));return &b;
        }
        if(create && r) {
            batches_full_=true;rejected_recordings_[rejected_next_]=r;
            rejected_next_=(rejected_next_+1)%rejected_recordings_.size();
        }
        return nullptr;
    }
    Value* ValueFor(Resource r,bool create) noexcept {
        for(auto& v:values_)if(v.resource==r)return &v;
        if(create)for(auto& v:values_)if(!v.resource){v.resource=r;return &v;}
        return nullptr;
    }
    bool Add(Recording r,Event e) noexcept {
        if(!r || (e.op!=Op::InvalidateDevice && (!e.destination || r.device!=e.destination.device)))return false;
        auto* b=BatchFor(r,true);if(!b){InvalidateAllValues();return false;}
        if(e.op==Op::InvalidateDevice && b->count && Events(b)[b->count-1].op==Op::InvalidateDevice)return true;
        if(e.op==Op::InvalidateDevice && !b->submitted&&!b->overflow){
            // The latest unknown device write supersedes the previous tail of
            // unknown target writes. Retain all semantic Set/Copy/Clear events.
            // Thousands of draws to the same RTV do not consume thousands of
            // receipt slots or silently turn every application frame unknown.
            unsigned tail=b->count;
            while(tail&&(Events(b)[tail-1].op==Op::Invalidate||Events(b)[tail-1].op==Op::InvalidateDevice))--tail;
            for(unsigned i=tail;i<b->count;++i)if(Events(b)[i].op==Op::InvalidateDevice){b->count=i+1;return true;}
        }
        if(b->submitted || b->count==Events(b).size()){b->overflow=true;return false;}
        Events(b)[b->count++]=e;
        pending_batches_|=Id{1}<<(b-batches_.data());return true;
    }
    Label Pending(Resource resource,const Batch* batch,unsigned before,unsigned depth) const noexcept {
        if(depth>48){Label unknown{};unknown.tainted=true;return unknown;}
        if(batch&&batch->overflow){Label unknown{};unknown.tainted=true;unknown.encoding=Encoding(resource);return unknown;}
        if(batch && !batch->overflow)for(unsigned n=before;n>0;--n){const auto& e=Events(batch)[n-1];
            if(e.op==Op::InvalidateDevice)return Unknown(Pending(resource,batch,n-1,depth+1));
            if(!(e.destination==resource))continue;
            if(e.op==Op::Set){auto value=e.label;const auto encoding=Encoding(resource);if(encoding)value.encoding=encoding;return value;}
            if(e.op==Op::Copy)return Pending(e.source,batch,n-1,depth+1);
            if(e.op==Op::Clear){Label value{};value.encoding=Encoding(resource);return value;}
            return Unknown(Pending(resource,batch,n-1,depth+1));
        }
        for(const auto& v:values_)if(v.resource==resource)return v.label;
        return {};
    }
public:
    bool TracksList(Id address,Id lifetime) const noexcept {
        for(const auto& batch:batches_)if(batch.recording.list==address&&batch.recording.lifetime==lifetime)return true;
        return false;
    }
    bool PendingList(Recording recording) const noexcept {
        for(const auto& batch:batches_)if(batch.recording==recording)return !batch.submitted&&batch.count!=0;
        return false;
    }
    bool AnyPending() const noexcept {return pending_batches_!=0;}
    bool Set(Recording r,Resource destination,Label label) noexcept {
        label.submitted=false;label.queue=label.submission=0;
        return Add(r,{Op::Set,destination,{},label});
    }
    bool Copy(Recording r,Resource destination,Resource source,bool complete_copy) noexcept {
        if(!source || source.device!=destination.device || !complete_copy)return Invalidate(r,destination);
        return Add(r,{Op::Copy,destination,source,{}});
    }
    bool Invalidate(Recording r,Resource resource) noexcept {return Add(r,{Op::Invalidate,resource,{},{}});}
    bool Clear(Recording r,Resource resource) noexcept {return Add(r,{Op::Clear,resource,{},{}});}
    bool InvalidateDevice(Recording r) noexcept {return Add(r,{Op::InvalidateDevice,{},{},{}});}
    Label Lookup(Resource resource,Recording current={}) noexcept {
        auto* b=current?BatchFor(current,false):nullptr;
        return Pending(resource,b,b?b->count:0,0);
    }
    bool Submit(Recording r,Id queue,bool execute_succeeded) noexcept {
        auto* b=BatchFor(r,false);if(!b)return true;
        const bool valid=queue && execute_succeeded && !b->submitted && !b->overflow &&
            next_submission_!=std::numeric_limits<Id>::max();
        const Id submission=valid?next_submission_++:0;
        if(b->count)values_unknown_=false; // even failed submission may publish queue/taint metadata
        for(unsigned i=0;i<b->count;++i){const auto& e=Events(b)[i];
            if(e.op==Op::InvalidateDevice){
                for(auto& v:values_)if(v.resource.device==r.device){
                    v.label=Unknown(v.label);v.label.encoding=v.surface_encoding;
                }
                continue;
            }
            auto* target=ValueFor(e.destination,true);if(!target){b->overflow=true;continue;}
            Label value=valid?Label{}:Unknown(target->label);
            if(e.op==Op::Invalidate)value=Unknown(target->label);
            if(valid && e.op==Op::Set)value=e.label;
            if(valid && e.op==Op::Copy){auto* source=ValueFor(e.source,false);
                // Cross-queue copies require a separate explicit wait proof;
                // this core initially admits only the same serialized queue.
                if(source && source->label.submitted && source->label.queue==queue)value=source->label;
                else if(source)value=Unknown(source->label);}
            if(!valid&&(e.op==Op::Set))value.tainted|=Unknown(e.label).tainted;
            if(valid && e.op!=Op::Copy && next_content_!=std::numeric_limits<Id>::max()){
                value.content=next_content_++;value.exact_write=true;}
            value.queue=queue;value.submission=submission;value.submitted=valid;
            if(target->surface_encoding)value.encoding=target->surface_encoding;
            target->label=value;
        }
        b->submitted=true;pending_batches_&=~(Id{1}<<(b-batches_.data()));
        return valid && !b->overflow;
    }
    void Reset(Recording r) noexcept {for(auto& b:batches_)if(b.recording.list==r.list && b.recording.lifetime==r.lifetime){ClearBatch(b);ForgetBatchMisses();}}
    void Forget(Resource r) noexcept {
        for(auto& v:values_)if(v.resource==r)v={};
        for(auto& b:batches_)for(unsigned i=0;i<b.count;++i)
            if(Events(&b)[i].destination==r || Events(&b)[i].source==r)b.overflow=true;
    }
    void ForgetAddress(Id address) noexcept {
        for(auto& value:values_)if(value.resource.address==address){const auto resource=value.resource;Forget(resource);}
    }
    void ForgetList(Id address) noexcept {for(auto& batch:batches_)if(batch.recording.list==address){ClearBatch(batch);ForgetBatchMisses();}}
    void RetireSource(Id stream,Id epoch) noexcept {
        values_unknown_=false; // Role::Retired is not a fixed point of Unknown
        auto retire=[&](Label& label){if(label.source.stream==stream&&label.source.epoch==epoch){
            label.source={};if(label.role!=Role::Generated)label.role=Role::Retired;label.exact_write=false;}};
        for(auto& value:values_)retire(value.label);
        for(auto& batch:batches_)for(unsigned i=0;i<batch.count;++i)retire(Events(&batch)[i].label);
    }
    void Presented(Resource resource,Label label,Id queue) noexcept {
        auto* value=ValueFor(resource,true);if(!value)return;
        values_unknown_=false;
        label.queue=queue;label.submitted=queue!=0;label.processed=true;label.exact_write=true;
        if(!label.content)label.content=value->label.content;
        value->label=label;
    }
    void DescribeSurface(Resource resource,unsigned encoding) noexcept {
        auto* value=ValueFor(resource,true);if(value){value->surface_encoding=encoding;value->label.encoding=encoding;}
    }
};
struct QueueLease {
    Id device=0,producer_queue=0,consumer_queue=0,fence=0,value=0,completed=0;
    bool source_current=false,resources_pinned=false,states_known=false;
    bool Admitted() const noexcept {
        return device && producer_queue && producer_queue==consumer_queue && source_current &&
            resources_pinned && states_known && completed!=std::numeric_limits<Id>::max();
    }
    bool CanReuse() const noexcept {
        return Admitted() && fence && value && completed>=value;
    }
};
} // namespace nr_source_frame

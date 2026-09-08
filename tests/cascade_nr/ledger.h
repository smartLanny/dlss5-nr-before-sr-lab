#pragma once
// Test-only bounded experiment ledger. No NGX, DLL export or production caller.
#include <array>
#include <cstdint>
#include <cmath>

namespace cascade_test {
using Id = std::uint64_t;
enum class Phase { Unverified, KnownStaticZero, MockOnlyVerified };
enum class Status { Rejected, Identity, Complete, HostDeclined, Poisoned };
enum class Operation { Create, Evaluate, Composite };
struct Handles { Id feature=0, params=0; };
struct Request {
    Id view=0, queue=0, source_epoch=0, source_frame=0;
    Id original=0, proxy=0, ping_a=0, ping_b=0;
    unsigned width=0, height=0, layers=1;
    float strength=1;
    std::array<Id,3> tuning{};
    bool synthetic=false, static_source=false, reset_all=true;
    bool permit_unverified_static_experiment=false;
    bool mock_temporal_history_only=false; // Never a real host admission mode.
    Phase source_phase=Phase::Unverified;
};
struct Evaluation {
    unsigned layer=0; Id view=0, queue=0, frame=0, epoch=0;
    Handles handles{}; Id input=0, output=0, immutable_proxy=0;
    bool reset=true, synthetic=true;
    Phase input_phase=Phase::Unverified;
};
struct Result {
    Status status=Status::Rejected;
    unsigned completed_layers=0;
    Id last_completed_output=0;
    bool composed=false;
};
struct Layer {
    Handles handles{}; Id tuning=0, generation=0, last_frame=0;
    unsigned successful_history_steps=0;
    bool dirty=true;
};

class Ledger {
    std::array<Layer,3> layers_{};
    Id view_=0, queue_=0, epoch_=0, frame_=0;
    unsigned width_=0, height_=0;
    bool poisoned_=false;
    void Invalidate(unsigned first) { for(unsigned i=first;i<3;++i) layers_[i].dirty=true; }
    bool Unique(Handles h,unsigned except) const {
        if(!h.feature || !h.params) return false;
        for(unsigned i=0;i<3;++i) if(i!=except && layers_[i].handles.feature &&
            (layers_[i].handles.feature==h.feature || layers_[i].handles.params==h.params)) return false;
        return true;
    }
public:
    const Layer& layer(unsigned index) const { return layers_.at(index); }
    bool poisoned() const { return poisoned_; }

    // Provider must report real completion separately from a successful record.
    // In this preparation all providers are explicit fakes. No runtime is bound.
    template<class Provider> Result Run(const Request& q,Provider& p) {
        Result result;
        if(poisoned_) { result.status=Status::Poisoned; return result; }
        if(!q.view || !q.queue || !q.source_epoch || !q.source_frame ||
           !q.original || !q.proxy || !q.ping_a || !q.ping_b ||
           q.proxy==q.ping_a || q.proxy==q.ping_b || q.ping_a==q.ping_b ||
           q.original==q.ping_a || q.original==q.ping_b ||
           !q.width || !q.height || q.layers<1 || q.layers>3 ||
           !std::isfinite(q.strength) || q.strength<0 || q.strength>4 ||
           !q.synthetic || !q.static_source || (!q.reset_all && !q.mock_temporal_history_only) ||
           q.source_phase!=Phase::KnownStaticZero ||
           (q.layers>1 && !q.permit_unverified_static_experiment) ||
           (view_ && (q.view!=view_ || q.queue!=queue_)) ||
           q.source_frame<=frame_) return result;
        view_=q.view;queue_=q.queue;frame_=q.source_frame;
        if(q.strength==0) {
            Invalidate(0);result.status=Status::Identity;
            result.last_completed_output=q.original;return result;
        }
        const bool size_changed=width_ && (q.width!=width_ || q.height!=height_);
        if(q.source_epoch!=epoch_ || size_changed) Invalidate(0);
        epoch_=q.source_epoch;width_=q.width;height_=q.height;
        auto poison=[&]() {
            poisoned_=true;Invalidate(0);result.status=Status::Poisoned;
            return result; // Keep handles/dependencies; no recovery/release loop.
        };
        // Previous successful Run completed every opaque stage before return.
        // A failed one poisons the ledger, so retirement cannot race pending work.
        for(unsigned i=0;i<3;++i) {
            auto& layer=layers_[i];
            const bool changed=i<q.layers && layer.tuning!=q.tuning[i];
            if(layer.handles.feature && (i>=q.layers || changed || size_changed)) {
                if(!p.Retire(i,layer.handles)) return poison();
                layer.handles={};layer.last_frame=0;layer.successful_history_steps=0;
                Invalidate(i);
            }
        }
        result.last_completed_output=q.proxy;
        result.status=Status::Complete;
        for(unsigned i=0;i<q.layers;++i) {
            auto& layer=layers_[i];
            if(!p.CanRecord(i)) {
                Invalidate(i);result.status=Status::HostDeclined;break;
            }
            if(!layer.handles.feature) {
                Handles acquired{};
                // Failed creates may still return an opaque handle: retain it.
                const bool created=p.Create(i,q.tuning[i],acquired);
                layer.handles=acquired;
                if(!created || !Unique(acquired,i) || !p.Complete(i,Operation::Create)) return poison();
                layer.tuning=q.tuning[i];++layer.generation;
            }
            if(layer.last_frame==q.source_frame) return poison();
            const Id output=i==1?q.ping_b:q.ping_a;
            Evaluation call{i,q.view,q.queue,q.source_frame,q.source_epoch,layer.handles,
                result.last_completed_output,output,q.proxy,true,true,
                i==0?Phase::KnownStaticZero:q.mock_temporal_history_only?Phase::MockOnlyVerified:Phase::Unverified};
            // Full reset is mandatory in the static-only experiment. It does
            // not assert any general temporal/jitter contract for later layers.
            const bool reset=q.reset_all || layer.dirty;
            call.reset=reset;
            Id reported_output=0;
            if(!p.Evaluate(call,reported_output) || reported_output!=output ||
               !p.Complete(i,Operation::Evaluate)) return poison();
            layer.last_frame=q.source_frame;layer.dirty=false;
            layer.successful_history_steps=reset?1:layer.successful_history_steps+1;
            result.last_completed_output=reported_output;
            ++result.completed_layers;
        }
        if(result.completed_layers) {
            if(!p.Composite(q.original,q.proxy,result.last_completed_output,q.strength) ||
               !p.Complete(0,Operation::Composite)) return poison();
            result.composed=true;
        }
        return result;
    }
};
}

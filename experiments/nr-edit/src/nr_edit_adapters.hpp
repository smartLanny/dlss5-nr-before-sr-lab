#pragma once
#include "nr_edit_flow.hpp"
#include "nr_deferred_dx12.hpp"
#include "nr_rr_dx12.hpp"
namespace nr_edit_adapters {
inline nr_edit_flow::Step Step(nr_deferred_dx12::Result r){
    using P=nr_edit_flow::Phase;using S=nr_deferred_dx12::Status;
    return {r.status==S::Poisoned?P::Fatal:r.composed?P::Composed:
            r.status==S::Prepared?P::Ready:P::Declined,0,r.recorded};
}
inline nr_edit_flow::Step Step(nr_rr_dx12::Result r){
    using P=nr_edit_flow::Phase;using S=nr_rr_dx12::Status;
    return {r.status==S::Poisoned?P::Fatal:r.composed?P::Composed:
            r.status==S::Prepared?P::Ready:P::Declined,0,r.recorded};
}
// Hooks owns real source claims and both native command-state snapshots.
template<class Hooks>
class Deferred : public Hooks {
    nr_deferred_dx12::Context* context_;
public:
    nr_deferred_dx12::Input input{};
    Deferred(Hooks hooks,nr_deferred_dx12::Context* c):Hooks(hooks),context_(c){}
    nr_edit_flow::Step PrepareEdit(){
        if(!this->BindPrivateImage(input))return {};
        return Step(nr_deferred_dx12::Begin(context_,this->CommandList(),input));
    }
    nr_edit_flow::Step FinishEdit(std::uint32_t native){
        if(!this->RefreshPostState(input))return {};
        return Step(nr_deferred_dx12::Finish(context_,this->CommandList(),input,native));
    }
    void CancelPendingEdit(){nr_deferred_dx12::Cancel(context_);}
};
template<class Hooks>
class Rr : public Hooks {
    nr_rr_dx12::Context* context_;
public:
    nr_rr_dx12::Input input{};
    Rr(Hooks hooks,nr_rr_dx12::Context* c):Hooks(hooks),context_(c){}
    nr_edit_flow::Step PrepareEdit(){
        if(!this->BindPrivateImage(input))return {};
        return Step(nr_rr_dx12::Begin(context_,this->CommandList(),input));
    }
    nr_edit_flow::Step FinishEdit(std::uint32_t native){
        if(!this->RefreshPostState(input))return {};
        return Step(nr_rr_dx12::Finish(context_,this->CommandList(),input,native));
    }
    void CancelPendingEdit(){nr_rr_dx12::Cancel(context_);}
};
}

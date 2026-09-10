#pragma once
#define NR_HAS_NATIVE_CLEAR_REQUEST 1
#include "nr_native_format.h"
#include "nr_descriptor_provenance.h"

// A frozen CPU API request, not a promise about descriptor contents at GPU
// execution. Only the exact following native invocation can authorize use of a
// cached request for preparing our own typed views.
namespace nr_native_clear {
using Id = std::uint64_t;
struct Receipt {
    nr_native_format::Key key;
    Id config=0,call=0,list=0,reset=0,list_address=0,resource=0,resource_address=0;
    unsigned format=0,width=0,height=0,clears=0;
    bool identity=false,shape=false,unknown=false,conflict=false,overflow=false;
    bool finished=false,success=false;
    explicit operator bool() const {
        return key && key.role==nr_native_format::Role::Output && config && call &&
            list && list_address && resource && resource_address && identity && shape &&
            format && clears && !unknown && !conflict && !overflow && finished && success;
    }
    void Observe(const nr_descriptor::Pair& before,const nr_descriptor::Pair& after) {
        if(++clears>4)overflow=true;
        const auto same=[](const nr_descriptor::Receipt& a,const nr_descriptor::Receipt& b) {
            return a && b && a.stamp.heap==b.stamp.heap && a.stamp.index==b.stamp.index &&
                a.stamp.revision==b.stamp.revision && a.stamp.epoch==b.stamp.epoch &&
                a.shader_visible==b.shader_visible && nr_descriptor::SameView(a.view,b.view);
        };
        if(!before || !after || !same(before.cpu,after.cpu) || !same(before.gpu,after.gpu) ||
           before.cpu.shader_visible || !before.gpu.shader_visible ||
           !nr_descriptor::SameView(before.cpu.view,before.gpu.view) ||
           before.cpu.view.resource.device!=key.device || before.cpu.view.resource.id!=resource ||
           before.cpu.view.resource.address!=resource_address ||
           before.cpu.view.kind!=nr_descriptor::Kind::Uav || !before.cpu.view.format ||
           nr_format_access::IsTypeless(before.cpu.view.format) ||
           !before.cpu.view.typed || !before.cpu.view.shape_known ||
           before.cpu.view.mip || before.cpu.view.plane ||
           before.cpu.view.width!=width || before.cpu.view.height!=height) {unknown=true;return;}
        if(format && format!=before.cpu.view.format)conflict=true;
        if(!format)format=before.cpu.view.format;
    }
};
struct Invocation { Id call=0; Receipt request; };
struct FreshGate {
    nr_native_format::Key key;
    Id config=0,prepared_call=0,current_call=0,request_call=0;
    bool required=false,evaluated=false,accepted=false,same_key=false,current_scope=false;
    const char* reason="not-observed";
};
template<unsigned Capacity=256> class Ledger {
    std::array<Receipt,Capacity> entries{};
    unsigned next=0;
public:
    Receipt Find(const nr_native_format::Key& key,Id config) const {
        if(key)for(const auto& e:entries)if(e.key==key && e.config==config)return e;
        return {};
    }
    void Commit(const Receipt& value) {
        if(!value.key || !value.call || !value.finished)return;
        for(auto& e:entries)if(e.key==value.key && e.config==value.config) {
            if(e.call<value.call)e=value;
            return;
        }
        entries[next++%Capacity]=value;
    }
};
inline bool Admit(const Receipt& prepared,const Invocation& current,
        const nr_native_format::Key& key,Id config,Id list,Id reset,Id address,unsigned format) {
    const auto& fresh=current.request;
    return prepared && fresh && current.call==fresh.call && fresh.call>prepared.call &&
        prepared.key==key && fresh.key==key && prepared.config==config && fresh.config==config &&
        fresh.list==list && fresh.reset==reset && fresh.list_address==address &&
        prepared.format==format && fresh.format==format &&
        prepared.width==fresh.width && prepared.height==fresh.height &&
        prepared.resource==fresh.resource && prepared.resource_address==fresh.resource_address;
}
} // namespace nr_native_clear

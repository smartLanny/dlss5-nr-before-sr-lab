// Exact product resolver with CPU-only D3D12 COM doubles; no NGX or GPU calls.
#define CINTERFACE
#define COBJMACROS
#define NR_HAS_DESCRIPTOR_PROVENANCE 1
#include <d3d12.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nr_post_boundary {
struct List { std::uint64_t address=0,token=0,reset=0,device=0;bool known=false; };
}
struct NrPostBoundaryAliasScope {
    NrPostBoundaryAliasScope* parent=nullptr;
    std::uint64_t original=0,epoch=0;
    nr_post_boundary::List identity{};
};
std::atomic<std::uint64_t> g_command_state_epoch{7};
NrPostBoundaryAliasScope* g_nr_post_boundary_alias_scope=nullptr;
ID3D12GraphicsCommandList* g_native=nullptr;
nr_post_boundary::List g_snapshot{};
nr_post_boundary::List SnapshotPostBoundaryList(ID3D12GraphicsCommandList* list) {
    return list==g_native?g_snapshot:nr_post_boundary::List{};
}
#include "nr_private_sr_call_list.inl"

namespace {
constexpr GUID kUnwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
unsigned checks=0;
void Check(bool ok,const char* name) {++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::abort();}}
struct ListDouble {
    ID3D12GraphicsCommandList iface{};
    ID3D12GraphicsCommandListVtbl vtable{};
    ListDouble* unwrap=nullptr;
    ListDouble* query_override=nullptr;
    bool change_epoch_on_query=false;
    unsigned refs=1,queries=0,releases=0;
    explicit ListDouble(ListDouble* original=nullptr):unwrap(original) {
        vtable.QueryInterface=Query;vtable.AddRef=Add;vtable.Release=Release;iface.lpVtbl=&vtable;
    }
    static ListDouble& Of(ID3D12GraphicsCommandList* p) {return *reinterpret_cast<ListDouble*>(p);}
    static HRESULT STDMETHODCALLTYPE Query(ID3D12GraphicsCommandList* self,REFIID iid,void** out) {
        auto& s=Of(self);++s.queries;if(out)*out=nullptr;
        if(s.change_epoch_on_query)g_command_state_epoch.fetch_add(1);
        if(s.unwrap&&out&&std::memcmp(&iid,&kUnwrapped,sizeof(GUID))==0) {
            auto* target=s.query_override?s.query_override:s.unwrap;
            target->iface.lpVtbl->AddRef(&target->iface);*out=&target->iface;return S_OK;
        }
        return E_NOINTERFACE;
    }
    static ULONG STDMETHODCALLTYPE Add(ID3D12GraphicsCommandList* self){return ++Of(self).refs;}
    static ULONG STDMETHODCALLTYPE Release(ID3D12GraphicsCommandList* self){auto& s=Of(self);Check(s.refs>0,"release live");++s.releases;return --s.refs;}
};
nr_post_boundary::List Proof(ID3D12GraphicsCommandList* raw) {
    return {reinterpret_cast<std::uint64_t>(raw),31,5,47,true};
}
NrPostBoundaryAliasScope Scope(ID3D12GraphicsCommandList* original,const nr_post_boundary::List& proof,
                               std::uint64_t epoch=7,NrPostBoundaryAliasScope* parent=nullptr) {
    return {parent,reinterpret_cast<std::uint64_t>(original),epoch,proof};
}
void ExpectRejected(const char* reason,ID3D12GraphicsCommandList* raw) {
    Check(NrPrivateOriginalCallList(raw)==nullptr,reason);
}
}

int main(){
    ListDouble raw,proxy(&raw),other_raw,other_proxy(&other_raw);
    g_native=&raw.iface;g_snapshot=Proof(&raw.iface);
    auto live=Scope(&proxy.iface,g_snapshot);g_nr_post_boundary_alias_scope=&live;
    Check(NrPrivateOriginalCallList(&raw.iface)==&proxy.iface,"live proxy alias selected");
    Check(raw.refs==1&&raw.releases==1,"QI raw temporary released");
    const unsigned query_count=proxy.queries;
    auto raw_alias=Scope(&raw.iface,g_snapshot);g_nr_post_boundary_alias_scope=&raw_alias;
    Check(NrPrivateOriginalCallList(&raw.iface)==&raw.iface,"verified raw alias selected");
    Check(proxy.queries==query_count,"raw alias does not query unrelated proxy");
    g_nr_post_boundary_alias_scope=nullptr;ExpectRejected("raw without live alias fails closed",&raw.iface);
    g_nr_post_boundary_alias_scope=&live;
    auto invalid=live;invalid.epoch=6;g_nr_post_boundary_alias_scope=&invalid;ExpectRejected("stale epoch",&raw.iface);
    invalid=live;invalid.identity.reset++;ExpectRejected("stale reset",&raw.iface);
    invalid=live;invalid.identity.token++;ExpectRejected("wrong token",&raw.iface);
    invalid=live;invalid.identity.device++;ExpectRejected("wrong device metadata",&raw.iface);
    invalid=live;invalid.identity.address++;ExpectRejected("wrong native address",&raw.iface);
    invalid=live;invalid.identity.known=false;ExpectRejected("unknown alias proof",&raw.iface);
    invalid=live;invalid.original=reinterpret_cast<std::uint64_t>(&other_proxy.iface);
    ExpectRejected("unwrapped list differs from recording",&raw.iface);
    Check(other_raw.refs==1,"wrong unwrapped list temporary released");
    g_nr_post_boundary_alias_scope=&live;auto original_snapshot=g_snapshot;g_snapshot.known=false;
    ExpectRejected("unknown current recording",&raw.iface);g_snapshot=original_snapshot;
    invalid=live;invalid.parent=&live;invalid.epoch=6;g_nr_post_boundary_alias_scope=&invalid;
    Check(NrPrivateOriginalCallList(&raw.iface)==&proxy.iface,"valid outer alias within nesting bound");
    NrPostBoundaryAliasScope chain[17]{};
    chain[16]=live;
    for(int i=15;i>=0;--i){chain[i]=invalid;chain[i].parent=&chain[i+1];}
    g_nr_post_boundary_alias_scope=&chain[0];ExpectRejected("valid alias beyond sixteen scopes ignored",&raw.iface);
    g_nr_post_boundary_alias_scope=&live;proxy.change_epoch_on_query=true;
    ExpectRejected("epoch changes during QI",&raw.iface);
    Check(raw.refs==1,"epoch-race QI reference released");proxy.change_epoch_on_query=false;
    g_command_state_epoch.store(7);
    g_nr_post_boundary_alias_scope=&live;
    Check(NrPrivateOriginalCallList(&raw.iface)==&proxy.iface,"new call revalidates current scope");
    g_nr_post_boundary_alias_scope=nullptr;ExpectRejected("proxy not cached after call",&raw.iface);
    Check(NrPrivateOriginalCallList(nullptr)==nullptr,"null list refused");
    Check(NrPrivateOriginalCallList(&other_raw.iface)==nullptr,"different list refused");
    std::printf("private call list checks=%u\n",checks);
}

// Actual production Reset/Release/setter bodies. CPU COM doubles, not a game.
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>
#include <cstdlib>
#include <chrono>
static void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(2);}}
struct ComCounter {
    IUnknown base{}; unsigned refs=1;
    static HRESULT STDMETHODCALLTYPE Query(IUnknown*,REFIID,void** out){if(out)*out=nullptr;return E_NOINTERFACE;}
    static ULONG STDMETHODCALLTYPE Add(IUnknown* p){return ++reinterpret_cast<ComCounter*>(p)->refs;}
    static ULONG STDMETHODCALLTYPE Drop(IUnknown* p){auto* c=reinterpret_cast<ComCounter*>(p);check(c->refs>1,"COM under-release");return --c->refs;}
    ComCounter(){static IUnknownVtbl table{Query,Add,Drop};base.lpVtbl=&table;}
    template<class T>T* As(){return reinterpret_cast<T*>(&base);}
};
namespace reference {
#include "reset_reference.inl"
}
namespace current {
#include "reset_current.inl"
}
namespace reference_token {
struct CommandStateRecord{unsigned long long token=0;};
CommandStateRecord g_command_states[2048]{};
thread_local unsigned long long token_probes=0;
#include "token_reference.inl"
}
namespace current_token {
struct CommandStateRecord{unsigned long long token=0;};
CommandStateRecord g_command_states[2048]{};
thread_local unsigned long long token_probes=0;
#include "token_current.inl"
}
static void equal(const reference::NativeCommandState& a,const current::NativeCommandState& b){
    check(a.known==b.known&&a.first_recording_observed==b.first_recording_observed,"known/create flag");
    check(!std::strcmp(a.unknown_reason,b.unknown_reason)&&a.unknown_domains==b.unknown_domains,"unknown reason/domain");
    check(a.meta_initialization_dirty==b.meta_initialization_dirty&&a.meta_execution_dirty==b.meta_execution_dirty,"meta state");
    check(a.pipeline==b.pipeline&&a.predicate==b.predicate&&a.shading_image==b.shading_image&&a.raytracing_pipeline==b.raytracing_pipeline,"COM bindings");
    check(a.rtv_cache==b.rtv_cache&&a.dsv_cache==b.dsv_cache,"private descriptors");
    check(a.null_pipeline_baseline==b.null_pipeline_baseline&&a.null_guard_covered==b.null_guard_covered&&a.null_guard_reset_observed==b.null_guard_reset_observed&&a.null_guard_tainted==b.null_guard_tainted,"null baseline");
    check(!std::strcmp(a.baseline_origin,b.baseline_origin)&&a.reset_serial==b.reset_serial&&a.pipeline_writes==b.pipeline_writes,"serial/origin");
    check(!std::memcmp(&a.dynamic,&b.dynamic,sizeof(a.dynamic)),"dynamic reset fields");
    for(unsigned s=0;s<2;++s){
        check(a.roots[s]==b.roots[s]&&a.heaps[s]==b.heaps[s],"root/heap ownership");
        for(unsigned i=0;i<64;++i){const auto& x=a.arguments[s][i];const auto& y=b.arguments[s][i];
            check(unsigned(x.kind)==unsigned(y.kind)&&x.value==y.value&&x.constant_mask==y.constant_mask,"root type/value/mask");
            if(x.kind==reference::CommandRootKind::Constants)
                for(unsigned n=0;n<64;++n)if(x.constant_mask&(UINT64{1}<<n))check(x.constants[n]==y.constants[n],"live constant");
        }
    }
}
static int reset_budget(){
    auto s=std::make_unique<current::NativeCommandState>();
    for(auto& stage:s->arguments)for(auto& arg:stage){arg.kind=current::CommandRootKind::Constants;arg.constant_mask=~UINT64{0};std::fill_n(arg.constants,64,0xA5A51234u);}
    current::ResetNativeCommandState(*s,nullptr);
    unsigned retained=0;
    for(auto& stage:s->arguments)for(auto& arg:stage){check(arg.kind==current::CommandRootKind::Empty&&arg.value==0&&arg.constant_mask==0,"all roots invalidated");for(auto v:arg.constants)retained+=v==0xA5A51234u;}
    std::cout<<"inactive constant words preserved="<<retained<<"/8192\n";
    return retained==8192?0:3;
}
static int token_budget(){
    using namespace current_token;
    for(unsigned i=0;i<16;++i)g_command_states[2000+i].token=10001+i;
    for(unsigned i=0;i<16;++i)check(FindCommandStateTokenLocked(10001+i)==&g_command_states[2000+i],"warm lookup");
    token_probes=0;
    for(unsigned n=0;n<1000;++n)for(unsigned i=0;i<16;++i)check(FindCommandStateTokenLocked(10001+i)==&g_command_states[2000+i],"hot lookup");
    std::cout<<"token registry scan comparisons="<<token_probes<<"/16000 lookups\n";
    return token_probes==0?0:3;
}
static void semantics(){
    ComCounter pipeline,root,heap,predicate;
    auto a=std::make_unique<reference::NativeCommandState>();auto b=std::make_unique<current::NativeCommandState>();
    std::mt19937 rng(0xD218);
    for(unsigned iteration=0;iteration<10000;++iteration){
        const UINT64 value=rng(); const unsigned stage=rng()%2,slot=rng()%64;
        reference::CommandStateAssign(a->roots[stage],root.As<ID3D12RootSignature>());
        current::CommandStateAssign(b->roots[stage],root.As<ID3D12RootSignature>());
        reference::CommandStateAssign(a->heaps[stage],heap.As<ID3D12DescriptorHeap>());
        current::CommandStateAssign(b->heaps[stage],heap.As<ID3D12DescriptorHeap>());
        reference::CommandStateAssign(a->predicate,predicate.As<ID3D12Resource>());
        current::CommandStateAssign(b->predicate,predicate.As<ID3D12Resource>());
        a->null_guard_covered=b->null_guard_covered=bool(rng()%2);
        a->null_guard_tainted=b->null_guard_tainted=bool(rng()%2);
        a->meta_initialization_dirty=b->meta_initialization_dirty=rng();
        a->meta_execution_dirty=b->meta_execution_dirty=rng();
        UINT values[64];for(auto& v:values)v=rng();
        reference::CommandStateSetConstants(*a,stage,slot,64,values,0);current::CommandStateSetConstants(*b,stage,slot,64,values,0);
        auto* p=(iteration%2)?pipeline.As<ID3D12PipelineState>():nullptr;
        const char* origin=iteration%3?"Reset":"ClearState";
        reference::ResetNativeCommandState(*a,p,origin);current::ResetNativeCommandState(*b,p,origin);equal(*a,*b);
        reference::CommandStateSetConstants(*a,stage,slot,1,values,63);current::CommandStateSetConstants(*b,stage,slot,1,values,63);equal(*a,*b);
        check(b->arguments[stage][slot].constant_mask==(UINT64{1}<<63),"reset cannot resurrect stale constants");
        reference::CommandStateSetValue(*a,stage,slot,reference::CommandRootKind::Cbv,value);
        current::CommandStateSetValue(*b,stage,slot,current::CommandRootKind::Cbv,value);equal(*a,*b);
    }
    reference::ReleaseNativeCommandState(*a);current::ReleaseNativeCommandState(*b);equal(*a,*b);
    check(pipeline.refs==1&&root.refs==1&&heap.refs==1&&predicate.refs==1,"COM references balanced");
    auto compare=[](UINT64 token){auto* a=reference_token::FindCommandStateTokenLocked(token);auto* b=current_token::FindCommandStateTokenLocked(token);check(bool(a)==bool(b),"lookup presence");if(a)check(a-std::data(reference_token::g_command_states)==b-std::data(current_token::g_command_states),"lookup slot");};
    for(unsigned n=0;n<20000;++n){unsigned slot=rng()%2048;UINT64 token=100+n;
        if(n%3){reference_token::g_command_states[slot].token=token;current_token::g_command_states[slot].token=token;}
        else {reference_token::g_command_states[slot]={};current_token::g_command_states[slot]={};}
        compare(token);compare(token>20?token-20:0);compare(0);compare(1ULL<<48);
    }
    // An absent token may become registered without any TLS negative invalidation.
    compare(123456789);reference_token::g_command_states[1024].token=123456789;current_token::g_command_states[1024].token=123456789;compare(123456789);
    SRWLOCK lock=SRWLOCK_INIT;std::atomic<bool> go{false};std::vector<std::thread> readers;
    for(unsigned worker=0;worker<2;++worker)readers.emplace_back([&]{while(!go.load())std::this_thread::yield();for(unsigned i=0;i<10000;++i){AcquireSRWLockShared(&lock);compare(900000+i%64);ReleaseSRWLockShared(&lock);}});
    go=true;
    for(unsigned i=0;i<4000;++i){AcquireSRWLockExclusive(&lock);unsigned slot=100+i%64;UINT64 token=i%2?900000+i%64:0;reference_token::g_command_states[slot].token=token;current_token::g_command_states[slot].token=token;ReleaseSRWLockExclusive(&lock);}
    for(auto& t:readers)t.join();
    std::cout<<"10000 reset/partial-write/COM sequences, 20000 token mutations and 20000 native-SRW reads passed\n";
}

static void vertices(){
    std::mt19937 rng(1010277);auto a=std::make_unique<reference::NativeCommandState>();auto b=std::make_unique<current::NativeCommandState>();
    D3D12_VERTEX_BUFFER_VIEW v[32]{};
    for(unsigned trial=0;trial<8000;++trial){
        unsigned first=rng()%35,count=rng()%35;for(auto& x:v){x.BufferLocation=rng();x.SizeInBytes=rng();x.StrideInBytes=rng();}
        const auto* input=trial%3?v:nullptr;
        reference::CommandStateVertices(*a,first,count,input);current::CommandStateVertices(*b,first,count,input);equal(*a,*b);
    }
    for(unsigned count:{0u,1u,2u,16u,31u,32u})for(unsigned first=0;first<=32;++first){
        reference::CommandStateVertices(*a,first,count,nullptr);current::CommandStateVertices(*b,first,count,nullptr);equal(*a,*b);
    }
    // Direct diagnostic calls can alias the private array; retain the old
    // forward-copy result for either overlap direction, not merely memmove.
    for(unsigned first=0;first<16;++first)for(unsigned source=0;source<16;++source){
        reference::CommandStateVertices(*a,0,32,v);current::CommandStateVertices(*b,0,32,v);
        reference::CommandStateVertices(*a,first,16,a->dynamic.vertices+source);
        current::CommandStateVertices(*b,first,16,b->dynamic.vertices+source);equal(*a,*b);
    }
    std::cout<<"8000 vertex updates, zero/full/invalid ranges and alias diagnostics passed\n";
}
static void benchmark(){
    auto a=std::make_unique<reference::NativeCommandState>();auto b=std::make_unique<current::NativeCommandState>();
    auto run=[&](auto& state,bool old,unsigned mode){auto start=std::chrono::steady_clock::now();UINT64 sum=0;UINT word=17;
        D3D12_VERTEX_BUFFER_VIEW v[32]{};
        for(unsigned n=0;n<100000;++n){word+=n;v[0].BufferLocation=word;
            if(mode==0){
                if(old){reference::ResetNativeCommandState(*a,nullptr);reference::CommandStateSetConstants(*a,0,0,1,&word,63);sum+=a->arguments[0][0].constants[63];}
                else{current::ResetNativeCommandState(*b,nullptr);current::CommandStateSetConstants(*b,0,0,1,&word,63);sum+=b->arguments[0][0].constants[63];}
            }else{unsigned count=mode==1?1:mode==2?4:32;
                if(old)reference::CommandStateVertices(*a,0,count,v);else current::CommandStateVertices(*b,0,count,v);
                sum+=state.dynamic.vertices[0].BufferLocation+state.dynamic.vertex_mask;
            }
        }
        std::cout<<" checksum="<<sum;return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
    for(unsigned mode=0;mode<4;++mode){double x=run(*a,true,mode),y=run(*b,false,mode);std::cout<<" mode="<<mode<<" old_ms="<<x<<" new_ms="<<y<<'\n';}
}

int main(int argc,char** argv){if(argc>1&&std::strcmp(argv[1],"--reset-budget")==0)return reset_budget();if(argc>1&&std::strcmp(argv[1],"--token-budget")==0)return token_budget();if(argc>1&&std::strcmp(argv[1],"--bench")==0){benchmark();return 0;}semantics();vertices();}

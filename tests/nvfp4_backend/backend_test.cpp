// Exercises the real backend state machine with a recording-only fake COM
// surface and NVAPI callback. No CUDA/D3D12 GPU execution is claimed here.
#include "../../src/nvfp4/backend.cpp"
#include <cassert>
#include <iostream>
#include <cstdlib>
static std::atomic<size_t> test_allocations{0};
static std::atomic<bool> fail_next_allocation{false};
void* operator new(size_t size) { if(fail_next_allocation.exchange(false))throw std::bad_alloc(); test_allocations.fetch_add(1); if(void* p=std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
void operator delete[](void* p,size_t) noexcept { std::free(p); }

namespace {
using namespace nr::nvfp4;
struct FakeObject { void** vtable; uint64_t address = 0; uint64_t completed = 0; FakeObject* device = nullptr; };
ULONG STDMETHODCALLTYPE AddRef(void*) { return 2; }
ULONG STDMETHODCALLTYPE Release(void*) { return 1; }
HRESULT STDMETHODCALLTYPE GetDevice(FakeObject* object, REFIID, void** result) { *result = object->device; return S_OK; }
uint64_t STDMETHODCALLTYPE Address(FakeObject* object) { return object->address; }
uint64_t STDMETHODCALLTYPE Completed(FakeObject* object) { return object->completed; }
unsigned barriers = 0;
void STDMETHODCALLTYPE Barrier(void*,UINT,const D3D12_RESOURCE_BARRIER* b) { assert(b->Type==D3D12_RESOURCE_BARRIER_TYPE_UAV); ++barriers; }
void* resource_table[32]{}, *list_table[64]{}, *device_table[32]{}, *queue_table[32]{}, *fence_table[32]{};
FakeObject device{device_table}, list{list_table}, queue{queue_table,0,0,&device}, fence{fence_table,0,0,&device};
std::vector<std::unique_ptr<FakeObject>> resources;
unsigned forwards = 0, replacements = 0, fail_at = 0;
unsigned stage_begins[6]{},stage_ends[6]{};
void ObserveStage(Stage stage,bool begin,uint32_t block,ID3D12GraphicsCommandList*,void*) { assert(block>=31 && block<=38); ++(begin ? stage_begins : stage_ends)[uint32_t(stage)]; }
api::Handle publish=reinterpret_cast<void*>(100), expand=reinterpret_cast<void*>(101), contract=reinterpret_cast<void*>(102);
api::Status __cdecl FakeLaunch(ID3D12GraphicsCommandList*,const api::LaunchParams* params,uint32_t count) {
    if(params && count==1 && reinterpret_cast<uintptr_t>(params->function)>=200 && reinterpret_cast<uintptr_t>(params->function)<210) {
        ++replacements; return fail_at && replacements==fail_at ? api::Error : api::Ok;
    }
    ++forwards; return 37; // Exact nonzero original return must be preserved.
}
void Tables() {
    for(auto* table:{resource_table,list_table,device_table,queue_table,fence_table}) { table[1]=reinterpret_cast<void*>(&AddRef); table[2]=reinterpret_cast<void*>(&Release); }
    resource_table[11]=reinterpret_cast<void*>(&Address); list_table[26]=reinterpret_cast<void*>(&Barrier);
    queue_table[7]=reinterpret_cast<void*>(&GetDevice); fence_table[7]=reinterpret_cast<void*>(&GetDevice); fence_table[8]=reinterpret_cast<void*>(&Completed);
}
Buffer FakeBuffer(uint64_t address,uint64_t bytes) {
    auto object=std::make_unique<FakeObject>(); object->vtable=resource_table; object->address=address;
    Buffer buffer; buffer.gpu.Attach(reinterpret_cast<ID3D12Resource*>(object.get())); buffer.bytes=bytes; resources.push_back(std::move(object)); return buffer;
}
std::shared_ptr<Device> Fixture() {
    auto& g=G();
    g.mode.store(Mode::UpstreamReference); g.launch.store(&FakeLaunch); g.observer_enabled.store(false);
    g.contexts.clear(); g.devices.clear(); g.jobs.clear();
    g.retired_contexts.clear(); g.retirement_order.clear(); g.retire_queue.clear(); g.failed_destroy_devices.clear();
    auto d=std::make_shared<Device>(); d->owner=reinterpret_cast<ID3D12Device*>(&device); d->ready=true; d->runtime_approved=true;
    d->cuda.sm=82; d->cuda.major=12; d->cuda.minor=0;
    d->pack=reinterpret_cast<void*>(200); d->expand=reinterpret_cast<void*>(201); d->epilogue=reinterpret_cast<void*>(203);
    d->weights=FakeBuffer(0x10000000,37765120); d->norm=FakeBuffer(0x13000000,256);
    Contract c; c.function=reinterpret_cast<void*>(202); c.grid={8,8,1}; c.block={384,1,1}; c.workspace_bytes=256; c.reset_bytes=0; d->contracts.emplace(960,std::move(c));
    auto a=std::make_shared<Arena>(); a->ready=true; a->m=960;
    a->xp=FakeBuffer(0x20000000,524288); a->xs=FakeBuffer(0x21000000,65536); a->yp=FakeBuffer(0x22000000,2097152);
    a->ys=FakeBuffer(0x23000000,262144); a->c=FakeBuffer(0x24000000,2097152); a->workspace=FakeBuffer(0x25000000,256);
    for(unsigned i=0;i<8;++i) { a->expand_params[i].resize(2176); a->contract_params[i].resize(3456);
        a->expand_info[i].grid[0]=32; a->expand_info[i].grid[1]=1; a->expand_info[i].grid[2]=1;
        a->expand_info[i].block[0]=384; a->expand_info[i].block[1]=1; a->expand_info[i].block[2]=1;
    }
    d->arenas.push_back(a); g.devices[d->owner.Get()]=d;
    auto meta=std::make_shared<Metadata>(); auto raw=d->owner.Get();auto module=reinterpret_cast<void*>(99);
    auto module_live=std::make_shared<std::atomic<bool>>(true);meta->modules[module]={raw,true,module_live};
    meta->functions[publish]={raw,module,Kind::Publish,true,"publish",std::make_shared<std::atomic<bool>>(true),module_live};
    meta->functions[expand]={raw,module,Kind::Expand,true,"expand",std::make_shared<std::atomic<bool>>(true),module_live};
    meta->functions[contract]={raw,module,Kind::Contract,true,"contract",std::make_shared<std::atomic<bool>>(true),module_live};
    std::atomic_store(&g.metadata,std::shared_ptr<const Metadata>(meta));
    forwards=replacements=barriers=fail_at=0; return d;
}
OwnedCall Call(uint64_t context,uint64_t token) {
    auto c=std::make_shared<Context>(); c->m=960; c->device=G().devices.begin()->second;
    G().contexts[{context,1}]=c;
    OwnedCall call; call.context_id=context; call.generation=1; call.recording_token=token; call.evaluation_serial=token;
    call.device=reinterpret_cast<ID3D12Device*>(&device); call.command_list=reinterpret_cast<ID3D12GraphicsCommandList*>(&list);
    call.width=1280; call.height=720; return call;
}
struct OriginalParams { uint64_t p[8]; uint32_t rows=40,cols=24; };
api::Status Expansion(OwnedCall call,unsigned pair,bool bad_geometry=false) {
    OriginalParams p{{0x40000000,0,0x41000000,0,0,0,0,0x42000000}};
    api::LaunchParams k{pair?expand:publish,{bad_geometry?1u:256u,1,1},{32,4,1},0,&p,sizeof(p)};
    return Launch(call.command_list,&k,1);
}
api::Status Contraction(OwnedCall call,bool bad_alias=false) {
    OriginalParams p{{0x41000000,bad_alias?0x99000000ull:0x40000000ull,0x43000000,0,0x44000000,0,0x42000000,0x45000000}};
    api::LaunchParams k{contract,{64,1,4},{32,4,1},0,&p,sizeof(p)}; return Launch(call.command_list,&k,1);
}
void Sequence(OwnedCall call) { for(unsigned i=0;i<8;++i) { assert(Expansion(call,i)==api::Ok); assert(Contraction(call)==api::Ok); } }
void LifetimeTests() {
    using nr::nvfp4::detail::RecordingLifetime;
    RecordingLifetime a; a.token=1;
    assert(!a.Reclaimable()); assert(a.Submit(10,20,3)); a.submissions[0].complete=true;
    assert(!a.Reclaimable()); // complete closed list may replay
    assert(a.Submit(10,20,8)); assert(!a.submissions[0].complete);
    assert(a.Submit(11,21,4)); a.Reset(false); a.submissions[0].complete=true;
    assert(!a.Reclaimable()); a.submissions[1].complete=true; assert(a.Reclaimable());
    RecordingLifetime discarded; discarded.Reset(true); assert(discarded.Reclaimable());
    RecordingLifetime unknown; unknown.quarantined=true; unknown.Reset(false); assert(!unknown.Reclaimable());
    RecordingLifetime lost; assert(lost.Submit(1,2,3)); lost.quarantined=true; lost.submissions[0].complete=true; lost.Reset(false); assert(!lost.Reclaimable());
    RecordingLifetime impossible; impossible.Reset(true); assert(!impossible.Submit(1,2,3)); assert(impossible.quarantined);
    Budget budget; assert(budget.Reserve(MaximumBudget)); assert(!budget.Reserve(1)); budget.Release(128); assert(budget.Reserve(128)); assert(budget.Used()==MaximumBudget);
}
void ScopeTests() {
    auto d=Fixture(); auto call=Call(1,1);
    assert(Launch(call.command_list,nullptr,7)==37 && forwards==1);
    for(unsigned feature:{1u,13u,17u}) { call.feature_id=feature; OwnedScope scope(call); assert(Expansion(call,0)==37); }
    call.feature_id=18; call.pre_sr=false; { OwnedScope scope(call); assert(Expansion(call,0)==37); }
    call.pre_sr=true; G().mode.store(Mode::Disabled); { OwnedScope scope(call); assert(Expansion(call,0)==37); }
    assert(replacements==0 && barriers==0);
    G().mode.store(Mode::UpstreamReference); forwards=0;
    G().config.observe_stage=&ObserveStage;
    const auto before=test_allocations.load();
    { OwnedScope scope(call); assert(scope.WillReplace()); Sequence(call); auto s=scope.Status(); assert(!scope.MayHaveRecordedFailure()); assert(s.replaced_pairs==8 && s.replacement_launches==32 && s.uav_barriers==40 && s.cuda_sm_count==82); }
    assert(test_allocations.load()==before); // Scope + 16 intercepted calls + destruction allocate nothing.
    for(unsigned stage=1;stage<6;++stage) assert(stage_begins[stage]==8 && stage_ends[stage]==8);
    G().config.observe_stage=nullptr;
    assert(forwards==0 && replacements==32 && barriers==40 && d->arenas[0]->leased);
    fence.completed=1; OnSubmit(1,reinterpret_cast<ID3D12CommandQueue*>(&queue),reinterpret_cast<ID3D12Fence*>(&fence),1);
    GetStatus(1,1); assert(d->arenas[0]->leased); // completion alone is not Reset
    OnReset(1,false); assert(!d->arenas[0]->leased);
}
void FallbackTests() {
    auto d=Fixture(); auto call=Call(2,2); d->arenas[0]->ready=false;
    { OwnedScope scope(call); assert(Expansion(call,0)==37); d->arenas[0]->ready=true;
      for(unsigned i=0;i<8;++i) { if(i) assert(Expansion(call,i)==37); assert(Contraction(call)==37); }
      assert(scope.Status().replaced_pairs==0 && !scope.MayHaveRecordedFailure()); }
    assert(replacements==0 && forwards==16 && barriers==0);
    Fixture(); call=Call(3,3);
    { OwnedScope scope(call); assert(Expansion(call,0,true)==37); assert(Contraction(call)==37); assert(!scope.MayHaveRecordedFailure()); }
    assert(replacements==0);
    Fixture(); call=Call(4,4);
    { OwnedScope outer(call); auto nested=call; nested.feature_id=1; { OwnedScope inner(nested); assert(Expansion(nested,0)==37); }
      Sequence(call); assert(!outer.MayHaveRecordedFailure()); }
    Fixture(); call=Call(5,5); call.allow_replacement=false;
    { OwnedScope scope(call); assert(!scope.WillReplace()); assert(Expansion(call,0)==37); assert(Contraction(call)==37); }
    assert(replacements==0);
    assert(WrapNvapi(0x12345678,reinterpret_cast<void*>(1234))==reinterpret_cast<void*>(1234));
    assert(WrapNvapi(api::LaunchId,nullptr)==nullptr);
}
void FailureTests() {
    for(unsigned point=1;point<=32;++point) {
        Fixture(); auto call=Call(100+point,100+point); fail_at=point;
        { OwnedScope scope(call); bool failed=false;
          for(unsigned i=0;i<8 && !failed;++i) { failed=Expansion(call,i)!=api::Ok; if(!failed) failed=Contraction(call)!=api::Ok; }
          assert(failed && scope.MayHaveRecordedFailure()); unsigned before=replacements;
          assert(Expansion(call,0)==api::Error && replacements==before && forwards==0);
        }
        assert(GetStatus(call.context_id,1).may_have_recorded_failure);
        // Failure cannot alter a launch outside this owned context.
        assert(Launch(call.command_list,nullptr,1)==37);
        OnReset(call.recording_token,true);
    }
    Fixture(); auto call=Call(500,500);
    { OwnedScope scope(call); assert(Expansion(call,0)==api::Ok); assert(Contraction(call,true)==api::Error); assert(scope.MayHaveRecordedFailure()); }
    assert(replacements==2 && forwards==0);
    Fixture(); call=Call(501,501);
    { OwnedScope scope(call); assert(Expansion(call,0)==api::Ok); assert(Launch(call.command_list,nullptr,2)==api::Error); assert(scope.MayHaveRecordedFailure()); }
    assert(replacements==2 && forwards==0);
    Fixture(); call=Call(502,502);
    { OwnedScope scope(call); assert(Expansion(call,0)==api::Ok); assert(scope.MayHaveRecordedFailure()); }
    assert(GetStatus(502,1).may_have_recorded_failure);
}
void PolicyAndFusedTests() {
    auto d=Fixture(); auto call=Call(600,600); G().mode.store(Mode::Auto);
    assert(!policy::HasQualifiedPolicies());
    assert(!policy::FindQualifiedPolicy("e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e",{0x10de,0x2c02,12,0,82,13000}));
    assert(!AllowedM(*d,Mode::Auto,960) && AllowedM(*d,Mode::UpstreamReference,960));
    d->optimized_ready.store(true); d->optimized_settings.tested_m={640,960}; d->optimized_settings.allow_auto=true;
    assert(AllowedM(*d,Mode::Optimized,640) && !AllowedM(*d,Mode::Optimized,641));
    assert(!AllowedM(*d,Mode::Auto,640)); // A caller's allow_auto flag is not a qualified registry receipt.
    { OwnedScope scope(call); assert(!scope.WillReplace()); assert(Expansion(call,0)==37); assert(Contraction(call)==37); }
    assert(replacements==0);
    auto prepare_fused=[]() {
        auto device=Fixture(); G().mode.store(Mode::Optimized);
        device->optimized_ready.store(true); device->optimized_settings.tested_m={960}; device->optimized_settings.fused_publish=true;
        device->optimized_contract=reinterpret_cast<void*>(204);
        auto& arena=*device->arenas[0]; arena.optimized=true; arena.fused_publish=true;
        arena.optimized_info.params_bytes=2176; arena.optimized_info.params_alignment=128;
        arena.optimized_info.grid[0]=8; arena.optimized_info.grid[1]=8; arena.optimized_info.grid[2]=1;
        arena.optimized_info.block[0]=384; arena.optimized_info.block[1]=1; arena.optimized_info.block[2]=1;
        // The fake builder fixture explicitly supplies a mutable-region contract.
        // Production reads this offset from the hash-locked real builder.
        arena.fused_info.runtime_bindings_offset=1280; arena.fused_info.runtime_bindings_bytes=48;
        for(auto& params:arena.optimized_params) { params.Resize(2176,128); std::memset(params.Data(),0xa5,2176); }
        return device;
    };
    d=prepare_fused(); call=Call(601,601);
    const auto before=test_allocations.load();
    { OwnedScope scope(call); assert(scope.WillReplace()); Sequence(call); assert(scope.Status().replaced_pairs==8); assert(!scope.MayHaveRecordedFailure()); }
    assert(test_allocations.load()==before && replacements==24 && barriers==32 && forwards==0);
    for(const auto& params:d->arenas[0]->optimized_params) {
        const auto* bytes=params.bytes.data()+params.offset;
        for(size_t i=0;i<2176;++i) if(i<1280 || i>=1328) assert(bytes[i]==0xa5);
        NrNvfp4RuntimeBindings bindings{}; std::memcpy(&bindings,bytes+1280,sizeof(bindings));
        assert(bindings.skip==0x40000000 && bindings.original_output==0x43000000 && bindings.done==0x45000000 && bindings.counter==0x44000000 && bindings.expand_done==0x42000000);
    }
    for(unsigned point=1;point<=24;++point) {
        d=prepare_fused(); call=Call(700+point,700+point); fail_at=point;
        { OwnedScope scope(call); bool failed=false;
          for(unsigned i=0;i<8 && !failed;++i) { failed=Expansion(call,i)!=api::Ok; if(!failed) failed=Contraction(call)!=api::Ok; }
          assert(failed && scope.MayHaveRecordedFailure()); assert(Expansion(call,0)==api::Error && forwards==0);
        }
        OnReset(call.recording_token,true);
    }
    auto prepare_two=[&]() {
        auto device=prepare_fused();device->packed_expansion_function=reinterpret_cast<void*>(205);
        auto& arena=*device->arenas[0];arena.packed_expansion=true;
        auto& info=arena.packed_expansion_info;info.params_bytes=sizeof(NrExpansionParams);
        info.params_alignment=alignof(NrExpansionParams);info.original_bytes=960*1024;
        info.linear_input_offset=offsetof(NrExpansionParams,original_fp8);info.linear_input_bytes=8;
        info.grid[0]=32;info.grid[1]=16;info.grid[2]=1;info.block[0]=256;info.block[1]=info.block[2]=1;
        for(auto& params:arena.packed_expansion_params){params.Resize(sizeof(NrExpansionParams),alignof(NrExpansionParams));std::memset(params.Data(),0xa5,sizeof(NrExpansionParams));}
        return device;
    };
    d=prepare_two();call=Call(850,850);G().config.observe_stage=&ObserveStage;
    std::fill(std::begin(stage_begins),std::end(stage_begins),0);std::fill(std::begin(stage_ends),std::end(stage_ends),0);
    const auto two_before=test_allocations.load();
    {OwnedScope scope(call);assert(scope.WillReplace());Sequence(call);const auto status=scope.Status();
     assert(status.replaced_pairs==8&&status.replacement_launches==16&&status.uav_barriers==24&&!scope.MayHaveRecordedFailure());}
    assert(test_allocations.load()==two_before&&replacements==16&&barriers==24&&forwards==0);
    assert(stage_begins[unsigned(Stage::Pack)]==0&&stage_begins[unsigned(Stage::Epilogue)]==0);
    assert(stage_begins[unsigned(Stage::Expansion)]==8&&stage_ends[unsigned(Stage::Expansion)]==8);
    for(const auto& params:d->arenas[0]->packed_expansion_params){
        const auto* bytes=params.bytes.data()+params.offset;uint64_t original=0;std::memcpy(&original,bytes,8);assert(original==0x40000000);
        for(size_t i=8;i<sizeof(NrExpansionParams);++i)assert(bytes[i]==0xa5);
    }
    G().config.observe_stage=nullptr;
    for(unsigned point=1;point<=16;++point){
        d=prepare_two();call=Call(900+point,900+point);fail_at=point;
        {OwnedScope scope(call);bool failed=false;
         for(unsigned i=0;i<8&&!failed;++i){failed=Expansion(call,i)!=api::Ok;if(!failed)failed=Contraction(call)!=api::Ok;}
         assert(failed&&scope.MayHaveRecordedFailure());assert(Expansion(call,0)==api::Error&&forwards==0);}
        OnReset(call.recording_token,true);
    }
    // Incomplete expansion identities cannot silently enter the two-launch path.
    Fixture();G().devices.clear();G().contexts.clear();
    const std::string digest(64,'a');uint32_t shape=960;
    ExpansionAcceptance expansion_acceptance;expansion_acceptance.directory=L"C:\\private-expansion";
    expansion_acceptance.cubin_sha256=expansion_acceptance.builder_sha256=expansion_acceptance.build_id=digest.c_str();
    expansion_acceptance.kernel_id="nr_pack_expand_v1_m64n128";
    OptimizedAcceptance accepted;accepted.directory=L"C:\\private-contract";
    accepted.cubin_sha256=accepted.builder_sha256=accepted.build_id=digest.c_str();accepted.kernel_id="nr_nvfp4_contract_publish_v1";
    accepted.tested_m=&shape;accepted.tested_m_count=1;accepted.expansion=&expansion_acceptance;
    Config config;config.mode=Mode::Optimized;config.asset_directory=L"C:\\private-assets";config.optimized_acceptance=&accepted;
    assert(!Init(config));accepted.fused_publish=true;expansion_acceptance.variant=3;assert(!Init(config));
    expansion_acceptance.variant=0;expansion_acceptance.build_id="unqualified";assert(!Init(config));
    expansion_acceptance.build_id=digest.c_str();assert(Init(config));
    assert(G().optimized.expansion.build_id==digest&&!G().optimized.qualified_by_registry);
}
unsigned destroy_calls=0;
api::Status __cdecl RejectDestroy(ID3D12Device*,api::Handle) { ++destroy_calls; return api::Error; }
void RetirementTests() {
    auto d=Fixture(); auto call=Call(900,900); call.allow_replacement=false;
    { OwnedScope scope(call); assert(Expansion(call,0)==37); }
    RetireContext(900,1);
    assert(G().contexts.empty() && G().devices.empty() && G().retire_queue.size()==1);
    assert(DestroyPreparedDevice(G().retire_queue.front())); G().retire_queue.clear();
    assert(d->arenas.empty() && !d->weights.gpu);
    Fixture();
    for(uint64_t i=1;i<=5000;++i) { G().contexts[{i,1}]=std::make_shared<Context>(); RetireContext(i,1); }
    assert(G().contexts.empty() && G().retired_contexts.size()==4096 && G().retirement_order.size()==4096);
    // A later-allocated context retiring cannot reject a lower live ID that
    // was delayed before its first eligible backend scope.
    Fixture(); RetireContext(100000,1); call=Call(1,1); call.allow_replacement=false;
    { OwnedScope scope(call); assert(Expansion(call,0)==37); assert(scope.Status().original_launches==1); }
    d=Fixture(); d->functions={reinterpret_cast<void*>(200),reinterpret_cast<void*>(201)};
    G().destroy_function.store(&RejectDestroy); destroy_calls=0;
    assert(!DestroyPreparedDevice(d)); assert(destroy_calls==1 && d->functions.size()==2 && d->weights.gpu && !d->arenas.empty());
    G().destroy_function.store(nullptr);
    // Lost submissions remain pinned even when every known fence completes and
    // Reset subsequently succeeds; this differs from a known recording error.
    d=Fixture(); call=Call(1000,1000);
    { OwnedScope scope(call); Sequence(call); OnQuarantine(1000,"Unknown external submission"); assert(Expansion(call,0)==api::Error); }
    fence.completed=9; OnSubmit(1000,reinterpret_cast<ID3D12CommandQueue*>(&queue),reinterpret_cast<ID3D12Fence*>(&fence),9);
    OnReset(1000,false); RetireContext(1000,1); assert(d->arenas[0]->leased && !G().devices.empty());
}
void RuntimeIdentityTest() {
    const auto path=fs::temp_directory_path()/("nr-nvfp4-runtime-negative-"+std::to_string(GetCurrentProcessId())+".bin");
    { std::ofstream out(path,std::ios::binary); out<<"abc"; }
    assert(ExactHexHash(path,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    auto device=std::make_shared<Device>(); device->runtime_path=path;
    bool rejected=false; try { PrepareDevice(device); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected && !device->runtime_approved && !device->cuda.driver && device->modules.empty());
    device.reset(); fs::remove(path);
}
api::Handle reused_handle=nullptr;
api::Status __cdecl ReusedFunction(ID3D12Device*,api::Handle,const char*,api::Handle* out){*out=reused_handle;return api::Ok;}
api::Status __cdecl ReusedModule(ID3D12Device*,const void*,uint32_t,api::Handle* out){*out=reused_handle;return api::Ok;}
void MetadataReuseTests(){
    const auto module=reinterpret_cast<void*>(99);auto raw=reinterpret_cast<ID3D12Device*>(&device);
    auto d=Fixture();G().create_function.store(&ReusedFunction);G().create_module.store(&ReusedModule);
    auto call=Call(2000,2000);api::Handle output=nullptr;
    {OwnedScope scope(call);const auto old=std::atomic_load(&G().metadata);reused_handle=publish;
        assert(CreateFunction(raw,module,"some_non_ffn_kernel",&output)==api::Ok);
        assert(!old->functions.at(publish).live->load());assert(std::atomic_load(&G().metadata)->functions.count(publish)==0);
        assert(Expansion(call,0)==37 && replacements==0 && !scope.MayHaveRecordedFailure());}
    d=Fixture();call=Call(2001,2001);reused_handle=contract;
    {OwnedScope scope(call);assert(Expansion(call,0)==api::Ok);
        assert(CreateFunction(raw,module,nullptr,&output)==api::Ok);
        assert(Contraction(call)==api::Error && scope.MayHaveRecordedFailure() && forwards==0);}
    d=Fixture();call=Call(2002,2002);reused_handle=module;
    {OwnedScope scope(call);auto old=std::atomic_load(&G().metadata);
        assert(CreateModule(raw,"bad",3,&output)==api::Ok);
        assert(!old->modules.at(module).live->load());assert(std::atomic_load(&G().metadata)->functions.empty());
        assert(Expansion(call,0)==37 && replacements==0);}
    // Allocation failure after a successful native create must invalidate both
    // current and already captured snapshots before retaining the old map.
    d=Fixture();call=Call(2003,2003);reused_handle=publish;
    {OwnedScope scope(call);auto old=std::atomic_load(&G().metadata);fail_next_allocation.store(true);
        assert(CreateFunction(raw,module,"cc_vit_1d_ffn_expand_publish_fp8",&output)==api::Ok);
        assert(!fail_next_allocation.load() && !old->functions.at(publish).live->load());
        assert(Expansion(call,0)==37 && replacements==0);}
    d=Fixture();call=Call(2004,2004);reused_handle=module;
    {OwnedScope scope(call);auto old=std::atomic_load(&G().metadata);fail_next_allocation.store(true);
        assert(CreateModule(raw,"bad",3,&output)==api::Ok);
        assert(!old->modules.at(module).live->load());assert(Expansion(call,0)==37 && replacements==0);
        reused_handle=publish;assert(CreateFunction(raw,module,"cc_vit_1d_ffn_expand_publish_fp8",&output)==api::Ok);
        assert(!std::atomic_load(&G().metadata)->functions.at(publish).supported);}
    d=Fixture();call=Call(2005,2005);G().destroy_function.store(&RejectDestroy);
    {OwnedScope scope(call);assert(DestroyFunction(raw,publish)==api::Error);assert(Expansion(call,0)==37 && replacements==0);}
    d=Fixture();call=Call(2006,2006);G().destroy_module.store(&RejectDestroy);
    {OwnedScope scope(call);assert(DestroyModule(raw,module)==api::Error);assert(Expansion(call,0)==37 && replacements==0);}
    G().destroy_module.store(nullptr);G().destroy_function.store(nullptr);
}
}
int main() { Tables(); LifetimeTests(); ScopeTests(); FallbackTests(); FailureTests(); PolicyAndFusedTests(); RetirementTests(); RuntimeIdentityTest(); MetadataReuseTests(); std::cout<<"NVFP4 backend CPU: scope, zero steady-state allocations, 32 reference + 24 fused + 16 two-launch failure points, exact Auto policy, runtime rejection, 5000 retirements, handle reuse/observer allocation failure and fence/Reset lifetime passed\n"; }

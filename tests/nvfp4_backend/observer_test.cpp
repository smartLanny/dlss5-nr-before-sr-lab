// Executes the real cold API against fake DLL/NVAPI/MinHook boundaries.
// No real NVAPI, CUDA, D3D12 device or GPU is loaded or created.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include "../../src/nvfp4/backend.h"
#include "../../src/nvfp4/nvapi_abi.h"
#include <cassert>
#include <cwchar>
#include <iostream>

HMODULE WINAPI TestLoadLibraryExW(LPCWSTR,HANDLE,DWORD);
FARPROC WINAPI TestGetProcAddress(HMODULE,LPCSTR);
BOOL WINAPI TestGetModuleHandleExW(DWORD,LPCWSTR,HMODULE*);
MH_STATUS WINAPI TestInitialize();
MH_STATUS WINAPI TestCreateHook(LPVOID,LPVOID,LPVOID*);
MH_STATUS WINAPI TestEnableHook(LPVOID);
MH_STATUS WINAPI TestRemoveHook(LPVOID);
#define LoadLibraryExW TestLoadLibraryExW
#define GetProcAddress TestGetProcAddress
#define GetModuleHandleExW TestGetModuleHandleExW
#define MH_Initialize TestInitialize
#define MH_CreateHook TestCreateHook
#define MH_EnableHook TestEnableHook
#define MH_RemoveHook TestRemoveHook
#include "../../src/nvfp4/backend.cpp"
#undef LoadLibraryExW
#undef GetProcAddress
#undef GetModuleHandleExW
#undef MH_Initialize
#undef MH_CreateHook
#undef MH_EnableHook
#undef MH_RemoveHook

namespace {
using namespace nr::nvfp4;
enum class Fault { None, Load, Export, FirstPin, SecondPin, Entry, Initialize, Create, Enable, Unknown };
struct Calls {
    Fault fault=Fault::None;
    unsigned loads=0,exports=0,pins=0,queries=0,initializes=0,creates=0,enables=0,removes=0;
    unsigned modules=0,functions=0,launches=0;
    int32_t launch_status=37;
    bool already_initialized=false;
} calls;
HMODULE FakeModule() { return reinterpret_cast<HMODULE>(0x1000); }
api::Status __cdecl NativeModule(ID3D12Device*,const void*,uint32_t,api::Handle* out) {
    ++calls.modules;*out=reinterpret_cast<void*>(0x2000);return api::Ok;
}
api::Status __cdecl NativeFunction(ID3D12Device*,api::Handle,const char*,api::Handle* out) {
    ++calls.functions;*out=reinterpret_cast<void*>(0x3000);return api::Ok;
}
api::Status __cdecl NativeLaunch(ID3D12GraphicsCommandList*,const api::LaunchParams*,uint32_t) {
    ++calls.launches;return calls.launch_status;
}
api::Status __cdecl NativeDestroy(ID3D12Device*,api::Handle) { return api::Ok; }
void* __cdecl NativeQuery(uint32_t id) {
    ++calls.queries;
    if(calls.fault==Fault::Entry && id==api::LaunchId)return nullptr;
    switch(id) {
    case api::CreateModuleId:return reinterpret_cast<void*>(&NativeModule);
    case api::CreateFunctionId:return reinterpret_cast<void*>(&NativeFunction);
    case api::LaunchId:return reinterpret_cast<void*>(&NativeLaunch);
    case api::DestroyModuleId:case api::DestroyFunctionId:return reinterpret_cast<void*>(&NativeDestroy);
    default:return reinterpret_cast<void*>(0x4000);
    }
}
void NoPreparation() {
    auto& g=G();
    assert(g.devices.empty() && g.jobs.empty() && !g.worker_started);
    assert(g.retire_queue.empty() && g.failed_destroy_devices.empty());
}
void Reset(Fault fault=Fault::None) {
    NoPreparation();
    auto& g=G();g.hook_installed=false;g.nvapi=nullptr;g.query=nullptr;g.query_target=nullptr;
    g.create_module.store(nullptr);g.create_function.store(nullptr);g.launch.store(nullptr);
    g.destroy_module.store(nullptr);g.destroy_function.store(nullptr);
    g.assets.clear();g.runtime_path.clear();g.contexts.clear();
    std::atomic_store(&g.metadata,std::shared_ptr<const Metadata>(std::make_shared<Metadata>()));
    calls={};calls.fault=fault;
    assert(Init(Config{}));
}
void ColdAndDisabledTests() {
    Reset();auto raw=reinterpret_cast<ID3D12Device*>(0x5000);
    assert(!EnsureStarted(raw) && !EnsureStarted(nullptr));
    assert(calls.loads==0 && !G().hook_installed);
    assert(ArmObserver() && G().hook_installed);
    assert(calls.loads==1 && calls.exports==1 && calls.pins==2 && calls.queries==5);
    assert(calls.initializes==1 && calls.creates==1 && calls.enables==1);
    assert(G().mode.load()==Mode::Disabled && G().status.mode==Mode::Disabled);
    assert(G().status.state==State::Disabled && !G().observer_enabled.load());
    assert(G().runtime_path.empty() && G().assets.empty());NoPreparation();
    assert(ArmObserver() && !EnsureStarted(raw));
    assert(calls.loads==1 && calls.creates==1 && calls.enables==1);
    assert(Query(0x12345678)==reinterpret_cast<void*>(0x4000));
    auto create=reinterpret_cast<api::CreateModule>(Query(api::CreateModuleId));
    auto function=reinterpret_cast<api::CreateFunction>(Query(api::CreateFunctionId));
    auto launch=reinterpret_cast<api::Launch>(Query(api::LaunchId));
    api::Handle module=nullptr,entry=nullptr;
    assert(create(raw,"untrusted",9,&module)==api::Ok);
    assert(function(raw,module,"cc_vit_1d_ffn_expand_publish_fp8",&entry)==api::Ok);
    const auto metadata=std::atomic_load(&G().metadata);
    assert(metadata->modules.at(module).device==raw && !metadata->modules.at(module).supported);
    assert(!metadata->functions.at(entry).supported); // Arming never relaxes the hash gate.
    OwnedCall call;call.device=raw;call.command_list=reinterpret_cast<ID3D12GraphicsCommandList*>(0x6000);
    call.context_id=1;call.generation=1;call.recording_token=1;
    { OwnedScope scope(call);assert(!scope.WillReplace());assert(launch(call.command_list,nullptr,7)==37); }
    assert(calls.launches==1 && G().contexts.empty());NoPreparation();
    Config enabled;enabled.mode=Mode::Auto;enabled.asset_directory=L"C:\\observer-test-assets";
    enabled.nr_runtime_path=L"C:\\observer-test-runtime.dll";
    assert(Init(enabled) && G().mode.load()==Mode::Auto);
    assert(std::atomic_load(&G().metadata)==metadata && ArmObserver());
    assert(calls.loads==1 && calls.creates==1);NoPreparation();
}
void ModeAndConcurrencyTests() {
    for(auto mode:{Mode::Disabled,Mode::Auto,Mode::Passthrough,Mode::UpstreamReference,Mode::Optimized}) {
        Reset();Config config;config.mode=mode;config.asset_directory=L"C:\\observer-test-assets";
        config.nr_runtime_path=L"C:\\observer-test-runtime.dll";assert(Init(config));
        const auto status=G().status;const auto assets=G().assets,runtime=G().runtime_path;
        assert(!EnsureStarted(nullptr) && calls.loads==0);
        assert(ArmObserver());assert(G().mode.load()==mode && G().config.mode==mode);
        assert(G().status.mode==status.mode && G().status.state==status.state);
        assert(!std::strcmp(G().status.reason,status.reason));
        assert(G().assets==assets && G().runtime_path==runtime);NoPreparation();
    }
    Reset();calls.already_initialized=true;
    std::thread threads[8];
    for(auto& thread:threads)thread=std::thread([]{assert(ArmObserver());});
    for(auto& thread:threads)thread.join();
    assert(calls.loads==1 && calls.queries==5 && calls.creates==1 && calls.enables==1);
    assert(G().mode.load()==Mode::Disabled);NoPreparation();
}
void FailureTests() {
    for(auto fault:{Fault::Load,Fault::Export,Fault::FirstPin,Fault::SecondPin,Fault::Entry,
                    Fault::Initialize,Fault::Create,Fault::Enable,Fault::Unknown}) {
        Reset(fault);assert(!ArmObserver() && !G().hook_installed);
        assert(G().mode.load()==Mode::Disabled && G().status.mode==Mode::Disabled);
        assert(G().status.state==State::Fallback && G().status.reason[0]);
        assert(G().assets.empty() && G().runtime_path.empty());NoPreparation();
        if(fault==Fault::Enable)assert(calls.removes==1 && G().query==nullptr);
        calls.fault=Fault::None;assert(ArmObserver() && G().hook_installed);
        assert(G().mode.load()==Mode::Disabled);NoPreparation();
    }
    Reset(Fault::Load);Config enabled;enabled.mode=Mode::Auto;
    enabled.asset_directory=L"C:\\observer-test-assets";assert(Init(enabled));
    assert(!EnsureStarted(reinterpret_cast<ID3D12Device*>(0x5000)));
    assert(calls.loads==1 && !G().hook_installed && G().mode.load()==Mode::Auto);NoPreparation();
}
struct OriginalEvent {
    uint64_t sequence=0;uintptr_t function=0,module=0;uint32_t count=0;
    bool begin=false,live=false,ffn=false,has_hash=false;int32_t status=0;
    char name[128]{};std::array<uint8_t,32> hash{};
};
std::array<OriginalEvent,32> original_events{};unsigned original_event_count=0;
void OriginalObserver(const OriginalLaunchInfo& info,bool begin,int32_t status,ID3D12GraphicsCommandList*,void*) noexcept {
    assert(original_event_count<original_events.size());auto& event=original_events[original_event_count++];event={};
    event.sequence=info.sequence;event.function=info.function;event.module=info.module;event.count=info.launch_count;
    event.begin=begin;event.live=info.identity_live;event.ffn=info.ffn_target;event.status=status;
    if(info.function_name)std::snprintf(event.name,sizeof(event.name),"%s",info.function_name);
    event.has_hash=info.module_sha256!=nullptr;if(info.module_sha256)std::copy(info.module_sha256,info.module_sha256+32,event.hash.begin());
}
void OriginalTimingObserverTests() {
    Reset();Config config;config.observe_original_launch=&OriginalObserver;
    assert(Init(config)&&ArmObserver()&&G().mode.load()==Mode::Disabled);NoPreparation();
    auto raw=reinterpret_cast<ID3D12Device*>(0x5000);api::Handle module=nullptr,function=nullptr;
    assert(CreateModule(raw,"abc",3,&module)==api::Ok);
    assert(CreateFunction(raw,module,"cc_vit_1d_attention_test",&function)==api::Ok);
    const auto metadata=std::atomic_load(&G().metadata);
    assert(metadata->functions.empty()); // Diagnostics cannot admit another replacement target.
    assert(metadata->original_functions.at(function).name=="cc_vit_1d_attention_test");
    const auto& hash=metadata->modules.at(module);assert(hash.hash_valid&&hash.hash[0]==0xba&&hash.hash[31]==0xad);
    OwnedCall call;call.device=raw;call.command_list=reinterpret_cast<ID3D12GraphicsCommandList*>(0x6000);
    call.context_id=1;call.generation=1;call.recording_token=1;
    api::LaunchParams params{function,{2,3,4},{32,1,1},0,nullptr,0};
    original_event_count=0;calls.launch_status=api::Ok;
    {OwnedScope scope(call);assert(!scope.WillReplace());assert(Launch(call.command_list,&params,1)==api::Ok);
        auto nested=call;nested.feature_id=1;{OwnedScope inner(nested);assert(Launch(call.command_list,&params,1)==api::Ok);}
        calls.launch_status=37;assert(Launch(call.command_list,&params,1)==37);
        assert(!scope.MayHaveRecordedFailure()&&scope.Status().original_launches==2);}
    assert(original_event_count==4&&original_events[0].begin&&!original_events[1].begin);
    assert(original_events[0].sequence==0&&original_events[2].sequence==1&&original_events[3].status==37);
    assert(original_events[0].live&&original_events[0].has_hash&&!original_events[0].ffn);
    assert(!std::strcmp(original_events[0].name,"cc_vit_1d_attention_test"));NoPreparation();
    assert(Launch(call.command_list,&params,1)==37&&original_event_count==4);
    original_event_count=0;
    {OwnedScope scope(call);assert(DestroyFunction(raw,function)==api::Ok);
        assert(Launch(call.command_list,&params,1)==37);assert(!original_events[0].live);}
    assert(!metadata->original_functions.at(function).live->load());
    assert(CreateFunction(raw,module,"another_kernel",&function)==api::Ok);
    original_event_count=0;
    {OwnedScope scope(call);api::Handle reused=nullptr;assert(CreateModule(raw,"def",3,&reused)==api::Ok);
        assert(Launch(call.command_list,&params,1)==37);assert(!original_events[0].live);}
    assert(std::atomic_load(&G().metadata)->original_functions.empty());NoPreparation();
    config.observe_original_launch=nullptr;assert(Init(config));original_event_count=0;
    {OwnedScope scope(call);assert(Launch(call.command_list,&params,1)==37);}
    assert(original_event_count==0);NoPreparation();
}
}
HMODULE WINAPI TestLoadLibraryExW(LPCWSTR name,HANDLE file,DWORD flags) {
    ++calls.loads;assert(!std::wcscmp(name,L"nvapi64.dll") && !file && flags==LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(calls.fault==Fault::Unknown)throw 1;
    return calls.fault==Fault::Load?nullptr:FakeModule();
}
FARPROC WINAPI TestGetProcAddress(HMODULE module,LPCSTR name) {
    ++calls.exports;assert(module==FakeModule() && !std::strcmp(name,"nvapi_QueryInterface"));
    if(calls.fault==Fault::Export)return nullptr;
    auto query=&NativeQuery;FARPROC result=nullptr;
    static_assert(sizeof(query)==sizeof(result));std::memcpy(&result,&query,sizeof(result));return result;
}
BOOL WINAPI TestGetModuleHandleExW(DWORD flags,LPCWSTR address,HMODULE* out) {
    ++calls.pins;assert(address && flags==(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN));
    if((calls.fault==Fault::FirstPin && calls.pins==1)||(calls.fault==Fault::SecondPin && calls.pins==2))return FALSE;
    *out=FakeModule();return TRUE;
}
MH_STATUS WINAPI TestInitialize() {
    ++calls.initializes;if(calls.fault==Fault::Initialize)return MH_ERROR_MEMORY_ALLOC;
    return calls.already_initialized?MH_ERROR_ALREADY_INITIALIZED:MH_OK;
}
MH_STATUS WINAPI TestCreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    ++calls.creates;assert(target==reinterpret_cast<void*>(&NativeQuery));
    assert(detour==reinterpret_cast<void*>(&nr::nvfp4::Query) && original);
    if(calls.fault==Fault::Create)return MH_ERROR_MEMORY_ALLOC;
    *original=reinterpret_cast<void*>(&NativeQuery);return MH_OK;
}
MH_STATUS WINAPI TestEnableHook(LPVOID target) {
    ++calls.enables;assert(target==reinterpret_cast<void*>(&NativeQuery));
    return calls.fault==Fault::Enable?MH_ERROR_MEMORY_PROTECT:MH_OK;
}
MH_STATUS WINAPI TestRemoveHook(LPVOID target) {
    ++calls.removes;assert(target==reinterpret_cast<void*>(&NativeQuery));return MH_OK;
}
int main() {
    ColdAndDisabledTests();ModeAndConcurrencyTests();FailureTests();OriginalTimingObserverTests();
    std::cout<<"NVFP4 observer CPU: actual cold API, Disabled passthrough, metadata retention, five unchanged modes, concurrent idempotence, nine install failures/retries, optional full-function timing identities and invalidation; no device, CUDA or worker preparation passed\n";
}

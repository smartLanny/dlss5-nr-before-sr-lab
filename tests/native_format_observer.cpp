// Host-only execution of the three production native-format modules.
// COM, hook publication and Win32 locks are explicit isolation seams; this is
// not native D3D12, MinHook installation, GPU execution or game verification.
#include <atomic>
#include <barrier>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

using UINT=unsigned; using UINT64=unsigned long long; using HRESULT=int;
using DXGI_FORMAT=unsigned; using REFIID=int;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16B16A16_FLOAT=10;
constexpr DXGI_FORMAT DXGI_FORMAT_UNKNOWN=0, DXGI_FORMAT_R32G32B32A32_TYPELESS=1,
    DXGI_FORMAT_R32G32B32A32_FLOAT=2, DXGI_FORMAT_R16G16B16A16_TYPELESS=9,
    DXGI_FORMAT_R11G11B10_FLOAT=26, DXGI_FORMAT_R10G10B10A2_UNORM=24,
    DXGI_FORMAT_R8G8B8A8_UNORM=28, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB=29;
#define STDMETHODCALLTYPE
#define SUCCEEDED(v) ((v)>=0)
#define FAILED(v) ((v)<0)
constexpr REFIID IID_ID3D12Device=1;
constexpr unsigned D3D12_RESOURCE_DIMENSION_TEXTURE2D=3;
constexpr unsigned D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE=8;
constexpr unsigned D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS=4;
constexpr unsigned D3D12_FEATURE_FORMAT_SUPPORT=3;
constexpr unsigned D3D12_FORMAT_SUPPORT1_TEXTURE2D=0x20;
constexpr unsigned D3D12_FORMAT_SUPPORT1_SHADER_LOAD=0x100;
constexpr unsigned D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW=0x2000000;
constexpr unsigned D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE=0x80;
constexpr unsigned D3D12_SRV_DIMENSION_TEXTURE2D=4, D3D12_UAV_DIMENSION_TEXTURE2D=4;
constexpr unsigned D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING=5768;
struct GUID { unsigned a; unsigned short b,c; unsigned char d[8]; };
struct D3D12_RESOURCE_DESC {
    unsigned Dimension=3; UINT64 Width=1280; unsigned Height=720;
    unsigned DepthOrArraySize=1, MipLevels=1;
    DXGI_FORMAT Format=27;
    struct {unsigned Count=1;} SampleDesc;
    unsigned Flags=4;
};
struct D3D12_SHADER_RESOURCE_VIEW_DESC {
    DXGI_FORMAT Format=28; unsigned ViewDimension=4;
    unsigned Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    struct {unsigned MostDetailedMip=0, MipLevels=1, PlaneSlice=0; float ResourceMinLODClamp=0;} Texture2D;
};
struct D3D12_UNORDERED_ACCESS_VIEW_DESC {
    DXGI_FORMAT Format=28; unsigned ViewDimension=4;
    struct {unsigned MipSlice=0, PlaneSlice=0;} Texture2D;
};
struct D3D12_CPU_DESCRIPTOR_HANDLE {std::uintptr_t ptr=0;};
struct D3D12_FEATURE_DATA_FORMAT_SUPPORT {DXGI_FORMAT Format=0; unsigned Support1=0, Support2=0;};
std::atomic<unsigned> checks{0},failures{0};
void Check(bool value,const char* message) {
    ++checks;
    if(!value) {++failures; std::fprintf(stderr,"FAIL: %s\n",message);}
}
thread_local unsigned registry_lock_depth=0;
using SRWLOCK=std::shared_mutex;
#define SRWLOCK_INIT {}
void AcquireSRWLockShared(SRWLOCK* p) {p->lock_shared(); ++registry_lock_depth;}
void ReleaseSRWLockShared(SRWLOCK* p) {--registry_lock_depth; p->unlock_shared();}
void AcquireSRWLockExclusive(SRWLOCK* p) {p->lock(); ++registry_lock_depth;}
void ReleaseSRWLockExclusive(SRWLOCK* p) {--registry_lock_depth; p->unlock();}
void OutsideRegistry() {Check(registry_lock_depth==0,"COM/native call made while ledger lock held");}

struct PrivateData {
    std::mutex mutex;
    std::map<unsigned,std::vector<unsigned char>> values;
    bool fail_get=false, fail_set=false, short_get=false;
};
HRESULT ReadPrivate(PrivateData& data,const GUID& key,UINT* bytes,void* out) {
    OutsideRegistry(); std::lock_guard guard(data.mutex);
    auto it=data.values.find(key.a);
    if(data.fail_get||it==data.values.end()||*bytes<it->second.size()) return -1;
    std::memcpy(out,it->second.data(),it->second.size());
    *bytes=static_cast<UINT>(it->second.size())-(data.short_get?1u:0u);
    return 0;
}
HRESULT WritePrivate(PrivateData& data,const GUID& key,UINT bytes,const void* value) {
    OutsideRegistry(); std::lock_guard guard(data.mutex);
    if(data.fail_set) return -1;
    const auto* begin=static_cast<const unsigned char*>(value);
    data.values[key.a]=std::vector<unsigned char>(begin,begin+bytes);
    return 0;
}
struct ID3D12Device;
struct ID3D12Resource {PrivateData data;};
struct ID3D12DeviceVtbl {
    void (*CreateShaderResourceView)(ID3D12Device*,ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
    void (*CreateUnorderedAccessView)(ID3D12Device*,ID3D12Resource*,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
};
struct ID3D12Device {
    ID3D12DeviceVtbl* lpVtbl;
    PrivateData data;
    std::atomic<unsigned> references{1}, queries{0};
    unsigned support1=0x20|0x100|0x2000000, support2=0x80;
    HRESULT query_result=0;
    explicit ID3D12Device(ID3D12DeviceVtbl* table):lpVtbl(table) {}
};
struct ID3D12GraphicsCommandList {ID3D12Device* device; bool fail_device=false;};
HRESULT ID3D12Resource_GetPrivateData(ID3D12Resource* p,const GUID& g,UINT* n,void* o) {return ReadPrivate(p->data,g,n,o);}
HRESULT ID3D12Resource_SetPrivateData(ID3D12Resource* p,const GUID& g,UINT n,const void* o) {return WritePrivate(p->data,g,n,o);}
HRESULT ID3D12Device_GetPrivateData(ID3D12Device* p,const GUID& g,UINT* n,void* o) {return ReadPrivate(p->data,g,n,o);}
HRESULT ID3D12Device_SetPrivateData(ID3D12Device* p,const GUID& g,UINT n,const void* o) {return WritePrivate(p->data,g,n,o);}
HRESULT ID3D12GraphicsCommandList_GetDevice(ID3D12GraphicsCommandList* p,REFIID id,void** out) {
    OutsideRegistry(); Check(id==IID_ID3D12Device,"GetDevice IID changed");
    if(p->fail_device) return -1;
    *out=p->device; if(p->device) ++p->device->references;
    return 0;
}
void ID3D12Device_Release(ID3D12Device* p) {OutsideRegistry(); --p->references;}
HRESULT ID3D12Device_CheckFeatureSupport(ID3D12Device* p,unsigned feature,void* data,std::size_t bytes) {
    OutsideRegistry(); ++p->queries;
    Check(feature==D3D12_FEATURE_FORMAT_SUPPORT&&bytes==sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT),"format capability query ABI");
    auto& support=*static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT*>(data);
    support.Support1=p->support1; support.Support2=p->support2; return p->query_result;
}
struct NgxParams {ID3D12Resource* color; ID3D12Resource* output; std::uint64_t untouched=0x8a77226655ull;};
struct NgxHandle {unsigned id=1;};
using NgxResult=unsigned; constexpr NgxResult kNgxSuccess=1;
using EvaluateFeatureFn=NgxResult(*)(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*);
bool GetResource(const NgxParams* p,const char* name,ID3D12Resource** out) {
    OutsideRegistry();
    if(std::strcmp(name,"Color")==0) *out=p->color;
    else if(std::strcmp(name,"Output")==0) *out=p->output;
    else {Check(false,"unexpected NGX parameter read"); *out=nullptr; return false;}
    return *out!=nullptr;
}
namespace reshade::api {
enum class device_api {d3d12,other};
struct device {device_api api; std::uintptr_t native; device_api get_api(){return api;} std::uintptr_t get_native(){return native;}};
}
struct CommandStateHookFamily {void* originals[4]{};};
struct CommandStateHookRequest {CommandStateHookFamily* family; void* target; void* const* detours;};
std::atomic<unsigned> g_config_generation{1};
bool publish_ok=true; unsigned publications=0;
bool PublishCommandStateHooks(CommandStateHookRequest* requests,unsigned count) {
    OutsideRegistry(); ++publications; Check(count==2,"native format hook batch is exactly two");
    if(!publish_ok) return false;
    for(unsigned i=0;i<count;++i) for(unsigned slot=0;slot<4;++slot) {
        Check(requests[i].detours[slot]!=nullptr,"missing native format detour");
        requests[i].family->originals[slot]=requests[i].target;
    }
    return true;
}

#include "src/nr_native_format.h"
struct { int r8_output_encoding = 0; } g_config;
#include "native_format_color_contract.inl"
#include "src/nr_native_format_runtime.inl"
#include "src/nr_native_format_hooks.inl"

struct DescriptorCall {
    ID3D12Device* device=nullptr; ID3D12Resource* resource=nullptr;
    ID3D12Resource* counter=nullptr; const void* desc=nullptr;
    std::uintptr_t destination=0; unsigned calls=0;
};
thread_local DescriptorCall srv_call,uav_call;
void OriginalSrv(ID3D12Device* d,ID3D12Resource* r,const D3D12_SHADER_RESOURCE_VIEW_DESC* s,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    OutsideRegistry(); srv_call={d,r,nullptr,s,h.ptr,srv_call.calls+1};
}
void OriginalUav(ID3D12Device* d,ID3D12Resource* r,ID3D12Resource* c,const D3D12_UNORDERED_ACCESS_VIEW_DESC* s,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    OutsideRegistry(); uav_call={d,r,c,s,h.ptr,uav_call.calls+1};
}
ID3D12DeviceVtbl device_vtable{&OriginalSrv,&OriginalUav};
void Srv(ID3D12Device& device,ID3D12Resource* resource,unsigned format=28,unsigned variant=0,unsigned slot=0) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{}; desc.Format=format;
    if(variant==1) desc.Texture2D.MostDetailedMip=1;
    if(variant==2) desc.Shader4ComponentMapping=0;
    if(variant==3) desc.ViewDimension=1;
    if(variant==4) desc.Texture2D.MipLevels=2;
    if(variant==5) desc.Texture2D.PlaneSlice=1;
    if(variant==6) desc.Texture2D.ResourceMinLODClamp=1;
    if(variant==8) desc.Texture2D.MipLevels=UINT_MAX;
    const auto saved=desc; const auto* pointer=variant==7?nullptr:&desc;
    const unsigned before=srv_call.calls;
    const D3D12_CPU_DESCRIPTOR_HANDLE destination{0x112233u};
    using Fn=decltype(ID3D12DeviceVtbl::CreateShaderResourceView);
    reinterpret_cast<Fn>(g_nr_format_srv_detours[slot])(&device,resource,pointer,destination);
    Check(srv_call.calls==before+1,"SRV original must be called exactly once");
    Check(srv_call.device==&device&&srv_call.resource==resource&&srv_call.desc==pointer&&srv_call.destination==destination.ptr,
          "SRV hook modified original pointer/handle parameters");
    Check(std::memcmp(&saved,&desc,sizeof(desc))==0,"SRV hook modified descriptor bytes");
}
void Uav(ID3D12Device& device,ID3D12Resource* resource,unsigned format=28,unsigned variant=0,unsigned slot=0) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{}; desc.Format=format;
    if(variant==1) desc.Texture2D.MipSlice=1;
    if(variant==2) desc.Texture2D.PlaneSlice=1;
    if(variant==3) desc.ViewDimension=1;
    const auto saved=desc; const auto* pointer=variant==4?nullptr:&desc;
    ID3D12Resource counter; auto* counter_pointer=variant==5?&counter:nullptr;
    const unsigned before=uav_call.calls;
    const D3D12_CPU_DESCRIPTOR_HANDLE destination{0x445566u};
    using Fn=decltype(ID3D12DeviceVtbl::CreateUnorderedAccessView);
    reinterpret_cast<Fn>(g_nr_format_uav_detours[slot])(&device,resource,counter_pointer,pointer,destination);
    Check(uav_call.calls==before+1,"UAV original must be called exactly once");
    Check(uav_call.device==&device&&uav_call.resource==resource&&uav_call.counter==counter_pointer&&
          uav_call.desc==pointer&&uav_call.destination==destination.ptr,"UAV hook modified original parameters");
    Check(std::memcmp(&saved,&desc,sizeof(desc))==0,"UAV hook modified descriptor bytes");
}
struct NativeCall {
    ID3D12GraphicsCommandList* list; const NgxHandle* handle; const NgxParams* params;
    void* callback; NgxResult result; std::function<void()> action; unsigned calls=0;
};
thread_local NativeCall* native_call=nullptr;
NgxResult OriginalEvaluate(ID3D12GraphicsCommandList* list,const NgxHandle* handle,const NgxParams* params,void* callback) {
    OutsideRegistry(); auto& expected=*native_call; ++expected.calls;
    Check(list==expected.list&&handle==expected.handle&&params==expected.params&&callback==expected.callback,
          "native Feature1 callback arguments changed");
    if(expected.action) expected.action(); return expected.result;
}
void Invoke(ID3D12GraphicsCommandList* list,const NgxParams& params,UINT64 generation,
            NgxResult result,const std::function<void()>& action) {
    NgxHandle handle; unsigned callback=711;
    NativeCall current{list,&handle,&params,&callback,result,action};
    const auto before=params; auto* prior=native_call; native_call=&current;
    const auto actual=NrCallNativeWithFormatObservation(&OriginalEvaluate,list,&handle,&params,&callback,generation);
    native_call=prior;
    Check(actual==result&&current.calls==1,"native callback result/count changed");
    Check(std::memcmp(&before,&params,sizeof(params))==0,"native NGX parameters mutated");
}
nr_native_format::Evidence Evidence(ID3D12Device& device,ID3D12Resource& resource,UINT64 generation,bool output=false) {
    return NrFormatEvidence(NrFormatKey(&device,&resource,generation,output));
}
void PureLedger() {
    using namespace nr_native_format;
    Ledger<2> ledger;
    Key a{1,2,3,Role::Color}, b{1,2,4,Role::Color}, c{1,5,3,Role::Output};
    View unorm; unorm.Observe(28);
    ledger.Commit(a,{},1,true); Check(!ledger.Find(a).key,"no observed view cannot prove interpretation");
    ledger.Commit(a,unorm,2,true); Check(ledger.Find(a).Resolved()==28,"ledger UNORM observation");
    ledger.Commit(a,unorm,3,true); Check(ledger.Find(a).first_call==2&&ledger.Find(a).last_call==3,"ledger call window");
    View srgb; srgb.Observe(29); ledger.Commit(a,srgb,4,true);
    Check(ledger.Find(a).Resolved()==0&&ledger.Find(a).view.conflict,"conflicting views revoke interpretation");
    ledger.Commit(a,unorm,5,true); Check(!ledger.Find(a).Resolved(),"conflict cannot be cleared by later same view");
    ledger.Commit(b,unorm,6,false); Check(ledger.Find(b).failed&&!ledger.Find(b).Resolved(),"failed native call poisons exact generation");
    ledger.Commit(c,unorm,7,true); Check(!ledger.Find(a).key&&ledger.Find(c).Resolved()==28,"bounded ledger evicts without aliasing resource");
    Check(!ledger.Find({9,5,3,Role::Output}).key&&!ledger.Find({1,5,3,Role::Color}).key,"device/role identity isolation");
    for(unsigned format:{0u,27u}) {View bad; bad.Observe(format); Check(bad.conflict,"unknown/typeless is not observed typed evidence");}
}
void HookAndScope() {
    ID3D12Device device(&device_vtable), other(&device_vtable);
    ID3D12GraphicsCommandList list{&device}; ID3D12Resource color,output,unrelated;
    NgxParams params{&color,&output};
    Check(EnsureNrNativeFormatHooks(&device),"hook publication");
    Check(EnsureNrNativeFormatHooks(&other),"other device publication");
    for(unsigned slot=0;slot<4;++slot) {Srv(device,&color,28,0,slot); Uav(device,&output,28,0,slot);}
    Check(!Evidence(device,color,100).key&&!Evidence(device,output,100,true).key,"own/outside descriptors must not establish native evidence");
    Invoke(&list,params,100,1,[&] {
        Srv(device,&unrelated,29); Uav(device,&unrelated,29);
        Srv(other,&color,29); Uav(other,&output,29);
        Srv(device,&output,29); Uav(device,&color,29);
    });
    Check(!Evidence(device,color,100).key&&!Evidence(device,output,100,true).key,"unrelated resource/device/role excluded inside native scope");
    Invoke(&list,params,101,1,[&] {Srv(device,&color); Uav(device,&output);});
    Check(Evidence(device,color,101).Resolved()==28&&Evidence(device,output,101,true).Resolved()==28,"successful native call commits color/output");
    Invoke(&list,params,104,1,[&] {Srv(device,&color,28,8);});
    Check(Evidence(device,color,104).Resolved()==28,"all-mips SRV preserves interpretation for single-mip admission");
    Check(!g_nr_native_format_scope,"scope restored after callback");
    Srv(device,&color,29); Uav(device,&output,29);
    Check(Evidence(device,color,101).Resolved()==28,"post-native own descriptors cannot alter evidence");
    Invoke(&list,params,102,0xdead,[&] {Srv(device,&color); Uav(device,&output);});
    Check(Evidence(device,color,102).failed&&!Evidence(device,color,102).Resolved(),"native failure must not admit a view");
    Invoke(&list,params,102,1,[&] {Srv(device,&color);});
    Check(!Evidence(device,color,102).Resolved(),"later success cannot erase failed native window");
    for(unsigned variant=1;variant<=7;++variant) {
        const UINT64 generation=200+variant;
        Invoke(&list,params,generation,1,[&] {Srv(device,&color,28,variant);});
        Check(Evidence(device,color,generation).view.conflict,"invalid native SRV shape/mapping must revoke evidence");
    }
    for(unsigned variant=1;variant<=5;++variant) {
        const UINT64 generation=220+variant;
        Invoke(&list,params,generation,1,[&] {Uav(device,&output,28,variant);});
        Check(Evidence(device,output,generation,true).view.conflict,"invalid native UAV/counter must revoke evidence");
    }
    const auto old_key=NrFormatKey(&device,&color,101,false);
    {std::lock_guard guard(color.data.mutex); color.data.values.clear();}
    const auto new_key=NrFormatKey(&device,&color,101,false);
    Check(old_key.resource&&new_key.resource&&old_key.resource!=new_key.resource,"resource address reuse must get a new lifetime token");
    Check(!NrFormatEvidence(new_key).key,"recreated resource must not inherit old ledger observation");
    Check(!Evidence(device,output,103,true).key,"Feature generation reuse must not inherit old view");
    Check(device.references==1&&other.references==1,"scope balances temporary device references");
}
void Resolution() {
    ID3D12Device device(&device_vtable); ID3D12GraphicsCommandList list{&device};
    ID3D12Resource color,output; NgxParams params{&color,&output};
    EnsureNrNativeFormatHooks(&device);
    D3D12_RESOURCE_DESC desc{};
    Check(!NrResolveColorView(&device,&color,desc,400,false,0),"typeless R8 lacks interpretation before native evidence");
    for(unsigned format:{28u,29u,30u,31u}) {
        const UINT64 generation=400+format;
        Invoke(&list,params,generation,1,[&] {Srv(device,&color,format); Uav(device,&output,format);});
        const auto pre=NrResolveColorView(&device,&color,desc,generation,false,0);
        const auto post=NrResolveColorView(&device,&output,desc,generation,true,0);
        Check(Evidence(device,color,generation).Resolved()==format,"typed native evidence remains separate from codec admission");
        Check(static_cast<bool>(pre)==(format==28),"pre runtime admits UNORM only; sRGB/UINT/SNORM remain unavailable");
        Check(static_cast<bool>(post)==(format==28),"post runtime admits UNORM only; sRGB/UINT/SNORM remain unavailable");
        if(format==28) {
            Check(pre.plan.srv==format&&pre.typed.Format==format,"pre observed typed view preserved");
            Check(post.plan.srv==format&&post.plan.uav==28,"post typed view uses legal UNORM writeback");
        }
    }
    Invoke(&list,params,470,1,[&] {Srv(device,&color,28); Srv(device,&color,29);});
    Check(!NrResolveColorView(&device,&color,desc,470,false,0),"conflicting native color evidence refuses admission");
    for(unsigned incompatible:{30u,31u}) {
        const UINT64 generation=480+incompatible;
        Invoke(&list,params,generation,1,[&] {Srv(device,&color,28); Srv(device,&color,incompatible);});
        Check(Evidence(device,color,generation).view.conflict&&
              !NrResolveColorView(&device,&color,desc,generation,false,0),
              "UINT/SNORM view conflict cannot regain UNORM admission");
    }
    auto read_denied=desc; read_denied.Flags|=8;
    Check(!NrResolveColorView(&device,&color,read_denied,428,false,0),"DENY_SHADER_RESOURCE remains rejected");
    auto no_uav=desc; no_uav.Flags=0;
    Check(!NrResolveColorView(&device,&output,no_uav,428,true,0),"post without UAV cannot use SRV proof as write permission");
    for(unsigned bad=0;bad<6;++bad) {
        auto d=desc;
        if(bad==0) d.Dimension=1; if(bad==1) d.DepthOrArraySize=2;
        if(bad==2) d.MipLevels=2; if(bad==3) d.SampleDesc.Count=4;
        if(bad==4) d.Width=0;
        if(bad==5) d.Height=0;
        Check(!NrResolveColorView(&device,&color,d,428,false,0),"unsupported texture shape remains rejected");
    }
    device.query_result=-7;
    Check(!NrResolveColorView(&device,&color,desc,428,false,0),"failed capability query cannot admit native evidence");
    device.query_result=0; device.support1=0x20;
    Check(!NrResolveColorView(&device,&color,desc,428,false,0),"missing ShaderLoad remains rejected");
    device.support1=0x20|0x100|0x2000000; device.support2=0;
    Check(!NrResolveColorView(&device,&output,desc,428,true,0),"missing typed store remains rejected");
    device.support2=0x80;
    auto exact_fp16=desc; exact_fp16.Format=10; exact_fp16.Flags=8|4;
    Check(static_cast<bool>(NrResolveColorView(&device,&output,exact_fp16,490,true,0)),
          "typed FP16 post retains CopyResource-to-owned-readable contract with denied native SRV");
    Check(!NrResolveColorView(&device,&color,exact_fp16,490,false,0),
          "typed FP16 post copy exception must not admit denied pre-SR reads");
    exact_fp16.Flags=8;
    Check(!NrResolveColorView(&device,&output,exact_fp16,490,true,0),
          "typed FP16 post copy still requires legal output writeback");
}
void DiagnosticWindows() {
    ID3D12Device device(&device_vtable); ID3D12GraphicsCommandList list{&device};
    ID3D12Resource color,output; NgxParams params{&color,&output};
    NrPrepareFormatObservation(&list);
    D3D12_RESOURCE_DESC desc{};
    g_config_generation=11;
    Check(!NrResolveColorViewFromList(&list,&color,desc,810,false,0),"first diagnostic window starts without native evidence");
    auto first=SnapshotNrFormatDiagnostics();
    Check(first.latest[0].available&&first.latest[0].config==11&&first.latest[0].feature==810&&
          first.latest[0].physical==27&&first.latest[0].coverage,"diagnostic identity and coverage belong to current sample");
    Check(first.first_unknown[0].available&&first.first_unknown[0].srv==0,"first unknown stores original unresolved view");
    Invoke(&list,params,810,1,[&] {Srv(device,&color,28); Uav(device,&output,28);});
    Check(static_cast<bool>(NrResolveColorViewFromList(&list,&color,desc,810,false,0)),"current diagnostic advances after native evidence");
    const auto current=SnapshotNrFormatDiagnostics();
    Check(current.latest[0].srv==28&&current.latest[0].observed_call!=0&&
          current.first_unknown[0].srv==0,"latest success must not rewrite historical first unknown");
    g_config_generation=12;
    Check(static_cast<bool>(NrResolveColorViewFromList(&list,&output,desc,810,true,0)),"output diagnostics use independent role");
    const auto output_window=SnapshotNrFormatDiagnostics();
    Check(output_window.latest[1].config==12&&output_window.latest[1].srv==28&&
          output_window.latest[1].uav==28,"output diagnostic preserves selected views");
    Check(!NrResolveColorViewFromList(&list,&color,desc,811,false,0),"new Feature generation must collect new evidence");
    const auto next=SnapshotNrFormatDiagnostics();
    Check(next.first_unknown[0].config==12&&next.first_unknown[0].feature==811,
          "first unknown resets on config/Feature generation change");
    Check(device.references==1,"diagnostic read balances temporary device references");
}
void NestedAndThreads() {
    ID3D12Device device(&device_vtable); ID3D12GraphicsCommandList list{&device};
    ID3D12Resource outer_color,outer_output,inner_color,inner_output;
    NgxParams outer{&outer_color,&outer_output},inner{&inner_color,&inner_output};
    EnsureNrNativeFormatHooks(&device);
    Invoke(&list,outer,500,1,[&] {
        auto* parent=g_nr_native_format_scope;
        Srv(device,&outer_color,28);
        Invoke(&list,inner,501,1,[&] {
            Check(g_nr_native_format_scope!=parent,"nested native scope must have its own TLS record");
            Srv(device,&outer_color,29); Srv(device,&inner_color,29);
        });
        Check(g_nr_native_format_scope==parent,"nested successful scope restores parent");
        Uav(device,&outer_output,28);
    });
    Check(Evidence(device,outer_color,500).Resolved()==28&&Evidence(device,inner_color,501).Resolved()==29,
          "nested native views remain in their own resource/Feature window");
    Invoke(&list,outer,502,1,[&] {
        Srv(device,&outer_color,28);
        Invoke(&list,outer,0,1,[&] {Srv(device,&outer_color,29);});
    });
    Check(Evidence(device,outer_color,502).Resolved()==28,"NESTED_INACTIVE_SCOPE: unknown nested call must not contaminate parent");
    Invoke(&list,outer,503,1,[&] {
        auto* parent=g_nr_native_format_scope;
        Invoke(&list,inner,504,17,[&] {Srv(device,&inner_color,28);});
        Check(g_nr_native_format_scope==parent,"failed nested native callback restores parent TLS");
        Srv(device,&outer_color,28);
        {NrNativeFormatScope abandoned(&list,&inner,505); Srv(device,&inner_color,28);}
        Check(g_nr_native_format_scope==parent,"abandoned scope destructor restores parent TLS");
    });
    Check(Evidence(device,outer_color,503).Resolved()==28&&Evidence(device,inner_color,504).failed&&
          Evidence(device,inner_color,505).failed,"nested failure/destructor cannot poison successful parent generation");
    std::barrier rendezvous(2);
    const auto worker=[&](unsigned index) {
        ID3D12Resource color,output; NgxParams params{&color,&output};
        ID3D12GraphicsCommandList own_list{&device};
        const UINT64 generation=600+index;
        Invoke(&own_list,params,generation,1,[&] {
            auto* own=g_nr_native_format_scope;
            rendezvous.arrive_and_wait();
            Check(g_nr_native_format_scope==own,"two threads must retain separate TLS scopes");
            Srv(device,&color,index?29:28); Uav(device,&output,28);
            rendezvous.arrive_and_wait();
        });
        Check(Evidence(device,color,generation).Resolved()==(index?29u:28u),"two-thread ledger preserves each resource interpretation");
        Check(!g_nr_native_format_scope,"worker TLS restored");
    };
    std::thread first(worker,0),second(worker,1); first.join(); second.join();
    Check(!g_nr_native_format_scope&&device.references==1,"nested/concurrent scopes release temporary device references");
}
void OptionalFailures() {
    ID3D12Device device(&device_vtable); ID3D12GraphicsCommandList list{&device};
    ID3D12Resource color,output; NgxParams params{&color,&output};
    const auto cold_before=publications;
    Invoke(&list,params,699,1,[&] {Srv(device,&color,28);});
    Check(publications==cold_before&&!Evidence(device,color,699).key,
          "native callback scope must not publish hooks or infer cold coverage");
    publish_ok=false; const auto before=publications;
    Check(!EnsureNrNativeFormatHooks(&device)&&!EnsureNrNativeFormatHooks(&device),"failed publication remains unknown");
    Check(publications==before+1,"failed device publication is not retried on every frame");
    Invoke(&list,params,700,0xbeef,[]{});
    Check(!Evidence(device,color,700).key,"uncovered native call cannot publish evidence");
    publish_ok=true;
    ID3D12Device ready(&device_vtable); EnsureNrNativeFormatHooks(&ready);
    ID3D12Resource resource; resource.data.fail_set=true;
    Check(NrFormatResourceIdentity(&resource)==0,"failed resource token publication stays unknown");
    resource.data.fail_set=false; resource.data.short_get=true;
    Check(NrFormatResourceIdentity(&resource)==0,"short resource token read stays unknown");
    list.fail_device=true;
    Invoke(&list,params,701,17,[]{});
    Check(!g_nr_native_format_scope&&device.references==1,"GetDevice failure preserves native callback and reference balance");
}
int main() {
    PureLedger(); HookAndScope(); Resolution(); NestedAndThreads(); OptionalFailures(); DiagnosticWindows();
    std::printf("native format observer HOST: %u checks / %u failures; actual production ledger/runtime/hooks; no D3D12/GPU/game\n",
                checks.load(),failures.load());
    return failures?1:0;
}

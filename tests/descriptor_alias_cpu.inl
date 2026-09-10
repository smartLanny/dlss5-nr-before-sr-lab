// Included after the complete staged Core. CPU-only native/proxy regression.
// Synthetic COM objects model forwarding private data and virtual CPU handles.
// Device init and descriptor hook publication are real CPU calls. Seeded command
// Clear coverage/state are seam preconditions, not D3D installation/admission proof.
#include <map>
#include <thread>
#include <functional>
#include <vector>
#include <array>
#include <string>
#include <cstdlib>
#include "native_observer_device.inl"

namespace descriptor_alias_test {
unsigned checks=0,driver_creates=0,driver_copies=0,proxy_copies=0,driver_clears=0,private_reads=0;
void Check(bool condition,const char *label) {
    ++checks;
    if(!condition){std::printf("FAIL %s\n",label);std::exit(41);}
    std::printf("PASS %s\n",label);
}
using Key=std::array<unsigned char,sizeof(GUID)>;
Key GuidKey(REFGUID value){Key key;std::memcpy(key.data(),&value,sizeof(value));return key;}
struct Private {std::vector<unsigned char> bytes;IUnknown *object=nullptr;};
struct State {
    State *private_owner=nullptr;
    std::map<Key,Private> values;
    ULONG references=1;
    void *device=nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    D3D12_RESOURCE_DESC resource{};
    std::uint64_t cpu=0,gpu=0;
    bool descriptor_interface_unavailable=false;
};
std::map<void *,State *> objects;
State &Object(void *self){return *objects.at(self);}
State &Storage(void *self){auto *s=&Object(self);while(s->private_owner)s=s->private_owner;return *s;}
ULONG STDMETHODCALLTYPE AddRef(void *self){return ++Object(self).references;}
ULONG STDMETHODCALLTYPE Release(void *self){return --Object(self).references;}
HRESULT STDMETHODCALLTYPE Query(void *self,REFIID,void **out){if(!out)return E_POINTER;*out=self;AddRef(self);return S_OK;}
HRESULT STDMETHODCALLTYPE GetPrivate(void *self,REFGUID guid,UINT *size,void *data) {
    ++private_reads;
    if(Storage(self).descriptor_interface_unavailable && IsEqualGUID(guid,kNrDescriptorLifetime))return E_FAIL;
    if(!size)return E_POINTER;
    auto &values=Storage(self).values;const auto found=values.find(GuidKey(guid));
    if(found==values.end())return E_FAIL;
    auto &value=found->second;
    const UINT required=value.object?sizeof(IUnknown *):static_cast<UINT>(value.bytes.size());
    if(*size<required){*size=required;return HRESULT_FROM_WIN32(ERROR_MORE_DATA);}
    *size=required;if(!data)return S_OK;
    if(value.object){IUnknown_AddRef(value.object);std::memcpy(data,&value.object,sizeof(value.object));}
    else if(required)std::memcpy(data,value.bytes.data(),required);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE SetPrivate(void *self,REFGUID guid,UINT size,const void *data) {
    auto &values=Storage(self).values;const auto key=GuidKey(guid);
    IUnknown *old=nullptr;
    if(auto found=values.find(key);found!=values.end()){old=found->second.object;values.erase(found);}
    if(size&&data){Private entry;const auto *begin=static_cast<const unsigned char *>(data);entry.bytes.assign(begin,begin+size);values.emplace(key,std::move(entry));}
    if(old)IUnknown_Release(old);return S_OK;
}
HRESULT STDMETHODCALLTYPE SetInterface(void *self,REFGUID guid,const IUnknown *data) {
    if(Storage(self).descriptor_interface_unavailable && IsEqualGUID(guid,kNrDescriptorLifetime))return E_FAIL;
    auto &values=Storage(self).values;const auto key=GuidKey(guid);
    IUnknown *old=nullptr;
    if(auto found=values.find(key);found!=values.end()){old=found->second.object;values.erase(found);}
    if(data){auto *object=const_cast<IUnknown *>(data);IUnknown_AddRef(object);Private entry;entry.object=object;values.emplace(key,std::move(entry));}
    if(old)IUnknown_Release(old);return S_OK;
}
HRESULT STDMETHODCALLTYPE GetDevice(void *self,REFIID,void **out) {
    if(!out)return E_POINTER;*out=Object(self).device;
    if(!*out)return E_FAIL;AddRef(*out);return S_OK;
}
D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE HeapDesc(void *self,D3D12_DESCRIPTOR_HEAP_DESC *out){*out=Object(self).heap;return out;}
D3D12_CPU_DESCRIPTOR_HANDLE CpuBase(void *self){return {static_cast<SIZE_T>(Object(self).cpu)};}
D3D12_GPU_DESCRIPTOR_HANDLE GpuBase(void *self){return {Object(self).gpu};}
D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE CpuBaseAbi(void *self,D3D12_CPU_DESCRIPTOR_HANDLE *out){*out=CpuBase(self);return out;}
D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE GpuBaseAbi(void *self,D3D12_GPU_DESCRIPTOR_HANDLE *out){*out=GpuBase(self);return out;}
D3D12_RESOURCE_DESC *STDMETHODCALLTYPE ResourceDesc(void *self,D3D12_RESOURCE_DESC *out){*out=Object(self).resource;return out;}
UINT STDMETHODCALLTYPE Stride(void *,D3D12_DESCRIPTOR_HEAP_TYPE){return 32;}
D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE ListType(void *){return D3D12_COMMAND_LIST_TYPE_DIRECT;}
template<class Vtable> void Common(Vtable &table) {
    table.QueryInterface=reinterpret_cast<decltype(table.QueryInterface)>(&Query);
    table.AddRef=reinterpret_cast<decltype(table.AddRef)>(&AddRef);
    table.Release=reinterpret_cast<decltype(table.Release)>(&Release);
    table.GetPrivateData=reinterpret_cast<decltype(table.GetPrivateData)>(&GetPrivate);
    table.SetPrivateData=reinterpret_cast<decltype(table.SetPrivateData)>(&SetPrivate);
    table.SetPrivateDataInterface=reinterpret_cast<decltype(table.SetPrivateDataInterface)>(&SetInterface);
}
ID3D12DeviceVtbl device_table{};
ID3D12Device native_device{&device_table},proxy_device{&device_table},other_device{&device_table};
ID3D12DescriptorHeapVtbl heap_table{};
ID3D12DescriptorHeap native_cpu{&heap_table},proxy_cpu{&heap_table},native_gpu{&heap_table},proxy_gpu{&heap_table};
ID3D12ResourceVtbl resource_table{};ID3D12Resource resource{&resource_table},proxy_resource{&resource_table},other_resource{&resource_table};
ID3D12GraphicsCommandListVtbl native_list_table{},proxy_list_table{};
ID3D12GraphicsCommandList native_list{&native_list_table},proxy_list{&proxy_list_table},other_list{&native_list_table};
IUnknownVtbl pso_table{};IUnknown pso{&pso_table};
State device_state,proxy_device_state,cpu_state,proxy_cpu_state,gpu_state,proxy_gpu_state,
    resource_state,list_state,proxy_list_state,pso_state,other_device_state,proxy_resource_state,other_resource_state,other_list_state;
constexpr std::uint64_t device_identity=91;
constexpr std::uint64_t command_device_identity=1701;
struct DriverView {ID3D12Resource *resource=nullptr;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;};
std::map<std::uint64_t,DriverView> driver_descriptors;
std::function<void(D3D12_CPU_DESCRIPTOR_HANDLE)> during_copy;
D3D12_CPU_DESCRIPTOR_HANDLE Raw(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    for(const auto *state:{&proxy_cpu_state,&proxy_gpu_state})if(handle.ptr>=state->cpu&&handle.ptr-state->cpu<256)
        return {static_cast<SIZE_T>(state->private_owner->cpu+handle.ptr-state->cpu)};
    return handle;
}
void STDMETHODCALLTYPE DriverUav(ID3D12Device *,ID3D12Resource *value,ID3D12Resource *,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    ++driver_creates;driver_descriptors[destination.ptr]={value,desc?desc->Format:DXGI_FORMAT_UNKNOWN};
}
void STDMETHODCALLTYPE DriverSrv(ID3D12Device *,ID3D12Resource *value,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    ++driver_creates;driver_descriptors[destination.ptr]={value,desc?desc->Format:DXGI_FORMAT_UNKNOWN};
}
HRESULT STDMETHODCALLTYPE DriverHeap(ID3D12Device *,const D3D12_DESCRIPTOR_HEAP_DESC *desc,REFIID,void **out) {
    if(!desc || !out)return E_POINTER;
    *out=(desc->Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)?static_cast<void *>(&native_gpu):static_cast<void *>(&native_cpu);
    AddRef(*out);return S_OK;
}
void STDMETHODCALLTYPE DriverRtv(ID3D12Device *,ID3D12Resource *value,
        const D3D12_RENDER_TARGET_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    ++driver_creates;driver_descriptors[destination.ptr]={value,desc?desc->Format:DXGI_FORMAT_UNKNOWN};
}
void STDMETHODCALLTYPE DriverCbv(ID3D12Device *,const D3D12_CONSTANT_BUFFER_VIEW_DESC *,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    ++driver_creates;driver_descriptors[destination.ptr]={};
}
void STDMETHODCALLTYPE ProxyUav(ID3D12Device *,ID3D12Resource *value,ID3D12Resource *counter,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    NrFormatCreateUav<0>(&native_device,value,counter,desc,Raw(destination));
}
void STDMETHODCALLTYPE ProxySrv(ID3D12Device *,ID3D12Resource *value,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    NrFormatCreateSrv<0>(&native_device,value,desc,Raw(destination));
}
void STDMETHODCALLTYPE DriverCopy(ID3D12Device *,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE destination,
        D3D12_CPU_DESCRIPTOR_HANDLE source,D3D12_DESCRIPTOR_HEAP_TYPE) {
    ++driver_copies;for(UINT i=0;i<count;++i)driver_descriptors[destination.ptr+i*32]=driver_descriptors.at(source.ptr+i*32);
    if(during_copy)during_copy(destination);
}
void STDMETHODCALLTYPE DriverCopyRanges(ID3D12Device *,UINT destinations,const D3D12_CPU_DESCRIPTOR_HANDLE *destination,
        const UINT *destination_sizes,UINT sources,const D3D12_CPU_DESCRIPTOR_HANDLE *source,const UINT *source_sizes,
        D3D12_DESCRIPTOR_HEAP_TYPE) {
    ++driver_copies;unsigned si=0,offset=0;
    for(UINT d=0;d<destinations;++d)for(UINT i=0;i<(destination_sizes?destination_sizes[d]:1);++i) {
        while(si<sources&&offset==(source_sizes?source_sizes[si]:1)){++si;offset=0;}
        Check(si<sources,"synthetic copy receives valid source ranges unchanged");
        driver_descriptors[destination[d].ptr+i*32]=driver_descriptors.at(source[si].ptr+offset++*32);
    }
    if(during_copy&&destinations)during_copy(destination[0]);
}
void STDMETHODCALLTYPE ProxyCopy(ID3D12Device *,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE destination,
        D3D12_CPU_DESCRIPTOR_HANDLE source,D3D12_DESCRIPTOR_HEAP_TYPE type) {
    ++proxy_copies;
    NrDescriptorCopyDescriptorsSimple<0>(&native_device,count,Raw(destination),Raw(source),type);
}
std::function<void()> during_clear;
struct ClearArguments {
    ID3D12GraphicsCommandList* list=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE gpu{};D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    ID3D12Resource* resource=nullptr;const FLOAT* values=nullptr;UINT count=0;const D3D12_RECT* rects=nullptr;
} last_clear;
void STDMETHODCALLTYPE DriverClear(ID3D12GraphicsCommandList *list,D3D12_GPU_DESCRIPTOR_HANDLE gpu,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu,ID3D12Resource *value,const FLOAT *values,UINT count,const D3D12_RECT *rects) {
    last_clear={list,gpu,cpu,value,values,count,rects};
    const auto descriptor=driver_descriptors.at(cpu.ptr);
    Check(descriptor.resource==value&&descriptor.format==DXGI_FORMAT_R8G8B8A8_UNORM,
        "fake backend receives the correct raw Output UAV, no GPU involved");++driver_clears;
    if(during_clear)during_clear();
}
void STDMETHODCALLTYPE ProxyClear(ID3D12GraphicsCommandList *,D3D12_GPU_DESCRIPTOR_HANDLE gpu,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu,ID3D12Resource *value,const FLOAT *values,UINT count,const D3D12_RECT *rects) {
    PostHookClearUnorderedAccessViewFloat<0>(&native_list,gpu,Raw(cpu),value,values,count,rects);
}
void Initialize() {
    InitializeCriticalSection(&g_chain_cs);
    Common(device_table);device_table.GetDescriptorHandleIncrementSize=reinterpret_cast<decltype(device_table.GetDescriptorHandleIncrementSize)>(&Stride);
    device_table.CreateShaderResourceView=&DriverSrv;device_table.CreateUnorderedAccessView=&DriverUav;
    device_table.CreateDescriptorHeap=&DriverHeap;device_table.CreateRenderTargetView=&DriverRtv;
    device_table.CreateConstantBufferView=&DriverCbv;device_table.CopyDescriptors=&DriverCopyRanges;
    device_table.CopyDescriptorsSimple=&DriverCopy;
    Common(heap_table);heap_table.GetDevice=reinterpret_cast<decltype(heap_table.GetDevice)>(&GetDevice);
    heap_table.GetDesc=reinterpret_cast<decltype(heap_table.GetDesc)>(&HeapDesc);
    heap_table.GetCPUDescriptorHandleForHeapStart=reinterpret_cast<decltype(heap_table.GetCPUDescriptorHandleForHeapStart)>(&CpuBaseAbi);
    heap_table.GetGPUDescriptorHandleForHeapStart=reinterpret_cast<decltype(heap_table.GetGPUDescriptorHandleForHeapStart)>(&GpuBaseAbi);
    Common(resource_table);resource_table.GetDevice=reinterpret_cast<decltype(resource_table.GetDevice)>(&GetDevice);
    resource_table.GetDesc=reinterpret_cast<decltype(resource_table.GetDesc)>(&ResourceDesc);
    Common(native_list_table);native_list_table.GetDevice=reinterpret_cast<decltype(native_list_table.GetDevice)>(&GetDevice);
    native_list_table.GetType=reinterpret_cast<decltype(native_list_table.GetType)>(&ListType);proxy_list_table=native_list_table;
    pso_table.QueryInterface=reinterpret_cast<decltype(pso_table.QueryInterface)>(&Query);
    pso_table.AddRef=reinterpret_cast<decltype(pso_table.AddRef)>(&AddRef);pso_table.Release=reinterpret_cast<decltype(pso_table.Release)>(&Release);
    objects={{&native_device,&device_state},{&proxy_device,&proxy_device_state},
        {&other_device,&other_device_state},{&proxy_resource,&proxy_resource_state},{&other_resource,&other_resource_state},
        {&native_cpu,&cpu_state},{&proxy_cpu,&proxy_cpu_state},{&native_gpu,&gpu_state},{&proxy_gpu,&proxy_gpu_state},
        {&resource,&resource_state},{&native_list,&list_state},{&proxy_list,&proxy_list_state},{&other_list,&other_list_state},{&pso,&pso_state}};
    proxy_device_state.private_owner=&device_state;
    cpu_state.heap={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,8,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    gpu_state.heap=cpu_state.heap;gpu_state.heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cpu_state.device=gpu_state.device=&native_device;cpu_state.cpu=0x100000;gpu_state.cpu=0x200000;gpu_state.gpu=0x80000000;
    proxy_cpu_state.private_owner=&cpu_state;proxy_cpu_state.device=&proxy_device;proxy_cpu_state.heap=cpu_state.heap;proxy_cpu_state.cpu=0x40000000;
    proxy_gpu_state.private_owner=&gpu_state;proxy_gpu_state.device=&proxy_device;proxy_gpu_state.heap=gpu_state.heap;
    proxy_gpu_state.cpu=0x50000000;proxy_gpu_state.gpu=gpu_state.gpu;
    resource_state.device=&native_device;resource_state.resource.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_state.resource.Width=16;resource_state.resource.Height=12;resource_state.resource.DepthOrArraySize=1;
    resource_state.resource.MipLevels=1;resource_state.resource.SampleDesc.Count=1;
    resource_state.resource.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;resource_state.resource.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    proxy_resource_state.private_owner=&resource_state;proxy_resource_state.device=&proxy_device;
    proxy_resource_state.resource=other_resource_state.resource=resource_state.resource;other_resource_state.device=&native_device;
    list_state.device=other_list_state.device=&native_device;proxy_list_state.private_owner=&list_state;proxy_list_state.device=&proxy_device;
    g_nr_format_next_identity.store(device_identity);
    reshade::api::NativeObserverTestDevice owner(&native_device);
    Check(!EnsureNrDescriptorHooks(&proxy_device),"unidentified proxy cannot self-declare a canonical collector");
    OnNrFormatInitDevice(&owner);
    Check(NrFormatDeviceIdentity(&native_device)==device_identity&&NrDescriptorObserveDeviceLayer(&native_device),
        "actual init-device callback and MinHook publisher register its get_native device as collector");
    Check(NrDescriptorDeviceCoverage(&proxy_device).ready&&!NrDescriptorObserveDeviceLayer(&proxy_device),
        "shared ready receipt does not certify the proxy as the raw CPU-handle layer");
    const NrFormatDeviceReceipt other_format{device_identity+1,true};
    ID3D12Device_SetPrivateData(&other_device,kNrFormatDevice,sizeof(other_format),&other_format);
    CommandStateMetaContract command_meta;command_meta.known=true;command_meta.device_identity=command_device_identity;
    ID3D12Device_SetPrivateData(&native_device,kCommandStateMetaContract,sizeof(command_meta),&command_meta);
    // Deliberately copied number on another lifetime: equality is insufficient.
    ID3D12Device_SetPrivateData(&other_device,kCommandStateMetaContract,sizeof(command_meta),&command_meta);
    g_nr_format_uav_hooks.originals[1]=reinterpret_cast<void *>(&ProxyUav);
    g_nr_format_srv_hooks.originals[1]=reinterpret_cast<void *>(&ProxySrv);
    g_descriptor_copy_simple_hooks.originals[1]=reinterpret_cast<void *>(&ProxyCopy);
    g_post_hook_ClearUnorderedAccessViewFloat.originals[0]=reinterpret_cast<void *>(&DriverClear);
    g_post_hook_ClearUnorderedAccessViewFloat.originals[1]=reinterpret_cast<void *>(&ProxyClear);
    g_descriptor_command_publications[0]={&native_list_table,1};g_descriptor_command_publications[1]={&proxy_list_table,1};
    g_enabled_fast=true;g_config_generation=9;g_carrier_evaluations=1;
}
struct Params {
    NgxParams base{};void *table[17]{};
    ID3D12Resource *output=&resource,*color=nullptr;
    static NgxResult Get(const NgxParams *base,const char *key,ID3D12Resource **out) {
        const auto *self=reinterpret_cast<const Params *>(base);
        *out=std::strcmp(key,"Output")==0?self->output:std::strcmp(key,"Color")==0?self->color:nullptr;
        return *out?kNgxSuccess:0;
    }
    Params(){base.vtable=table;table[9]=reinterpret_cast<void *>(&Get);}
};
HRESULT STDMETHODCALLTYPE FormatSupport(ID3D12Device*,D3D12_FEATURE feature,void* data,UINT) {
    if(feature!=D3D12_FEATURE_FORMAT_SUPPORT)return E_FAIL;
    auto* support=static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT*>(data);
    support->Support1=D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD|D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    support->Support2=D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;return S_OK;
}
void NativeClearRequests() {
    Params params;NgxHandle handle{993};const FLOAT values[]={0,0,1,1};
    device_table.CheckFeatureSupport=&FormatSupport;
    const auto clear=[&](ID3D12GraphicsCommandList* list,const NgxHandle*,const NgxParams*,void*) {
        PostHookClearUnorderedAccessViewFloat<0>(list,GpuBase(&native_gpu),CpuBase(&native_cpu),&resource,values,0,nullptr);
        return kNgxSuccess;
    };
    const auto calls_before=driver_clears;
    NrCallNativeWithFormatObservation(clear,&native_list,&handle,&params.base,nullptr,800);
    Check(driver_clears==calls_before+1 && last_clear.list==&native_list &&
        last_clear.gpu.ptr==gpu_state.gpu && last_clear.cpu.ptr==cpu_state.cpu && last_clear.resource==&resource &&
        last_clear.values==values && last_clear.count==0 && !last_clear.rects,
        "Clear observer preserves all original arguments and forwards the backend exactly once");
    const auto resolved=NrResolveColorView(&native_device,&resource,resource_state.resource,800,true,0);
    Check(static_cast<bool>(resolved),"cached native Clear request supplies typed R8 CPU semantics without synchronous CreateView");
#if defined(NR_HAS_NATIVE_CLEAR_REQUEST)
    Check(resolved.clear_source && resolved.plan.srv==28 && resolved.clear_request,
        "cached Clear source is explicit and separate from synchronous CreateView");
    Check(!NrFormatEvidence(resolved.key).Resolved(),"new CPU source never writes the existing CreateView ledger");
    auto typed=resource_state.resource;typed.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    const auto exact=NrResolveColorView(&native_device,&resource,typed,800,true,0);
    Check(exact && !exact.clear_source,"existing typed-resource resolution does not acquire a Clear dependency");
    const auto prepared=resolved.clear_request;
    nr_native_clear::Invocation fresh;
    NrCallNativeWithFormatObservation(clear,&native_list,&handle,&params.base,nullptr,800,&fresh,&prepared);
    const auto pending_gate=SnapshotNrNativeClearGate();
    Check(pending_gate.required && !pending_gate.evaluated && !pending_gate.accepted && pending_gate.current_scope &&
        !std::strcmp(pending_gate.reason,"native-returned-gate-not-run"),
        "candidate preparation and native return do not claim the fresh gate already accepted");
    auto gate=[&](const nr_native_clear::Invocation& invocation) {
        return NrNativeClearMayFinish(&native_list,&resource,800,28,prepared,invocation);
    };
    Check(gate(fresh),"exact next native call with the prepared format passes the production gate");
    const auto accepted_gate=SnapshotNrNativeClearGate();
    Check(accepted_gate.required && accepted_gate.evaluated && accepted_gate.accepted && accepted_gate.current_scope &&
        accepted_gate.same_key && accepted_gate.prepared_call==prepared.call && accepted_gate.current_call==fresh.call &&
        accepted_gate.request_call==fresh.request.call && !std::strcmp(accepted_gate.reason,"accepted"),
        "actual accepted guard publishes the exact preparation and current invocation identities");
    auto gate_report=std::make_unique<FeedbackSession>();FeedbackAppendNativeClearGate(*gate_report,"cpu");
    Check(std::strstr(gate_report->report,"required:1,evaluated:1,accepted:1,currentScope:1,sameKey:1") &&
        std::strstr(gate_report->report,"reason:accepted") && SnapshotNrNativeClearGate().current_call==accepted_gate.current_call,
        "real F8 serializer reports accepted current scope without mutating gate evidence");
    g_config_generation.fetch_add(1);
    Check(!gate(fresh),"production gate rereads the current config after native return");
    *gate_report={};FeedbackAppendNativeClearGate(*gate_report,"cpu");
    Check(std::strstr(gate_report->report,"required:1,evaluated:1,accepted:0,currentScope:0") &&
        std::strstr(gate_report->report,"reason:scope-mismatch"),
        "real F8 serializer distinguishes a prepared candidate from a rejected fresh config scope");
    g_config_generation.fetch_sub(1);
    auto bad=fresh;bad.call++;Check(!gate(bad),"outer call ID must match the returned Clear declaration");
    bad=fresh;bad.request.key.feature++;Check(!gate(bad),"another Feature generation cannot authorize post work");
    bad=fresh;bad.request.key.device++;Check(!gate(bad),"another device cannot authorize post work");
    bad=fresh;bad.request.key.resource++;Check(!gate(bad),"another semantic resource cannot authorize post work");
    bad=fresh;bad.request.key.role=nr_native_format::Role::Color;Check(!gate(bad),"Color role cannot authorize Output work");
    bad=fresh;bad.request.config++;Check(!gate(bad),"another config cannot authorize post work");
    bad=fresh;bad.request.reset++;Check(!gate(bad),"another command-list reset cannot authorize post work");
    bad=fresh;bad.request.list++;Check(!gate(bad),"another command-list token cannot authorize post work");
    bad=fresh;bad.request.format=24;Check(!gate(bad),"changed current typed request rejects the prepared format");
    bad=fresh;bad.request.unknown=true;Check(!gate(bad),"unknown current Clear rejects post work");
    bad=fresh;bad.request.conflict=true;Check(!gate(bad),"conflicting current Clears reject post work");
    bad=fresh;bad.request.overflow=true;Check(!gate(bad),"overflow cannot discard a conflicting omitted Clear");
    bad=fresh;bad.request.success=false;Check(!gate(bad),"failed native call cannot authorize post work");
    bad={prepared.call,prepared};Check(!gate(bad),"cached successful call cannot stand in for this native invocation");
    // No context or slot exists here. The real function must reject before even
    // dereferencing them, therefore before depth/snapshot/encode/create/writeback.
    PostSrSubstitution blocked;blocked.prepared_clear_request=prepared;blocked.original_output=&resource;
    FeatureRecord feature{};feature.generation=800;
    Check(!FinishAfterSr(&native_list,&params.base,feature,blocked),
        "production FinishAfterSr rejects missing fresh evidence before context access or first GPU command");
    auto rewrite_cpu=[&] {
        auto value=g_nr_descriptors.Cpu(cpu_state.cpu,0,nr_descriptor::Kind::Uav).view;
        const auto write=g_nr_descriptors.BeginWrite(cpu_state.cpu,0,value);g_nr_descriptors.Finish(write);
    };
    during_clear=rewrite_cpu;
    nr_native_clear::Invocation overwritten;
    NrCallNativeWithFormatObservation(clear,&native_list,&handle,&params.base,nullptr,800,&overwritten);during_clear={};
    Check(!overwritten.request && !gate(overwritten),"descriptor overwritten inside original Clear invalidates the pre/post receipt");
    nr_native_clear::Invocation reused;
    const auto reuse=[&](ID3D12GraphicsCommandList* list,const NgxHandle* h,const NgxParams* p,void* cb) {
        clear(list,h,p,cb);rewrite_cpu();return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(reuse,&native_list,&handle,&params.base,nullptr,800,&reused);
    Check(reused.request && gate(reused),"legal CPU descriptor reuse after Clear return preserves frozen CPU request semantics");
    Check(!SnapshotNrDescriptorUse().after_native_valid,
        "GPU revision diagnostics remain independent when frozen CPU request survives later reuse");
    const auto empty=[](ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*){return kNgxSuccess;};
    nr_native_clear::Invocation none;
    NrCallNativeWithFormatObservation(empty,&native_list,&handle,&params.base,nullptr,800,&none);
    const auto ungated=SnapshotNrNativeClearGate();
    Check(!ungated.required && !ungated.evaluated && !ungated.accepted && ungated.current_scope &&
        ungated.current_call==none.call && !std::strcmp(ungated.reason,"not-required"),
        "a following ordinary native call resets historical gate acceptance to not-required");
    Check(!none.request && !gate(none),"successful native call without Clear cannot reuse an earlier request");
    Check(!NrNativeClearCandidate(prepared.key,prepared.config),"latest missing Clear invalidates the cached preparation candidate");
    nr_native_clear::Invocation nested,outer;
    const auto parent=[&](ID3D12GraphicsCommandList* list,const NgxHandle* h,const NgxParams* p,void* cb) {
        NrCallNativeWithFormatObservation(clear,list,h,p,cb,801,&nested);return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(parent,&native_list,&handle,&params.base,nullptr,800,&outer);
    Check(nested.request && nested.request.key.feature==801 && !outer.request,
        "nested native invocation cannot teach the outer scope");
    nr_native_clear::Invocation partial;
    const auto rectangle=[&](ID3D12GraphicsCommandList* list,const NgxHandle*,const NgxParams*,void*) {
        const D3D12_RECT rect{0,0,8,12};
        PostHookClearUnorderedAccessViewFloat<0>(list,GpuBase(&native_gpu),CpuBase(&native_cpu),&resource,values,1,&rect);
        Check(last_clear.count==1 && last_clear.rects==&rect && last_clear.values==values,
            "partial Clear rectangle and value pointers are forwarded unchanged");
        return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(rectangle,&native_list,&handle,&params.base,nullptr,802,&partial);
    Check(partial.request && partial.request.format==28,"partial rectangle still declares the same typed UAV without proving full Output coverage");
    nr_native_clear::Invocation overflow;
    const auto many=[&](ID3D12GraphicsCommandList* list,const NgxHandle* h,const NgxParams* p,void* cb) {
        for(unsigned i=0;i<5;++i)clear(list,h,p,cb);return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(many,&native_list,&handle,&params.base,nullptr,803,&overflow);
    Check(!overflow.request && overflow.request.overflow && overflow.request.clears==5,
        "actual fifth Clear produces explicit bounded-overflow rejection");
    nr_native_clear::Invocation failed;
    const auto failing=[&](ID3D12GraphicsCommandList* list,const NgxHandle* h,const NgxParams* p,void* cb) {
        clear(list,h,p,cb);return static_cast<NgxResult>(0xBAD);
    };
    const auto before_failed=driver_clears;
    const auto failed_result=NrCallNativeWithFormatObservation(failing,&native_list,&handle,&params.base,nullptr,804,&failed);
    Check(!failed.request && !failed.request.success && failed_result==0xBAD && driver_clears==before_failed+1,
        "failed native Evaluate preserves its exact result and one Clear but cannot publish a candidate");
    auto pair=g_nr_descriptors.UavPair(device_identity,g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&resource)),
        g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&native_gpu)).id,gpu_state.gpu,cpu_state.cpu);
    auto model=prepared;model.finished=false;model.success=false;model.clears=0;model.format=0;
    const auto good=model;
    for(unsigned field=0;field<8;++field) {
        model=good;auto changed=pair;
        auto& stamp=field<4?changed.cpu.stamp:changed.gpu.stamp;
        switch(field%4){case 0:++stamp.heap;break;case 1:++stamp.index;break;case 2:++stamp.revision;break;default:++stamp.epoch;}
        model.Observe(pair,changed);
        Check(model.unknown,"each CPU/GPU heap, index, revision and epoch must match before and after Clear");
    }
    model=good;auto changed=pair;changed.cpu.view.format=changed.gpu.view.format=30;
    model.Observe(pair,pair);model.Observe(changed,changed);
    Check(model.conflict,"two stable but different typed Clear requests conflict");
    auto evicted=pair;evicted.gpu={};evicted.gpu.shader_visible=true;
    evicted.gpu.stamp=pair.gpu.stamp;evicted.gpu.reason=nr_descriptor::Reason::SlotUnknown;
    evicted.reason=nr_descriptor::Reason::SlotUnknown;
    auto cpu_only=fresh.request;cpu_only.finished=false;cpu_only.success=false;
    cpu_only.clears=0;cpu_only.format=0;cpu_only.unknown=false;cpu_only.conflict=false;cpu_only.overflow=false;
    cpu_only.Observe(evicted,evicted,pair.gpu.stamp.heap);cpu_only.finished=true;cpu_only.success=true;
    nr_native_clear::Invocation evicted_invocation{fresh.call,cpu_only};
    Check(cpu_only && NrNativeClearMayFinish(&native_list,&resource,800,28,prepared,evicted_invocation),
        "stable CPU UAV plus explicit resource survives an evicted GPU slot and passes the fresh guard");
    auto wrong_bound=good;wrong_bound.Observe(evicted,evicted,pair.gpu.stamp.heap+1);
    Check(wrong_bound.unknown,"GPU-unknown CPU fallback rejects a mismatched bound heap identity");
    nr_descriptor::NativeUse gpu_unknown;gpu_unknown.hooks_ready=gpu_unknown.identity_known=true;
    gpu_unknown.Observe(evicted);gpu_unknown.Finish(true,g_nr_descriptors);
    Check(gpu_unknown.unknown==1&&!gpu_unknown.observed_typed&&!gpu_unknown.after_native_valid,
        "evicted GPU slot remains unknown GPU-use proof beside the CPU declaration");
    auto changed_gpu=pair;changed_gpu.gpu.stamp.revision++;
    model=good;model.Observe(pair,changed_gpu);
    Check(model.unknown,"two known GPU receipts with a changed revision reject the CPU declaration");
    model=good;changed=pair;changed.reason=nr_descriptor::Reason::SlotUnknown;
    model.Observe(pair,pair);model.Observe(changed,changed);
    Check(model.unknown,"a later unknown pair invalidates a previously known Clear in the same scope");
    auto conflicting_call=[&](ID3D12GraphicsCommandList* list,const NgxHandle* h,const NgxParams* p,void* cb) {
        clear(list,h,p,cb);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R8G8B8A8_UINT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        auto spare=CpuBase(&native_cpu);spare.ptr+=32;
        NrFormatCreateUav<0>(&native_device,&resource,nullptr,&uav,spare);return kNgxSuccess;
    };
    nr_native_clear::Invocation sync_conflict;
    NrCallNativeWithFormatObservation(conflicting_call,&native_list,&handle,&params.base,nullptr,800,&sync_conflict);
    Check(sync_conflict.request && !gate(sync_conflict),
        "current native CreateView conflicting with a frozen Clear rejects before post commands");
    Check(!NrResolveColorView(&native_device,&resource,resource_state.resource,800,true,0),
        "conflicting synchronous and Clear sources also reject the next preparation");
    const auto conflict=resolved.clear_request;
    nr_native_clear::Ledger<2> ledger;ledger.Commit(fresh.request);ledger.Commit(conflict);
    Check(ledger.Find(conflict.key,conflict.config).call==fresh.call,
        "older concurrent completion cannot replace a newer same-key receipt");
    bool isolated[2]={};
    auto worker=[&](unsigned index) {
        NrNativeFormatScope format(nullptr,nullptr,0);
        NrNativeClearScope scope(format,nullptr);
        scope.request=good;scope.request.key.feature=9000+index;scope.request.call=9000+index;
        const auto* own=g_nr_native_clear_scope;
        std::this_thread::yield();
        NrObserveNativeClearRequest(&native_list,&resource,pair,pair,0,nullptr);
        const bool own_scope=(g_nr_native_clear_scope==own && own==&scope);
        const auto invocation=scope.Finish(true);
        const auto published=NrNativeClearCandidate(scope.request.key,scope.request.config);
        isolated[index]=own_scope && invocation.request && published &&
            published.key.feature==9000+index && published.call==9000+index;
    };
    std::thread a(worker,0),b(worker,1);a.join();b.join();
    Check(isolated[0] && isolated[1] && !g_nr_native_clear_scope,"concurrent production CPU callbacks publish only their own TLS invocation and key");
#endif
}
void LedgerEvents() {
    Params params;NgxHandle handle{992};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R8G8B8A8_UNORM;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    auto create=[&](ID3D12GraphicsCommandList *list,const NgxHandle *,const NgxParams *,void *) {
        Check(list==&proxy_list,"typed CreateView observation preserves the original proxy Evaluate argument");
        NrFormatCreateUav<0>(&native_device,&resource,nullptr,&uav,CpuBase(&native_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(create,&proxy_list,&handle,&params.base,nullptr,710);
    Check(NrFormatEvidence(NrFormatKey(&native_device,&resource,710,true)).Resolved()==28,
        "actual native-layer CreateUav teaches the original ledger through proven device aliases");
    params.output=&proxy_resource;
    NrCallNativeWithFormatObservation(create,&proxy_list,&handle,&params.base,nullptr,711);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,711,true)).Resolved(),
        "a shared resource receipt does not prove native codec unwrap and cannot expand typed admission");
    const auto canonical_view=g_nr_descriptors.Cpu(cpu_state.cpu,0,nr_descriptor::Kind::Uav);
    Check(canonical_view&&NrDescriptorTrackRouteResource(&proxy_resource,device_identity,NrFormatResourceIdentity(&proxy_resource))&&
          g_nr_descriptors.Current(canonical_view)&&g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&resource)).id==canonical_view.view.resource.id&&
          !g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&proxy_resource)),
        "route tracking through a resource proxy preserves the canonical native descriptor index and current receipt");
    params.output=&resource;
    const auto before=g_nr_descriptors.SnapshotCounters();const auto creates=driver_creates;
    auto nested=[&](ID3D12GraphicsCommandList *,const NgxHandle *,const NgxParams *,void *) {
        NrFormatCreateUav<1>(&proxy_device,&resource,nullptr,&uav,CpuBase(&proxy_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(nested,&proxy_list,&handle,&params.base,nullptr,712);
    const auto after=g_nr_descriptors.SnapshotCounters();
    Check(after.writes==before.writes+1&&after.overlaps==before.overlaps&&driver_creates==creates+1,
        "nested equivalent CreateUav layers produce one write and one backend call");
    Check(NrFormatEvidence(NrFormatKey(&native_device,&resource,712,true)).Resolved()==28,
        "deduplicated actual CreateUav retains the typed ledger evidence");

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels=1;params.color=&resource;
    auto create_srv=[&](ID3D12GraphicsCommandList *,const NgxHandle *,const NgxParams *,void *) {
        NrFormatCreateSrv<1>(&proxy_device,&resource,&srv,CpuBase(&proxy_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(create_srv,&proxy_list,&handle,&params.base,nullptr,713);
    Check(NrFormatEvidence(NrFormatKey(&native_device,&resource,713,false)).Resolved()==28,
        "actual nested CreateSrv teaches Color through the exact device lifetime");
    srv.Texture2D.MipLevels=2;
    NrCallNativeWithFormatObservation(create_srv,&proxy_list,&handle,&params.base,nullptr,714);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,714,false)).Resolved(),
        "invalid SRV shape remains a conflict through proxy aliases");
    auto conflict=[&](ID3D12GraphicsCommandList *,const NgxHandle *,const NgxParams *,void *) {
        NrFormatCreateUav<0>(&native_device,&resource,nullptr,&uav,CpuBase(&native_cpu));
        auto other=uav;other.Format=DXGI_FORMAT_R8G8B8A8_UINT;
        NrFormatCreateUav<0>(&native_device,&resource,nullptr,&other,CpuBase(&native_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(conflict,&proxy_list,&handle,&params.base,nullptr,715);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,715,true)).Resolved(),
        "two actual typed interpretations remain ambiguous, never default to UNORM");
    auto failed=[&](ID3D12GraphicsCommandList *,const NgxHandle *,const NgxParams *,void *) {
        NrFormatCreateUav<0>(&native_device,&resource,nullptr,&uav,CpuBase(&native_cpu));return NgxResult{0};
    };
    NrCallNativeWithFormatObservation(failed,&proxy_list,&handle,&params.base,nullptr,716);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,716,true)).Resolved(),
        "native Evaluate failure cannot establish an alias format");

    const NrFormatDeviceReceipt copied_device{device_identity,true};
    ID3D12Device_SetPrivateData(&other_device,kNrFormatDevice,sizeof(copied_device),&copied_device);
    auto unrelated=[&](ID3D12GraphicsCommandList *,const NgxHandle *,const NgxParams *,void *) {
        NrFormatCreateUav<0>(&other_device,&resource,nullptr,&uav,CpuBase(&native_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(unrelated,&proxy_list,&handle,&params.base,nullptr,717);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,717,true)).Resolved(),
        "copied semantic device number without the same attached COM receipt is insufficient");
    const NrFormatDeviceReceipt distinct_device{device_identity+1,true};
    ID3D12Device_SetPrivateData(&other_device,kNrFormatDevice,sizeof(distinct_device),&distinct_device);
    const auto copied_resource=NrFormatResourceIdentity(&resource);
    ID3D12Resource_SetPrivateData(&other_resource,kNrFormatLifetime,sizeof(copied_resource),&copied_resource);
    auto unrelated_resource=[&](ID3D12GraphicsCommandList *,const NgxHandle *,const NgxParams *,void *) {
        NrFormatCreateUav<0>(&native_device,&other_resource,nullptr,&uav,CpuBase(&native_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(unrelated_resource,&proxy_list,&handle,&params.base,nullptr,718);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,718,true)).Resolved(),
        "copied semantic resource number cannot substitute a distinct attached resource lifetime");

    // Optional descriptor provenance may be unavailable without disabling the
    // original exact-interface typed CreateView observation path.
    const auto old_resource=g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&resource));
    ID3D12Resource_SetPrivateDataInterface(&resource,kNrDescriptorLifetime,nullptr);
    Check(!g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&resource))&&
          !g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&proxy_resource)),
        "resource retirement removes the canonical record and leaves no persistent wrapper aliases");
    resource_state.descriptor_interface_unavailable=true;
    auto exact=[&](ID3D12GraphicsCommandList *list,const NgxHandle *,const NgxParams *,void *) {
        Check(list==&native_list,"exact-interface ledger control keeps original native list");
        NrFormatCreateUav<0>(&native_device,&resource,nullptr,&uav,CpuBase(&native_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(exact,&native_list,&handle,&params.base,nullptr,719);
    Check(NrFormatEvidence(NrFormatKey(&native_device,&resource,719,true)).Resolved()==28,
        "exact native CreateUav still teaches the original ledger when descriptor lifetime attachment fails");
    NrCallNativeWithFormatObservation(create,&proxy_list,&handle,&params.base,nullptr,720);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,720,true)).Resolved(),
        "alias CreateUav requires its additional lifetime proof when that proof is unavailable");
    resource_state.descriptor_interface_unavailable=false;
    const auto replacement=NrDescriptorTrackResource(&native_device,&resource);
    Check(replacement&&replacement.id!=old_resource.id,"a replacement resource receipt never reuses the retired identity");
}

void DescriptorWindowRetirement() {
    const auto retired=NrDescriptorTrackResource(&native_device,&resource);
    SetInterface(&resource,kNrDescriptorLifetime,nullptr);
    const auto alias=NrDescriptorTrackResource(&proxy_device,&proxy_resource);
    IUnknown *receipt=nullptr;UINT bytes=sizeof(receipt);
    Check(SUCCEEDED(GetPrivate(&resource,kNrDescriptorLifetime,&bytes,&receipt))&&receipt&&
          reinterpret_cast<NrDescriptorLifetime *>(receipt)->address==reinterpret_cast<std::uint64_t>(&proxy_resource),
        "retirement fixture attaches the actual notifier first through a resource proxy");
    IUnknown_Release(receipt);
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    NrFormatCreateUav<0>(&native_device,&resource,nullptr,&desc,CpuBase(&native_cpu));
    NrNativeOutputTraceScope native_trace(&resource,&native_list);
    const auto native=native_trace.Finish();
    NrDiagnosticIdentity diagnostic{};diagnostic.frame_id=901;diagnostic.config_generation=9;
    const auto trace=PublishPostBoundary(&resource,native,diagnostic,731,reinterpret_cast<std::uint64_t>(&native_device));
    Check(trace&&alias.id!=retired.id&&g_post_boundary_current.descriptor_resource==alias.id&&
          g_post_boundary_resource.load()==reinterpret_cast<std::uint64_t>(&resource),
        "canonical native observation window holds the same unique lifetime with a different interface address");
    ForgetPostBoundaryDescriptorResource(reinterpret_cast<std::uint64_t>(&resource),retired.id);
    ForgetPostBoundaryDescriptorResource(reinterpret_cast<std::uint64_t>(&resource),alias.id+100000);
    Check(g_post_boundary_current.trace==trace,"matching raw address with stale or other lifetime tokens cannot close a current window");
    SetInterface(&proxy_resource,kNrDescriptorLifetime,nullptr);
    bool closed=false;
    for(const auto &window:g_post_boundary_history)if(window.trace==trace)
        closed=window.close==nr_post_boundary::Close::ResourceRetired&&window.descriptor_resource==alias.id;
    Check(closed&&!g_post_boundary_current.frame&&!g_post_boundary_resource.load(),
        "actual notifier retirement closes the canonical window by lifetime despite its first proxy address");
    NrFormatCreateUav<0>(&native_device,&resource,nullptr,&desc,CpuBase(&native_cpu));
    diagnostic.frame_id=902;
    const auto replacement=PublishPostBoundary(&resource,native,diagnostic,732,reinterpret_cast<std::uint64_t>(&native_device));
    ForgetPostBoundaryDescriptorResource(reinterpret_cast<std::uint64_t>(&proxy_resource),alias.id);
    Check(replacement&&g_post_boundary_current.trace==replacement&&g_post_boundary_current.descriptor_resource!=alias.id,
        "retirement of an old alias token cannot close a replacement lifetime window");
    ClosePostBoundaryAtPresent();
}
void IdentityNegatives() {
    const auto token=ReadCommandStateToken(&native_list);
    const auto original_resource=NrDescriptorTrackResource(&native_device,&resource);
    const auto original_wrapper=NrDescriptorTrackResource(&proxy_device,&proxy_resource);
    Check(original_wrapper.id==original_resource.id&&!g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&proxy_resource)),
        "resource aliases have current attached identity but no persistent wrapper address record");
    proxy_resource_state.private_owner=&other_resource_state;
    const auto reused_wrapper=NrDescriptorTrackResource(&proxy_device,&proxy_resource);
    Check(reused_wrapper&&reused_wrapper.id!=original_wrapper.id&&
          NrDescriptorTrackResource(&native_device,&resource).id==original_resource.id,
        "reused wrapper address takes its current receipt while the previous native resource stays alive");
    Params reused_params;reused_params.output=&proxy_resource;NgxHandle reused_handle{993};
    auto stale_resource=[&](ID3D12GraphicsCommandList *,const NgxHandle *,const NgxParams *,void *) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        NrFormatCreateUav<0>(&native_device,&resource,nullptr,&desc,CpuBase(&native_cpu));return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(stale_resource,&proxy_list,&reused_handle,&reused_params.base,nullptr,721);
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&proxy_resource,721,true)).Resolved(),
        "native CreateView for the old target cannot teach a reused wrapper now naming another live resource");
    proxy_resource_state.private_owner=&resource_state;
    const auto original_heap=g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&native_cpu));
    proxy_cpu_state.private_owner=&gpu_state;
    NrDescriptorTrackHeap(&proxy_device,&proxy_cpu);
    Check(!g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&proxy_cpu))&&
          !g_nr_descriptors.Cpu(proxy_cpu_state.cpu,0)&&
          g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&native_cpu)).id==original_heap.id,
        "reused heap wrapper and virtual CPU address cannot borrow the still-live old native heap domain");
    proxy_cpu_state.private_owner=&cpu_state;
    CommandStateMetaContract other_meta;other_meta.known=true;other_meta.device_identity=command_device_identity;
    CommandCreationInfo other_creation;other_creation.serial=2;other_creation.method=1;
    other_creation.initial_pso=true;other_creation.device_identity=command_device_identity;
    Check(RegisterCreatedCommandStateList(&other_list,other_meta,other_creation),"second native list gets a separate actual lifetime receipt");
    const auto other_token=ReadCommandStateToken(&other_list);
    proxy_list_state.private_owner=&other_list_state;
    {
        NrPostBoundaryAliasScope reused(&proxy_list);
        Check(SnapshotPostBoundaryList(&proxy_list).known&&SnapshotPostBoundaryList(&proxy_list).token==other_token&&other_token!=token&&
              SnapshotPostBoundaryList(&native_list).token==token,
            "reused proxy list address resolves the new lifetime while the original native list remains alive");
    }
    Check(!SnapshotPostBoundaryList(&proxy_list).known,"reused list alias also expires after the original call");
    proxy_list_state.private_owner=&list_state;
    SetInterface(&other_list,kNativeCommandLifetime,nullptr);
    Check(!NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(&proxy_device),NrDescriptorObject::Heap,device_identity),
        "shared COM receipt of the wrong object kind is rejected");
    Check(!NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(&proxy_device),NrDescriptorObject::Device,device_identity+1),
        "shared receipt for a different device identity is rejected");
    Check(!NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(&resource),NrDescriptorObject::Resource,device_identity,
          NrFormatResourceIdentity(&resource)+1),"different semantic resource lifetime is rejected");
    proxy_device_state.private_owner=nullptr;
    SetInterface(&proxy_device,kNrDescriptorLifetime,&pso);
    Check(!NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(&proxy_device),NrDescriptorObject::Device,device_identity),
        "foreign private IUnknown vtable cannot be reinterpreted as our receipt");
    SetInterface(&proxy_device,kNrDescriptorLifetime,nullptr);proxy_device_state.private_owner=&device_state;
    proxy_list_state.private_owner=nullptr;
    SetPrivate(&proxy_list,kCommandStateIdentity,sizeof(token),&token);
    SetInterface(&proxy_list,kNativeCommandLifetime,&pso);
    {
        NrPostBoundaryAliasScope foreign(&proxy_list);
        Check(!SnapshotPostBoundaryList(&proxy_list).known&&SnapshotPostBoundaryList(&native_list).known,
            "foreign command lifetime vtable is rejected while the original native record stays live");
        NrNativeOutputTraceScope trace(&resource,&native_list);
        const auto reads=private_reads;
        const FLOAT values[]={0,0,1,1};
        PostHookClearUnorderedAccessViewFloat<1>(&proxy_list,GpuBase(&proxy_gpu),CpuBase(&proxy_cpu),&resource,values,0,nullptr);
        Check((trace.Finish().coverage&nr_post_boundary::IdentityUnknown)!=0&&private_reads==reads,
            "an unknown Clear layer marks an explicit identity gap and still performs no hot COM queries");
    }
    SetInterface(&proxy_list,kNativeCommandLifetime,nullptr);
    proxy_list_state.private_owner=&list_state;
    {
        NrPostBoundaryAliasScope outer(&proxy_list);
        const auto wrong_token=token+100000;
        SetPrivate(&native_list,kCommandStateIdentity,sizeof(wrong_token),&wrong_token);
        NrPostBoundaryAliasScope nested(&proxy_list);
        const auto reads=private_reads;
        Check(!SnapshotPostBoundaryList(&proxy_list).known&&NrDescriptorBoundHeap(&proxy_list)==0,
            "wrong current token cannot borrow an outer alias or latest carrier");
        Check(private_reads==reads,"unknown alias hot rejection remains value-only");
        SetPrivate(&native_list,kCommandStateIdentity,sizeof(token),&token);
    }
    proxy_list_state.device=&other_device;
    const NrFormatDeviceReceipt same_number{device_identity,true};
    ID3D12Device_SetPrivateData(&other_device,kNrFormatDevice,sizeof(same_number),&same_number);
    {
        NrPostBoundaryAliasScope wrong_device(&proxy_list);
        Check(!SnapshotPostBoundaryList(&proxy_list).known,
            "coincident command and format device numbers on a distinct COM lifetime still reject the alias");
    }
    const NrFormatDeviceReceipt distinct_number{device_identity+1,true};
    ID3D12Device_SetPrivateData(&other_device,kNrFormatDevice,sizeof(distinct_number),&distinct_number);
    proxy_list_state.device=&proxy_device;
    {
        NrPostBoundaryAliasScope alias(&proxy_list);
        auto *record=FindCommandStateTokenLocked(token);++record->native_resets;
        Check(!SnapshotPostBoundaryList(&proxy_list).known,"reset change invalidates a prior scoped alias receipt");
        --record->native_resets;
    }
    {
        NrPostBoundaryAliasScope alias(&proxy_list);
        g_command_state_epoch.fetch_add(1,std::memory_order_release);
        Check(!SnapshotPostBoundaryList(&proxy_list).known,"registry epoch change invalidates a prior scoped alias");
    }
    {
        NrPostBoundaryAliasScope alias(&proxy_list);
        SetInterface(&native_list,kNativeCommandLifetime,nullptr);
        const auto reads=private_reads;
        Check(!SnapshotPostBoundaryList(&native_list).known&&!SnapshotPostBoundaryList(&proxy_list).known,
            "attached command lifetime retirement invalidates both native fast cache and scoped alias");
        Check(private_reads==reads,"retired-list rejection performs no hot COM calls");
    }
    Check(!ResolvePostBoundaryListForObservation(&proxy_list).known,
        "leftover token bytes after lifetime retirement cannot register a new alias");
    const auto cpu_life=g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&native_cpu)).id;
    SetInterface(&native_cpu,kNrDescriptorLifetime,nullptr);
    Check(cpu_life&&!g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&native_cpu))&&
          !g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&proxy_cpu))&&
          !g_nr_descriptors.Cpu(cpu_state.cpu,0)&&!g_nr_descriptors.Cpu(proxy_cpu_state.cpu,0),
        "heap retirement removes the native CPU domain without leaving any guessed virtual domain");
}
void InactiveClearDoesNotWaitForRegistry() {
    Check(!g_nr_descriptor_active_call&&!PostBoundaryRecordingActive(),
        "idle Clear regression runs with both observation scopes inactive");
    HANDLE complete=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(complete!=nullptr,"idle Clear completion event created");
    const auto clears=driver_clears,reads=private_reads;
    AcquireSRWLockExclusive(&g_command_state_lock);
    HANDLE worker=CreateThread(nullptr,0,[](void *event)->DWORD {
        const FLOAT values[]={0,0,1,1};
        PostHookClearUnorderedAccessViewFloat<0>(&native_list,GpuBase(&native_gpu),CpuBase(&native_cpu),
            &resource,values,0,nullptr);
        SetEvent(static_cast<HANDLE>(event));return 0;
    },complete,0,nullptr);
    const auto while_locked=worker?WaitForSingleObject(complete,1000):WAIT_FAILED;
    // Always release the lock before reporting failure. The old Snapshot path
    // must finish and fail this test, rather than leave a hung test process.
    ReleaseSRWLockExclusive(&g_command_state_lock);
    const auto joined=worker?WaitForSingleObject(worker,5000):WAIT_FAILED;
    if(worker)CloseHandle(worker);
    CloseHandle(complete);
    Check(worker&&joined==WAIT_OBJECT_0,"idle Clear worker terminates after controlled lock release");
    Check(while_locked==WAIT_OBJECT_0,"inactive Clear finishes while another thread owns the command registry lock");
    Check(driver_clears==clears+1&&private_reads==reads,
        "inactive Clear forwards the original once without any private-data lookup");
}
void CopyScopeUnrelatedOracle() {
    const auto type=static_cast<unsigned>(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const nr_descriptor::Heap unrelated{0x9001,device_identity,0x710000,0x6000000,0,1024,32,type,false};
    Check(g_nr_descriptors.ObserveHeap(unrelated),
        "scoped-copy oracle publishes an independent CPU heap");
    const auto stable_handle=unrelated.cpu+300*unrelated.stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC stable_desc{};
    stable_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    stable_desc.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    const auto stable_write=NrDescriptorBeginUav(&native_device,&resource,nullptr,&stable_desc,
        {static_cast<SIZE_T>(stable_handle)});
    g_nr_descriptors.Finish(stable_write);
    const auto stable=g_nr_descriptors.Cpu(stable_handle,type,nr_descriptor::Kind::Uav);
    Check(stable&&g_nr_descriptors.Current(stable),
        "scoped-copy oracle starts with an unrelated current CPU receipt");

    const auto seed=[&](nr_descriptor::Id base,unsigned count) {
        for(unsigned i=0;i<count;++i)
            driver_descriptors[base+static_cast<nr_descriptor::Id>(i)*unrelated.stride]={&resource,DXGI_FORMAT_R8G8B8A8_UNORM};
    };
    const auto destination_base=unrelated.cpu;
    const auto source_base=stable_handle;
    seed(destination_base,257);seed(source_base,257);
    const auto before=g_nr_descriptors.SnapshotCounters();const auto copies_before=driver_copies;
    bool target_pending=false;
    during_copy=[&](D3D12_CPU_DESCRIPTOR_HANDLE destination) {
        const auto observed=g_nr_descriptors.Cpu(destination.ptr,type,nr_descriptor::Kind::Uav);
        target_pending=observed.reason==nr_descriptor::Reason::WritePending;
    };
    NrDescriptorCopyDescriptorsSimple<0>(&native_device,257,
        {static_cast<SIZE_T>(destination_base)},{static_cast<SIZE_T>(source_base)},
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    during_copy={};

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,65> destinations{},sources{};
    std::array<UINT,65> sizes{};sizes.fill(1);
    for(unsigned i=0;i<destinations.size();++i) {
        destinations[i].ptr=destination_base+static_cast<nr_descriptor::Id>(i)*unrelated.stride;
        sources[i].ptr=source_base+static_cast<nr_descriptor::Id>(i)*unrelated.stride;
    }
    NrDescriptorCopyDescriptors<0>(&native_device,static_cast<UINT>(destinations.size()),destinations.data(),sizes.data(),
        static_cast<UINT>(sources.size()),sources.data(),sizes.data(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto after=g_nr_descriptors.SnapshotCounters();
    Check(driver_copies==copies_before+2&&target_pending&&g_nr_descriptors.Current(stable)&&
        after.scoped_copies==before.scoped_copies+2&&after.global_copies==before.global_copies&&
        after.invalidations==before.invalidations&&after.cpu_slots.active==before.cpu_slots.active&&
        after.cpu_slots.evictions==before.cpu_slots.evictions,
        "COPY_SCOPE_UNRELATED_ORACLE");
}
void Run() {
    Initialize();
    const auto failures=g_nr_descriptor_lifetime_failures.load();
    const auto device_token=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(&native_device),NrDescriptorObject::Device,device_identity);
    Check(device_token!=0,"native device lifetime receipt attaches");
    Check(NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(&proxy_device),NrDescriptorObject::Device,device_identity)==device_token,
        "device proxy resolves the exact shared private lifetime receipt");
    Check(g_nr_descriptor_lifetime_failures.load()==failures,"proven alias does not create another lifetime");
    ID3D12DescriptorHeap *created=nullptr;
    Check(SUCCEEDED(ID3D12Device_CreateDescriptorHeap(&native_device,&cpu_state.heap,IID_ID3D12DescriptorHeap,
            reinterpret_cast<void **>(&created)))&&created==&native_cpu,"real CPU hook publishes canonical nonvisible heap");
    ID3D12DescriptorHeap_Release(created);
    Check(SUCCEEDED(ID3D12Device_CreateDescriptorHeap(&native_device,&gpu_state.heap,IID_ID3D12DescriptorHeap,
            reinterpret_cast<void **>(&created)))&&created==&native_gpu,"real CPU hook publishes canonical GPU-visible heap");
    ID3D12DescriptorHeap_Release(created);
    const auto cpu_heap=g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&native_cpu));
    const auto gpu_heap=g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&native_gpu));
    Check(cpu_heap.id&&gpu_heap.id,"native CPU and GPU heap controls are tracked");
    Check(NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(&proxy_cpu),NrDescriptorObject::Heap,device_identity)==cpu_heap.id,
        "heap proxy shares the native lifetime without certifying its virtual CPU handle domain");
    NrDescriptorTrackHeap(&proxy_device,&proxy_cpu);NrDescriptorTrackHeap(&proxy_device,&proxy_gpu);
    Check(!g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(&proxy_cpu)),
        "short-lived heap wrapper addresses are not retained as canonical CPU domains");
    Check(g_nr_descriptor_lifetime_failures.load()==failures,"heap alias reuses its existing lifetime receipt");
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    NrFormatCreateUav<0>(&native_device,&resource,nullptr,&desc,CpuBase(&native_cpu));
    NrDescriptorCopyDescriptorsSimple<0>(&native_device,1,CpuBase(&native_gpu),CpuBase(&native_cpu),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto output=g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(&resource));
    const auto raw_pair=g_nr_descriptors.UavPair(device_identity,output,gpu_heap.id,gpu_state.gpu,cpu_state.cpu);
    const auto virtual_pair=g_nr_descriptors.UavPair(device_identity,output,gpu_heap.id,gpu_state.gpu,proxy_cpu_state.cpu);
    Check(raw_pair&&raw_pair.cpu.view.format==28,"native/raw CPU pair succeeds for the same known Output");
    Check(!virtual_pair&&virtual_pair.reason==nr_descriptor::Reason::HeapUnknown,
        "proxy CPU handles remain opaque; canonical hook will observe the converted raw handle");
    CommandStateMetaContract meta;meta.known=true;meta.device_identity=command_device_identity;
    CommandCreationInfo creation;creation.serial=1;creation.method=1;creation.initial_pso=true;creation.device_identity=command_device_identity;
    Check(RegisterCreatedCommandStateList(&native_list,meta,creation),"production list registration attaches its lifetime token");
    const auto token=ReadCommandStateToken(&native_list);
    auto *record=FindCommandStateTokenLocked(token);record->state=new NativeCommandState;record->state->known=true;record->hooks_ready=true;
    CommandStateAssign(record->state->pipeline,reinterpret_cast<ID3D12PipelineState *>(&pso));
    CommandStateAssign(record->state->heaps[0],&native_gpu);
    Check(ReadCommandStateToken(&proxy_list)==token,"proxy list GetPrivateData resolves the exact native lifetime token");
    auto snapshot=std::make_unique<NativeCommandState>();ID3D12GraphicsCommandList *canonical=nullptr;
    Check(CaptureNativeCommandState(&proxy_list,*snapshot,&canonical)&&canonical==&native_list,
        "production token-based capture resolves the proxy to canonical native list (synthetic state precondition)");
    ReleaseNativeCommandState(*snapshot);
    Check(SnapshotPostBoundaryList(&native_list).known&&SnapshotPostBoundaryList(&native_list).token==token,
        "native list observer control knows the registered token");
    Check(!SnapshotPostBoundaryList(&proxy_list).known,"unregistered proxy never borrows a global latest token");
    {
        NrPostBoundaryAliasScope alias(&proxy_list);
        const auto reads=private_reads;
        Check(SnapshotPostBoundaryList(&proxy_list).token==token&&SnapshotPostBoundaryList(&proxy_list).known,
            "cold exact token and attached lifetime enable only the scoped proxy alias");
        Check(SnapshotPostBoundaryList(&proxy_list).device==command_device_identity&&command_device_identity!=device_identity,
            "command and format device IDs remain distinct domains while the COM alias is proven");
        Check(NrDescriptorBoundHeap(&native_list)==gpu_heap.id&&NrDescriptorBoundHeap(&proxy_list)==gpu_heap.id,
            "native and verified proxy observe the same currently bound heap");
        Check(private_reads==reads,"hot Snapshot and BoundHeap perform no private-data calls");
    }
    Check(NrDescriptorBoundGpuHeapMatches(&native_list,gpu_heap.id,device_identity),
        "bound GPU heap helper confirms the live heap and owning device");
    Check(!NrDescriptorBoundGpuHeapMatches(&native_list,gpu_heap.id+1,device_identity),
        "an unrelated bound heap identity rejects a GPU-unknown fallback");
    Check(!NrDescriptorBoundGpuHeapMatches(&native_list,gpu_heap.id,device_identity+1),
        "a bound heap owned by another device rejects a GPU-unknown fallback");
    Check(!SnapshotPostBoundaryList(&proxy_list).known,"alias expires when its original-call scope exits");
    Params params;NgxHandle handle{991};const FLOAT values[]={0,0,1,1};
    const auto native_call=[&](ID3D12GraphicsCommandList *list,const NgxHandle *,const NgxParams *,void *) {
        Check(list==&native_list,"native control original argument is unchanged");
        const auto reads=private_reads;
        PostHookClearUnorderedAccessViewFloat<0>(list,GpuBase(&native_gpu),CpuBase(&native_cpu),&resource,values,0,nullptr);
        Check(private_reads==reads,"native Clear hot path remains free of COM identity queries");return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(native_call,&native_list,&handle,&params.base,nullptr,701);
    const auto native_use=SnapshotNrDescriptorUse();
    Check(native_use.identity_known&&native_use.matches==1&&native_use.observed_typed==28&&native_use.after_native_valid,
        "native/raw complete observer control produces a current read-only typed-use receipt");
    const auto proxy_call=[&](ID3D12GraphicsCommandList *list,const NgxHandle *,const NgxParams *,void *) {
        Check(list==&proxy_list,"proxy original Evaluate argument is preserved, never replaced with native");
        const auto reads=private_reads;
        PostHookClearUnorderedAccessViewFloat<1>(list,GpuBase(&proxy_gpu),CpuBase(&proxy_cpu),&resource,values,0,nullptr);
        Check(private_reads==reads,"both Clear layers, pairing and native-use observation are value-only hot paths");
        return kNgxSuccess;
    };
    NrCallNativeWithFormatObservation(proxy_call,&proxy_list,&handle,&params.base,nullptr,702);
    const auto proxy_use=SnapshotNrDescriptorUse();
    Check(proxy_use.hooks_ready&&proxy_use.identity_known&&proxy_use.list==token&&proxy_use.clear_calls==1&&proxy_use.unknown==0&&
          proxy_use.matches==1&&proxy_use.observed_typed==28&&proxy_use.native_success&&proxy_use.after_native_valid,
        "proxy native call yields exactly one known canonical Output clear without guessing its virtual handle");
    Check(driver_clears==2,"each original native/proxy call reaches the fake backend exactly once");
    InactiveClearDoesNotWaitForRegistry();
    const auto before=g_nr_descriptors.SnapshotCounters();const auto copies_before=driver_copies;
    NrDescriptorCopyDescriptorsSimple<1>(&proxy_device,1,CpuBase(&proxy_gpu),CpuBase(&proxy_cpu),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto after=g_nr_descriptors.SnapshotCounters();
    Check(driver_copies==copies_before+1&&proxy_copies==1,"two-layer copy forwards one actual backend operation");
    Check(after.copies==before.copies+1&&after.unknown_writes==before.unknown_writes&&after.overlaps==before.overlaps,
        "canonical collection produces one forwarded copy observation and no false write overlap");
    Check(g_nr_descriptors.Current(g_nr_descriptors.UavPair(device_identity,output,gpu_heap.id,gpu_state.gpu,cpu_state.cpu)),
        "deduplicated copy leaves the one current native descriptor revision valid");
    resource_state.device=&proxy_device;
    Check(NrDescriptorTrackRouteResource(&resource,device_identity,NrFormatResourceIdentity(&resource))&&
          g_nr_descriptor_lifetime_failures.load()==failures,
        "route resource accepts the proven forwarded device lifetime");
    resource_state.device=&native_device;
    Check(!NrFormatEvidence(NrFormatKey(&native_device,&resource,701,true)).Resolved()&&
          !NrFormatEvidence(NrFormatKey(&native_device,&resource,702,true)).Resolved(),
        "neither cached-use control nor successful proxy clear promotes diagnostics into the admission ledger");
    NativeClearRequests();LedgerEvents();CopyScopeUnrelatedOracle();DescriptorWindowRetirement();IdentityNegatives();
    std::printf("PASS %u assertions; full production TU; synthetic COM CPU only; D3D devices=0 GPU submits=0 NGX=0\n",checks);
}
}

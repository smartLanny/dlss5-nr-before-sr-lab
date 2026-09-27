#include <cstring>
#include "recorded_temporal.hpp"
#include "amd_of_budget.hpp"
#include "residual_accumulator_shader.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <mutex>
#include <stdexcept>
#include <string>
#include <memory>
#include <utility>
#include <cstdint>
#include <dxgi1_4.h>
#ifdef NR_TEMPORAL_PRECOMPILED
#include "nr_temporal_precompiled.hpp"
#endif

namespace nr::final_residual {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto cs=D3D12_RESOURCE_STATE_COPY_SOURCE;
constexpr auto sr=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto ua=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void checked(HRESULT h,const char* s){need(SUCCEEDED(h),s);}
bool device_is(ID3D12DeviceChild* p,ID3D12Device* d){
    ComPtr<ID3D12Device> actual;return p&&SUCCEEDED(p->GetDevice(IID_PPV_ARGS(&actual)))&&actual.Get()==d;
}
bool reached(ID3D12Fence* f,Id v){if(!v)return true;if(!f)return false;const auto n=f->GetCompletedValue();return n!=UINT64_MAX&&n>=v;}
void state(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&t);
}
bool region(ID3D12Resource* p,ID3D12Device* d,DXGI_FORMAT f,unsigned x,unsigned y,unsigned w,unsigned h){
    if(!device_is(p,d)||!w||!h)return false;const auto a=p->GetDesc();
    return a.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&a.MipLevels==1&&a.DepthOrArraySize==1&&
        a.SampleDesc.Count==1&&a.Format==f&&x<=a.Width&&y<=a.Height&&w<=a.Width-x&&h<=a.Height-y;
}
bool target_format(DXGI_FORMAT f){return f==DXGI_FORMAT_R16G16B16A16_FLOAT||f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R11G11B10_FLOAT;}
struct Constants {
    unsigned width,height,accumulate,later;float weight,tolerance;
    unsigned flow_width,flow_height,flow_x,flow_y;float unit_x,unit_y;
    unsigned amd,age,target_x,target_y;
    unsigned hdr,target_kind;float source_to_scene,previous_to_scene;
    unsigned validation,mode,depth_enabled,stats_enabled;
};
static_assert(sizeof(Constants)==24*4);

// TEMPORAL_CACHE_PORTABLE_BEGIN
// Fixed keys describe code compiled into this module, never external shader text.
// A failed build is not published. Serialized creation prevents duplicate work.
template<class T, std::size_t Count> class ImmutableArtifactCache {
    std::mutex mutex_;
    std::array<std::shared_ptr<const T>,Count> entries_{};
public:
    template<class Build> std::shared_ptr<const T> get(std::size_t key,Build&& build) {
        if(key>=Count)throw std::out_of_range("temporal artifact cache key");
        std::lock_guard<std::mutex> lock(mutex_);
        if(!entries_[key]) {
            auto value=build();
            if(!value)throw std::runtime_error("empty temporal artifact");
            entries_[key]=std::move(value);
        }
        return entries_[key];
    }
};
// GPU objects are shared only while an owning context retains them. Weak entries
// cannot keep an unused device/PSO resident, nor authorize early context release.
template<class T, std::size_t Count> class LiveArtifactCache {
    std::mutex mutex_;
    std::array<std::weak_ptr<T>,Count> entries_{};
public:
    template<class Match,class Build> std::shared_ptr<T> get(Match&& match,Build&& build) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t empty=Count;
        for(std::size_t i=0;i<Count;++i) {
            if(auto value=entries_[i].lock()) {
                if(match(*value))return value;
            } else if(empty==Count)empty=i;
        }
        auto value=build();
        if(!value)throw std::runtime_error("empty temporal live artifact");
        if(empty<Count)entries_[empty]=value;
        // Saturation does not invalidate another live context. Return uncached.
        return value;
    }
};
// Context-owned, serialized by the existing owner contract. Hardware format
// capabilities are immutable for its retained device; transient failures are
// NOT published. No per-frame allocation, global lock, or cross-device reuse.
template<class T, std::size_t Count> class SuccessfulQueryCache {
    std::array<T,Count> entries_{};
    std::array<bool,Count> valid_{};
public:
    template<class Query> bool get(std::size_t key,T& out,Query&& query) {
        if(key>=Count)return false;
        if(!valid_[key]) {
            T value{};
            if(!query(value))return false;
            entries_[key]=value;valid_[key]=true;
        }
        out=entries_[key];return true;
    }
};
constexpr bool temporal_inplace_choice(bool enabled,bool exact_fp16,bool typed_load) {
    return enabled&&exact_fp16&&typed_load;
}
constexpr bool temporal_fits_increment(std::uint64_t budget,std::uint64_t used,std::uint64_t extra) {
    return budget&&extra&&used<=budget&&extra<=budget-used;
}
// TEMPORAL_CACHE_PORTABLE_END

bool temporal_env_equals(const wchar_t* name,wchar_t expected) {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(name,value,2)==1&&value[0]==expected;
}
// Process-start controls for independent A/B tests; no INI/UI/default changes.
// The new pixel path stays opt-in until Windows/WARP/NVIDIA parity is recorded.
bool temporal_inplace_requested() {
    static const bool value=temporal_env_equals(L"DLSS5_TEMPORAL_INPLACE_FP16",L'1');
    return value;
}
bool temporal_cache_enabled() {
    static const bool value=!temporal_env_equals(L"DLSS5_TEMPORAL_PIPELINE_CACHE",L'0');
    return value;
}
using ShaderBlob=ComPtr<ID3DBlob>;
struct TemporalBytecode {
    D3D12_SHADER_BYTECODE code{};
    std::shared_ptr<const ShaderBlob> owner; // Only the standalone compiler path owns a blob.
};
TemporalBytecode temporal_bytecode(unsigned variant) {
    need(variant<3,"temporal shader variant");
#ifdef NR_TEMPORAL_PRECOMPILED
    const auto blob=nr_temporal_precompiled::Find(variant);
    need(blob.data&&blob.size,"required temporal build artifact missing");
    return {{blob.data,blob.size},{}};
#else
    // Independent probe/reference builds retain their original dynamic compiler.
    // Production builds require the generated header and cannot silently compile.
    static ImmutableArtifactCache<ShaderBlob,3> cache;
    auto build=[&]() {
        ShaderBlob code,error;
        const D3D_SHADER_MACRO macros[]={{"RECORDING","1"},
            {variant==2?"INPLACE_FP16":nullptr,variant==2?"1":nullptr},{nullptr,nullptr}};
        const auto hr=D3DCompile(residual_accumulator_shader,sizeof(residual_accumulator_shader)-1,
            "recorded-temporal",macros,nullptr,variant==0?"CopyCandidate":"CS","cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(hr)&&error)throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));
        checked(hr,"temporal shader");
        return std::make_shared<const ShaderBlob>(code);
    };
    auto owner=temporal_cache_enabled()?cache.get(variant,build):build();
    const D3D12_SHADER_BYTECODE code={(*owner)->GetBufferPointer(),(*owner)->GetBufferSize()};
    return {code,std::move(owner)};
#endif
}
struct TemporalPipelines {
    ComPtr<ID3D12Device> device; // Strong exact identity while contexts use the bundle.
    bool requested_inplace=false;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> copy,accumulate,inplace;
};
std::shared_ptr<TemporalPipelines> temporal_pipelines(ID3D12Device* d,bool inplace) {
    static LiveArtifactCache<TemporalPipelines,4> cache;
    need(d&&d->GetDeviceRemovedReason()==S_OK,"temporal pipeline device unavailable");
    auto build=[&]() {
        auto out=std::make_shared<TemporalPipelines>();out->device=d;out->requested_inplace=inplace;
        D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,11,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,6,0,0,11}};
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable={2,ranges};parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants={0,0,24};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
        ShaderBlob code,error;checked(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error),"temporal root serialization");
        checked(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&out->root)),"temporal root");
        auto pipeline=[&](unsigned variant,ComPtr<ID3D12PipelineState>& pso) {
            const auto blob=temporal_bytecode(variant);
            D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=out->root.Get();
            p.CS=blob.code;
            checked(d->CreateComputePipelineState(&p,IID_PPV_ARGS(&pso)),"temporal pipeline");
        };
        pipeline(0,out->copy);pipeline(1,out->accumulate);
        if(inplace) {
            // An optional shader/PSO failure retains the established snapshot path.
            try {pipeline(2,out->inplace);}catch(const std::exception&) {out->inplace.Reset();}
        }
        return out;
    };
    return temporal_cache_enabled()?cache.get([&](const TemporalPipelines& p) {
        return p.device.Get()==d&&p.requested_inplace==inplace;
    },build):build();
}
bool temporal_fp16_load_supported(ID3D12Device* d) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format{};format.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    return SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,&options,sizeof(options)))&&
        options.TypedUAVLoadAdditionalFormats&&
        SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&format,sizeof(format)))&&
        (format.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)&&
        (format.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
}
void temporal_uav_barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource=resource;list->ResourceBarrier(1,&barrier);
}
// Used only for the opt-in path's first fallback allocation, never each frame.
// Original admission checked the full legacy footprint; it was not an exclusive
// reservation. Recheck now because the process budget may have changed.
bool temporal_snapshot_headroom(ID3D12Device* d,ID3D12CommandQueue* q,Id bytes) {
    if(q->GetDesc().NodeMask>1)return false; // Existing textures are node zero.
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter3> adapter;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||
       FAILED(factory->EnumAdapterByLuid(d->GetAdapterLuid(),IID_PPV_ARGS(&adapter))))return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    return SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))&&
        temporal_fits_increment(info.Budget,info.CurrentUsage,bytes);
}
}
struct RecordedTemporal::Impl {
    struct Registry {std::mutex mutex;std::array<Impl*,4> objects{};std::array<bool,4> retired{};};
    static Registry& registry(){static auto* r=new Registry;return *r;}
    struct Slot {
        ComPtr<ID3D12DescriptorHeap> heap;
        std::array<ComPtr<ID3D12Resource>,4> inputs;
        ComPtr<ID3D12Resource> gpu_stats,stats_readback;
        ComPtr<ID3D12DescriptorHeap> stats_clear;
        bool stats_pending=false,stats_submitted=false;
        Id stats_configuration=0,stats_owner=0;
        ComPtr<ID3D12Fence> fence;Id value=0,recording=0;
    };
    int registry_slot=-1,pending_slot=-1;
    unsigned width=0,height=0,next_slot=0,index=1;
    Backend backend=Backend::None;Scope scope=Scope::WholeChain;
    bool ready=false,fault=false,recording_unsafe=false,retiring=false;
    std::string message="not initialized";
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> copy_pso,accumulate_pso;
    std::shared_ptr<TemporalPipelines> pipelines;
    SuccessfulQueryCache<D3D12_FEATURE_DATA_FORMAT_SUPPORT,3> candidate_formats;
    bool inplace_enabled=false,snapshot_refused=false;
    Id texture_bytes=0,granted_budget=0;
    std::array<ComPtr<ID3D12Resource>,2> originals,residuals,depths;
    ComPtr<ID3D12Resource> snapshot,first;
    std::array<Slot,3> slots;
    std::unique_ptr<AmdOfProducer> amd;
    HistoryContract history;Plan plan{};Frame pending_frame{},previous{};
    bool pending_depth=false,previous_depth=false;
    TemporalStats counters{};
    D3D12_RESOURCE_DESC description(){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;return d;}
    ComPtr<ID3D12Resource> texture(D3D12_RESOURCE_STATES initial=cs){
        auto d=description();D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> r;
        checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,initial,nullptr,IID_PPV_ARGS(&r)),"temporal texture allocation");
        return r;
    }
    ComPtr<ID3D12Resource> buffer(D3D12_HEAP_TYPE heap_type,D3D12_RESOURCE_STATES initial,bool uav){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=32;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        const auto bytes=device->GetResourceAllocationInfo(0,1,&d).SizeInBytes;
        need(temporal_fits_increment(granted_budget,counters.bytes,bytes),"GPU statistics allocation budget");
        D3D12_HEAP_PROPERTIES hp{};hp.Type=heap_type;ComPtr<ID3D12Resource> r;
        checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,initial,nullptr,IID_PPV_ARGS(&r)),"GPU statistics buffer");
        counters.bytes+=bytes;return r;
    }
    void poll_stats(){
        for(auto& s:slots){if(!s.stats_pending||!s.stats_submitted||!s.fence||!reached(s.fence.Get(),s.value))continue;
            unsigned data[8]{};void* mapped=nullptr;D3D12_RANGE read{0,sizeof(data)};
            if(FAILED(s.stats_readback->Map(0,&read,&mapped))||!mapped)continue;
            std::memcpy(data,mapped,sizeof(data));D3D12_RANGE none{0,0};s.stats_readback->Unmap(0,&none);s.stats_pending=false;
            const Id pixels=Id(width)*height;
            if(data[0]>pixels||data[1]>data[0]||data[2]>data[0]||data[3]>pixels||data[4]>data[2])continue;
            if(s.stats_configuration<counters.gpu_configuration)continue;
            if(s.stats_configuration!=counters.gpu_configuration||s.stats_owner!=counters.gpu_owner){
                counters.gpu_configuration=s.stats_configuration;counters.gpu_owner=s.stats_owner;
                counters.gpu_frames=counters.gpu_tested=counters.gpu_adopted=counters.gpu_rejected=counters.gpu_warmup=counters.gpu_depth_rejected=0;
            }
            ++counters.gpu_frames;counters.gpu_tested+=data[0];counters.gpu_adopted+=data[1];
            counters.gpu_rejected+=data[2];counters.gpu_warmup+=data[3];counters.gpu_depth_rejected+=data[4];
        }
    }
    void init(ID3D12Device* d,ID3D12CommandQueue* q,unsigned w,unsigned h,Backend b,Scope s,Id budget){
        need(d&&device_is(q,d)&&q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,"temporal same-device DIRECT queue required");
        need(w&&h&&w<=4096&&h<=4096,"temporal bounded active rectangle required");
        need(b==Backend::NativeMotion||b==Backend::AmdOpticalFlow,"temporal backend unavailable");
        need(s==Scope::WholeChain||s==Scope::LaterLayers,"temporal scope unavailable");
        device=d;queue=q;width=w;height=h;backend=b;scope=s;
        auto td=description();const auto one=d->GetResourceAllocationInfo(0,1,&td).SizeInBytes;
        need(one&&one!=UINT64_MAX,"temporal allocation estimate");
        texture_bytes=one;granted_budget=budget;
        const Id own=one*(scope==Scope::LaterLayers?6:5);
        // Query the pinned SDK resource layout. A flat 64 bytes/output-pixel
        // allowance falsely rejected every 4K context under the 768 MiB cap.
        const Id sdk_allowance=backend==Backend::AmdOpticalFlow?
            AmdBorrowedCreationAllowance(w,h,[&](unsigned x,unsigned y,unsigned format){
                auto resource=description();resource.Width=x;resource.Height=y;resource.Format=DXGI_FORMAT(format);
                return d->GetResourceAllocationInfo(0,1,&resource).SizeInBytes;
            }):0;
        need(backend!=Backend::AmdOpticalFlow||sdk_allowance,"temporal SDK allocation estimate unavailable");
        need(own<=budget&&sdk_allowance<=budget-own,"temporal creation budget exhausted");
        pipelines=temporal_pipelines(d,temporal_inplace_requested()&&temporal_fp16_load_supported(d));
        root=pipelines->root;copy_pso=pipelines->copy;accumulate_pso=pipelines->accumulate;
        inplace_enabled=pipelines->inplace.Get()!=nullptr;
        if(backend==Backend::AmdOpticalFlow){
            amd=std::make_unique<AmdOfProducer>(d,q,w,h,100,true);need(amd->initialized(),amd->status());
            need(amd->gpu_allocation_bytes()<=budget-own,"actual FFX allocation exceeds temporal budget");
        }
        for(auto& r:originals)r=texture();for(auto& r:residuals)r=texture();
        if(!inplace_enabled)snapshot=texture();
        if(scope==Scope::LaterLayers)first=texture(ua);
        counters.bytes=own-(inplace_enabled?one:0)+(amd?amd->gpu_allocation_bytes():0);
        counters.inplace_available=inplace_enabled;
        counters.snapshot_bytes=snapshot?one:0;
        if(backend==Backend::NativeMotion){
            auto depth_desc=description();depth_desc.Format=DXGI_FORMAT_R32_FLOAT;
            const auto depth_bytes=d->GetResourceAllocationInfo(0,1,&depth_desc).SizeInBytes;
            need(depth_bytes&&depth_bytes!=UINT64_MAX&&depth_bytes<=UINT64_MAX/2&&temporal_fits_increment(budget,counters.bytes,2*depth_bytes),"depth history allocation budget");
            D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
            for(auto& depth:depths){checked(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&depth_desc,cs,nullptr,IID_PPV_ARGS(&depth)),"same-source depth history");counters.bytes+=depth_bytes;}
        }
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=17;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        for(auto& slot:slots){checked(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&slot.heap)),"temporal descriptor set");
            slot.gpu_stats=buffer(D3D12_HEAP_TYPE_DEFAULT,ua,true);slot.stats_readback=buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,false);
            auto clear=hd;clear.NumDescriptors=1;clear.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            checked(d->CreateDescriptorHeap(&clear,IID_PPV_ARGS(&slot.stats_clear)),"GPU statistics CPU descriptor");
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;view.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.NumElements=8;view.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
            d->CreateUnorderedAccessView(slot.gpu_stats.Get(),nullptr,&view,slot.stats_clear->GetCPUDescriptorHandleForHeapStart());
        }
        ready=true;message="ready; no temporal work recorded";
    }
    bool ensure_snapshot() {
        if(snapshot)return true;
        if(snapshot_refused)return false;
        if(!temporal_fits_increment(granted_budget,counters.bytes,texture_bytes)||
           !temporal_snapshot_headroom(device.Get(),queue.Get(),texture_bytes)) {
            snapshot_refused=true;counters.snapshot_refused=true;return false;
        }
        try {snapshot=texture();}catch(const std::exception&) {
            snapshot_refused=true;counters.snapshot_refused=true;return false;
        }
        counters.bytes+=texture_bytes;counters.snapshot_bytes=texture_bytes;
        return true;
    }
    bool quarantined() const {
        if(recording_unsafe||pending_slot>=0)return true;
        for(const auto& s:slots)if(s.fence&&s.fence->GetCompletedValue()==UINT64_MAX)return true;
        return false;
    }
    bool destroyable() const {if(quarantined())return false;for(const auto& s:slots)if(s.recording||!reached(s.fence.Get(),s.value))return false;return true;}
    void fail(const char* why){fault=true;ready=false;counters.faulted=true;message=why;}
    void bind(const RecordedInputs& in,const RecordedFlow& flow,unsigned next,bool inplace){
        auto cpu=slots[next_slot].heap->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto srv=[&](ID3D12Resource* r,DXGI_FORMAT format){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=format;
            v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(r,&v,cpu);cpu.ptr+=step;};
        const auto fp=DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv(in.original,in.original->GetDesc().Format);srv(inplace?nullptr:snapshot.Get(),fp);
        srv(backend==Backend::NativeMotion?in.native.resource:nullptr,backend==Backend::NativeMotion?in.native.format:DXGI_FORMAT_R32G32_FLOAT);
        srv(originals[index].Get(),fp);srv(residuals[index].Get(),fp);srv(first.Get(),fp);
        srv(flow.grid,DXGI_FORMAT_R16G16_SINT);srv(flow.scene_data,DXGI_FORMAT_R32_UINT);
        // No SRV aliases the UAV target in the in-place shader variant.
        srv(inplace?nullptr:in.candidate,in.candidate_format);
        srv(in.native.depth,DXGI_FORMAT_R32_FLOAT);srv(depths[index].Get(),DXGI_FORMAT_R32_FLOAT);
        auto uav=[&](ID3D12Resource* r,DXGI_FORMAT format){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(r,nullptr,&v,cpu);cpu.ptr+=step;};
        uav(residuals[next].Get(),fp);uav(in.candidate,in.candidate_format);uav(snapshot.Get(),fp);uav(originals[next].Get(),fp);uav(depths[next].Get(),DXGI_FORMAT_R32_FLOAT);
        D3D12_UNORDERED_ACCESS_VIEW_DESC raw{};raw.Format=DXGI_FORMAT_R32_TYPELESS;raw.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        raw.Buffer.NumElements=8;raw.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(slots[next_slot].gpu_stats.Get(),nullptr,&raw,cpu);
    }
};
RecordedTemporal::RecordedTemporal(ID3D12Device* d,ID3D12CommandQueue* q,unsigned w,unsigned h,Backend b,Scope s,Id budget):p_(std::make_unique<Impl>()){
    collect_retired();auto& r=Impl::registry();
    {std::lock_guard<std::mutex> lock(r.mutex);for(unsigned i=0;i<r.objects.size();++i)if(!r.objects[i]){r.objects[i]=p_.get();p_->registry_slot=int(i);break;}}
    if(p_->registry_slot<0){p_->fail("temporal context capacity exhausted");return;}
    try{p_->init(d,q,w,h,b,s,budget);}catch(const std::exception& e){p_->fail(e.what());}
}
RecordedTemporal::~RecordedTemporal(){if(!p_||p_->registry_slot<0)return;auto& r=Impl::registry();
    {std::lock_guard<std::mutex> lock(r.mutex);r.retired[unsigned(p_->registry_slot)]=true;(void)p_.release();}collect_retired();}
RetirementStats RecordedTemporal::collect_retired() noexcept {
    auto& r=Impl::registry();std::lock_guard<std::mutex> lock(r.mutex);RetirementStats stats;
    for(unsigned i=0;i<r.objects.size();++i){auto* p=r.objects[i];if(!p)continue;
        if(!r.retired[i]){++stats.active;continue;}if(p->destroyable()){delete p;r.objects[i]=nullptr;r.retired[i]=false;continue;}
        if(p->quarantined())++stats.quarantined;else ++stats.pending;
    }AmdOfProducer::collect_retired();return stats;
}
bool RecordedTemporal::initialized()const noexcept{return p_->ready&&!p_->fault;}
bool RecordedTemporal::releasable()const noexcept{return p_->destroyable();}
const char* RecordedTemporal::status()const noexcept{return p_->message.c_str();}
TemporalStats RecordedTemporal::stats()const noexcept{p_->poll_stats();return p_->counters;}
ID3D12Resource* RecordedTemporal::anchor()const noexcept{return p_->first.Get();}
bool RecordedTemporal::can_record()const noexcept {
    const auto& p=*p_;return p.ready&&!p.fault&&!p.retiring&&p.pending_slot<0&&
        !p.slots[p.next_slot].recording&&reached(p.slots[p.next_slot].fence.Get(),p.slots[p.next_slot].value)&&(!p.amd||p.amd->can_record());
}
RecordResult RecordedTemporal::record(ID3D12GraphicsCommandList* l,const RecordedInputs& in)noexcept {
    auto& p=*p_;p.poll_stats();if(!in.settings.enabled)return RecordResult::Disabled;
    if(p.fault||!p.ready)return RecordResult::Faulted;
    if(p.previous.source.valid()&&in.frame.source==p.previous.source)return RecordResult::Duplicate;
    if(!can_record()){p.message="temporal recording or GPU resource slot is still occupied";return RecordResult::Busy;}
    const auto& frame=in.frame;const bool later=p.scope==Scope::LaterLayers;
    const bool hdr=frame.contract.color_domain==kResidualLinearScene;
    const auto original_format=in.original?in.original->GetDesc().Format:DXGI_FORMAT_UNKNOWN;
    const bool packed=original_format==DXGI_FORMAT_R11G11B10_FLOAT;
    const bool copy_pixels=in.original&&(packed||in.original->GetDesc().Width!=p.width||in.original->GetDesc().Height!=p.height);
    if(!in.settings.valid()||in.settings.scope!=p.scope||!frame.valid()||frame.contract.backend!=p.backend||frame.contract.scope!=p.scope||
        (frame.contract.color_domain!=kResidualSdrCodeValues&&frame.contract.color_domain!=kResidualLinearSdr&&!hdr)||
        (hdr&&(!std::isfinite(frame.source_to_scene)||frame.source_to_scene<1.f/1024.f||frame.source_to_scene>1024.f||in.candidate_format==DXGI_FORMAT_R8G8B8A8_UNORM))||
        frame.contract.width!=p.width||frame.contract.height!=p.height||
        in.layers<1||in.layers>5||(later&&(in.layers<2||!(in.anchor_source==frame.source)))||
        !device_is(l,p.device.Get())||l->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||
        (original_format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&!(hdr&&packed))||
        !region(in.original,p.device.Get(),original_format,0,0,p.width,p.height)||
        !target_format(in.candidate_format)||!region(in.candidate,p.device.Get(),in.candidate_format,in.candidate_x,in.candidate_y,p.width,p.height)||
        !(in.candidate->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||in.candidate==in.original){
        p.message="temporal source, scope, colour/exposure or active rectangle unavailable";return RecordResult::Unavailable;
    }
    if(p.backend==Backend::NativeMotion){const auto& m=in.native;
        if(!(m.source==frame.source)||!m.phase_proven||!std::isfinite(m.scale_x)||!std::isfinite(m.scale_y)||m.scale_x==0||m.scale_y==0||
            (m.format!=DXGI_FORMAT_R16G16_FLOAT&&m.format!=DXGI_FORMAT_R32G32_FLOAT)||
            !region(m.resource,p.device.Get(),m.format,m.x,m.y,m.width,m.height)||m.resource==in.original||m.resource==in.candidate||
            (m.depth&&(!m.depth_phase_proven||!(m.depth_source==frame.source)||m.x||m.y||
            !region(m.depth,p.device.Get(),DXGI_FORMAT_R32_FLOAT,0,0,m.width,m.height)||m.depth==in.original||m.depth==in.candidate||m.depth==m.resource))){
            p.message="native motion identity, phase, units or readable format unavailable";return RecordResult::Unavailable;
        }
    }
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    const unsigned format_key=in.candidate_format==DXGI_FORMAT_R16G16B16A16_FLOAT?0:
        in.candidate_format==DXGI_FORMAT_R8G8B8A8_UNORM?1:2;
    if(!p.candidate_formats.get(format_key,support,[&](auto& value){
            value.Format=in.candidate_format;
            return SUCCEEDED(p.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&value,sizeof(value)));
        })||
        !(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)||!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)){
        p.message="temporal candidate typed read/write unavailable";return RecordResult::Unavailable;
    }
    const bool inplace=in.settings.validation==Validation::Legacy&&temporal_inplace_choice(p.inplace_enabled,
        in.candidate_format==DXGI_FORMAT_R16G16B16A16_FLOAT,
        (support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)!=0);
    if(!inplace&&!p.ensure_snapshot()) {
        p.message="temporal snapshot fallback allocation refused; current candidate retained; collect F8 feedback";
        return RecordResult::Unavailable;
    }
    Frame next_frame=frame;
    const double delta=p.previous.valid()&&frame.source_time>p.previous.source_time?double(frame.source_time-p.previous.source_time)/1000.:0;
    if(delta>in.settings.maximum_gap_ms)next_frame.scene_reset=true;
    p.plan=p.history.prepare(next_frame);
    if(!p.plan.work()){p.message="temporal source identity did not admit a new frame";return p.plan.action==Action::Duplicate?RecordResult::Duplicate:RecordResult::Unavailable;}
    try {
        RecordedFlow flow{};
        if(p.amd){const auto result=p.amd->record(next_frame,p.plan,l,in.original,in.original_state,flow);
            if(result!=Submit::Recorded){p.history.cancel_before_flow(p.plan.token);p.plan={};
                if(result==Submit::Faulted){p.recording_unsafe=true;p.fail(p.amd->status());return RecordResult::Faulted;}
                p.message=p.amd->status();return RecordResult::Unavailable;
            }
            p.recording_unsafe=true;
        }
        need(p.history.observe_ordered_recording(p.plan.token,{p.backend,p.plan.history_generation,frame.source,p.plan.reference,p.plan.action==Action::ResetBoth}),"temporal recording contract rejected");
        const unsigned next=1-p.index;p.bind(in,flow,next,inplace);
        auto& slot=p.slots[p.next_slot];slot.inputs={in.original,in.candidate,in.native.resource,in.native.depth};slot.stats_pending=slot.stats_submitted=false;
        p.pending_depth=in.native.depth!=nullptr;slot.stats_configuration=frame.contract.config_generation>>32;slot.stats_owner=frame.contract.device_generation;
        p.pending_slot=int(p.next_slot);p.recording_unsafe=true;
        const float weight=HistoryWeight(in.settings,delta);
        Constants c{p.width,p.height,unsigned(p.plan.action==Action::Continue&&weight>0&&(!in.native.depth||p.previous_depth)),unsigned(later),weight,in.settings.color_tolerance,
            p.amd?flow.flow_width:in.native.width,p.amd?flow.flow_height:in.native.height,
            p.amd?0:in.native.x,p.amd?0:in.native.y,p.amd?float(p.width)/flow.flow_width:in.native.scale_x,
            p.amd?float(p.height)/flow.flow_height:in.native.scale_y,unsigned(bool(p.amd)),flow.frames_since_reset,in.candidate_x,in.candidate_y,
            unsigned(hdr),(in.candidate_format==DXGI_FORMAT_R11G11B10_FLOAT?2u:1u)|(copy_pixels?256u:0u),frame.source_to_scene,p.previous.source_to_scene,unsigned(in.settings.validation),unsigned(in.settings.mode),unsigned(p.backend==Backend::NativeMotion&&in.native.depth),1u};
        ID3D12DescriptorHeap* heaps[]={slot.heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(p.root.Get());
        l->SetComputeRootDescriptorTable(0,slot.heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRoot32BitConstants(1,24,&c,0);
        auto stats_gpu=slot.heap->GetGPUDescriptorHandleForHeapStart();stats_gpu.ptr+=16ull*p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const unsigned zero[4]{};l->ClearUnorderedAccessViewUint(stats_gpu,slot.stats_clear->GetCPUDescriptorHandleForHeapStart(),slot.gpu_stats.Get(),zero,0,nullptr);
        temporal_uav_barrier(l,slot.gpu_stats.Get());
        if(inplace) {
            state(l,in.candidate,in.candidate_state,ua);
            // A no-op UAV->UAV transition does not publish the preceding compositor.
            if(in.candidate_state==ua)temporal_uav_barrier(l,in.candidate);
        } else {
            state(l,in.candidate,in.candidate_state,sr);state(l,p.snapshot.Get(),cs,ua);
            l->SetPipelineState(p.copy_pso.Get());l->Dispatch((p.width+7)/8,(p.height+7)/8,1);
            state(l,p.snapshot.Get(),ua,sr);state(l,in.candidate,sr,ua);
        }
        state(l,in.original,in.original_state,sr);
        state(l,p.originals[p.index].Get(),cs,sr);state(l,p.residuals[p.index].Get(),cs,sr);state(l,p.residuals[next].Get(),cs,ua);
        if(later)state(l,p.first.Get(),ua,sr);if(!p.amd){state(l,in.native.resource,in.native.state,sr);
            if(in.native.depth)state(l,in.native.depth,in.native.depth_state,sr);state(l,p.depths[p.index].Get(),cs,sr);state(l,p.depths[next].Get(),cs,ua);}
        if(copy_pixels)state(l,p.originals[next].Get(),cs,ua);
        l->SetPipelineState(inplace?p.pipelines->inplace.Get():p.accumulate_pso.Get());l->Dispatch((p.width+7)/8,(p.height+7)/8,1);
        if(!inplace)state(l,p.snapshot.Get(),sr,cs);
        state(l,in.candidate,ua,in.candidate_state);
        if(in.candidate_state==ua){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=in.candidate;l->ResourceBarrier(1,&b);}
        state(l,p.originals[p.index].Get(),sr,cs);state(l,p.residuals[p.index].Get(),sr,cs);state(l,p.residuals[next].Get(),ua,cs);
        if(later)state(l,p.first.Get(),sr,ua);if(!p.amd){state(l,in.native.resource,sr,in.native.state);
            if(in.native.depth)state(l,in.native.depth,sr,in.native.depth_state);state(l,p.depths[p.index].Get(),sr,cs);state(l,p.depths[next].Get(),ua,cs);}
        temporal_uav_barrier(l,slot.gpu_stats.Get());state(l,slot.gpu_stats.Get(),ua,cs);
        l->CopyBufferRegion(slot.stats_readback.Get(),0,slot.gpu_stats.Get(),0,32);state(l,slot.gpu_stats.Get(),cs,ua);slot.stats_pending=true;
        if(copy_pixels){state(l,p.originals[next].Get(),ua,cs);state(l,in.original,sr,in.original_state);}
        else{
            state(l,in.original,sr,cs);state(l,p.originals[next].Get(),cs,D3D12_RESOURCE_STATE_COPY_DEST);
            l->CopyResource(p.originals[next].Get(),in.original);
            state(l,p.originals[next].Get(),D3D12_RESOURCE_STATE_COPY_DEST,cs);state(l,in.original,cs,in.original_state);
        }
        p.pending_frame=next_frame;p.index=next;p.recording_unsafe=false;
        if(inplace)++p.counters.inplace_recorded;else ++p.counters.snapshot_recorded;
        ++p.counters.recorded;if(!c.accumulate)++p.counters.seeded;p.counters.weight=weight;p.counters.pending_token=p.plan.token;
        p.message=inplace?"temporal work recorded; fp16-inplace experiment; awaiting actual owner submission":
            "temporal work recorded; snapshot path; awaiting actual owner submission";return RecordResult::Recorded;
    }catch(const std::exception& e){p.fail(e.what());return RecordResult::Faulted;}
}
bool RecordedTemporal::submitted(ID3D12Fence* fence,Id value,Id recording)noexcept {
    auto& p=*p_;if(p.fault||p.pending_slot<0||!value||value==UINT64_MAX||!device_is(fence,p.device.Get())||fence->GetCompletedValue()==UINT64_MAX)return false;
    if(p.amd&&!p.amd->commit_recording(p.plan.token,fence,value)){p.recording_unsafe=true;p.fail("AMD actual submission receipt rejected");return false;}
    if(p.history.finish(p.plan.token,p.plan.history_generation,ResidualStatus::SubmittedAdopted)!=Completion::Adopted){
        p.recording_unsafe=true;p.fail("temporal actual adoption rejected");return false;
    }
    auto& slot=p.slots[unsigned(p.pending_slot)];slot.fence=fence;slot.value=value;slot.recording=recording;slot.stats_submitted=true;
    p.previous=p.pending_frame;p.previous_depth=p.pending_depth;p.pending_slot=-1;p.next_slot=(p.next_slot+1)%p.slots.size();p.plan={};
    p.counters.pending_token=0;++p.counters.submitted;p.message="temporal result submitted and adopted; completion tracked separately";return true;
}
void RecordedTemporal::end_recording(Id recording)noexcept {
    if(!recording)return;for(auto& slot:p_->slots)if(slot.recording==recording)slot.recording=0;
}
bool RecordedTemporal::resubmitted(Id recording,ID3D12Fence* fence,Id value)noexcept {
    auto& p=*p_;bool found=false;
    for(const auto& slot:p.slots)found|=recording&&slot.recording==recording;
    if(!found)return true; // This recording has no temporal commands.
    if(!value||value==UINT64_MAX||!device_is(fence,p.device.Get())||fence->GetCompletedValue()==UINT64_MAX){
        p.recording_unsafe=true;p.fail("replayed recording missing its last completion receipt");return false;
    }
    // Same proven queue: this later signal also covers all earlier slots.
    for(auto& slot:p.slots){slot.fence=fence;slot.value=value;}
    if(p.amd&&!p.amd->retire_replayed_recording(fence,value)){
        p.recording_unsafe=true;p.fail("replayed AMD retirement receipt rejected");return false;
    }
    p.retiring=true;p.ready=false;
    p.message="replayed commands: retain latest fence and recreate before new source";return true;
}
bool RecordedTemporal::cancelled_unsubmitted()noexcept {
    auto& p=*p_;if(p.fault||p.pending_slot<0)return false;
    if(p.amd&&!p.amd->cancel_recording(p.plan.token))return false;
    if(p.history.finish(p.plan.token,p.plan.history_generation,ResidualStatus::NotSubmitted)!=Completion::ResetRequired)return false;
    p.slots[unsigned(p.pending_slot)].inputs={};p.slots[unsigned(p.pending_slot)].stats_pending=false;p.pending_slot=-1;p.retiring=true;p.ready=false;
    p.counters.pending_token=0;++p.counters.cancelled;p.message="unsubmitted recording cancelled; retire and recreate temporal context";return true;
}
}

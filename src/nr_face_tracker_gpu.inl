// C-interface D3D12 backend. Caller serializes access and supplies exact native
// command-list tokens and submission fences, as for the existing NR/F8 path.
#include "nr_face_tracker_shader.h"
#include "nr_region_inputs.h"
namespace nr_face_gpu {
constexpr unsigned kCapacity=24, kDescriptors=15;
constexpr std::uint64_t kMemoryLimit=64ull*1024*1024;
inline std::atomic<std::uint64_t> allocated_bytes{0};
inline bool Reserve(std::uint64_t bytes) {
    auto current=allocated_bytes.load();
    do{if(bytes>kMemoryLimit||current>kMemoryLimit-bytes)return false;}
    while(!allocated_bytes.compare_exchange_weak(current,current+bytes));return true;
}
struct Texture {
    ID3D12Resource* resource=nullptr;
    std::uint64_t bytes=0;
    nr_region::Frame identity{};
    bool valid=false;
    bool jitter_known=false;
    float jitter_x=0,jitter_y=0;
    ~Texture(){ReleaseCom(resource);allocated_bytes.fetch_sub(bytes);}
};
struct Constants {
    unsigned width=0,height=0,proxy_width=0,proxy_height=0;
    unsigned motion_width=0,motion_height=0,depth_width=0,depth_height=0;
    float motion_scale_x=0,motion_scale_y=0;
    unsigned face_count=0,face_age=0,has_previous=0,reset=0,depth_valid=0,motion_valid=0;
    float jitter_delta_x=0,jitter_delta_y=0,depth_tolerance=.0001f;
    unsigned reserved=0,motion_x=0,motion_y=0,depth_x=0,depth_y=0;
};
static_assert(sizeof(Constants)==24*4);
struct Face {float values[16]{};};
struct Inputs {
    ID3D12Resource* proxy=nullptr,*motion=nullptr,*depth=nullptr;
    DXGI_FORMAT proxy_format=DXGI_FORMAT_R16G16B16A16_FLOAT,motion_format=DXGI_FORMAT_R16G16_FLOAT,depth_format=DXGI_FORMAT_R32_FLOAT;
    std::shared_ptr<Texture> history[8]{};
};
struct Slot {
    std::shared_ptr<Texture> guide;
    ID3D12Resource* mask=nullptr,*payload=nullptr,*readback=nullptr,*detections=nullptr;
    ID3D12DescriptorHeap* heap=nullptr;
    ID3D12Resource* inputs[3]{};
    std::shared_ptr<Texture> history[8]{};
    nr_region::Frame identity{};
    std::uint64_t other_bytes=0;
    unsigned width=0,height=0,ledger=nr_f8_gpu::kCapacity;
    bool allocated=false,recorded=false,committed=false;
};
class Tracker {
public:
    static constexpr unsigned Invalid=kCapacity;
    bool Ensure(ID3D12Device* device) {
        if(device_==device&&pipelines_[0])return true;
        if(!device||device_)return false;
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,12,0,0,0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,12}};
        D3D12_ROOT_PARAMETER parameters[3]{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={2,ranges};
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,24};
        parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[2].Descriptor={1,0};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC description{};description.NumParameters=3;description.pParameters=parameters;
        description.NumStaticSamplers=1;description.pStaticSamplers=&sampler;
        ID3DBlob* blob=nullptr,*errors=nullptr;
        HRESULT hr=D3D12SerializeRootSignature(&description,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors);ReleaseCom(errors);
        if(SUCCEEDED(hr))hr=ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),IID_ID3D12RootSignature,reinterpret_cast<void**>(&root_));
        ReleaseCom(blob);
        const char* names[]={"FaceClearCS","FaceGuideCS","FaceMaskCS"};
        for(unsigned i=0;i<3&&SUCCEEDED(hr);++i) {
            hr=D3DCompile(kNrFaceTrackerShader,sizeof(kNrFaceTrackerShader)-1,"nr-face-tracker",nullptr,nullptr,names[i],"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
            ReleaseCom(errors);
            if(SUCCEEDED(hr)) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_;
                pso.CS={ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob)};
                hr=ID3D12Device_CreateComputePipelineState(device,&pso,IID_ID3D12PipelineState,reinterpret_cast<void**>(&pipelines_[i]));
            }
            ReleaseCom(blob);
        }
        if(FAILED(hr)){ReleaseCom(root_);for(auto& p:pipelines_)ReleaseCom(p);return false;}
        device_=device;ID3D12Device_AddRef(device_);
        stride_=ID3D12Device_GetDescriptorHandleIncrementSize(device_,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);return true;
    }
    // Width/height may change only through a fully retired resource slot. Live
    // history owns separate texture references and prevents its reuse.
    unsigned Arm(const nr_region::Frame& identity,unsigned width,unsigned height) {
        if(!device_||!identity.Valid()||width<32||height<32||width>384||height>224||width%32||height%32)return Invalid;
        for(unsigned i=0;i<kCapacity;++i) {
            auto& slot=slots_[i];
            if(slot.recorded||slot.ledger!=nr_f8_gpu::kCapacity|| (slot.guide&&slot.guide.use_count()!=1))continue;
            if(slot.allocated&&(slot.width!=width||slot.height!=height))Destroy(slot);
            if(!slot.allocated) {
                try{if(!Allocate(slot,width,height))return Invalid;}
                catch(...){Destroy(slot);return Invalid;}
            }
            nr_f8_gpu::TokenIdentity key{};key.request=identity.epoch;key.frame_index=static_cast<unsigned>(identity.frame);
            const int ledger=ledger_.Arm(key);if(ledger<0)return Invalid;
            slot.ledger=static_cast<unsigned>(ledger);slot.identity=identity;slot.committed=false;
            slot.guide->identity=identity;slot.guide->valid=false;++live_;return i;
        }
        return Invalid;
    }
    bool Record(unsigned index,ID3D12GraphicsCommandList* list,std::uint64_t token,
                const Inputs& inputs,Constants constants,const Face* faces,
                bool (*same_device)(ID3D12Device*,ID3D12Device*)=nullptr) {
        if(index>=kCapacity||!list||!token)return false;
        auto& slot=slots_[index];
        if(!slot.allocated||slot.recorded||slot.ledger==nr_f8_gpu::kCapacity||constants.face_count>16||constants.face_age>8||
            (constants.face_count&&!faces)||constants.width!=slot.width||constants.height!=slot.height||
            !std::isfinite(constants.motion_scale_x)||!std::isfinite(constants.motion_scale_y)||
            !std::isfinite(constants.jitter_delta_x)||!std::isfinite(constants.jitter_delta_y)||
            !std::isfinite(constants.depth_tolerance)||constants.depth_tolerance<0||constants.depth_tolerance>.01f)return false;
        ID3D12Resource* resources[]={inputs.proxy,inputs.motion,inputs.depth};
        const DXGI_FORMAT formats[]={inputs.proxy_format,inputs.motion_format,inputs.depth_format};
        const unsigned widths[]={constants.proxy_width,constants.motion_width,constants.depth_width};
        const unsigned heights[]={constants.proxy_height,constants.motion_height,constants.depth_height};
        const unsigned xs[]={0,constants.motion_x,constants.depth_x},ys[]={0,constants.motion_y,constants.depth_y};
        for(unsigned i=0;i<3;++i)if(!CheckInput(resources[i],formats[i],widths[i],heights[i],xs[i],ys[i],same_device))return false;
        const unsigned history_count=std::max(constants.face_age,constants.has_previous?1u:0u);
        for(unsigned f=0;f<constants.face_count;++f)for(float value:faces[f].values)
            if(!std::isfinite(value)||std::abs(value)>3)return false;
        for(unsigned i=0;i<history_count;++i) {
            const auto& history=inputs.history[i];
            if(!history||!history->valid||!nr_region::SameContract(history->identity,slot.identity)||
                slot.identity.frame<=i+1||history->identity.frame!=slot.identity.frame-i-1)return false;
            if(!CheckInput(history->resource,DXGI_FORMAT_R32G32B32A32_FLOAT,slot.width,slot.height,0,0,same_device))return false;
            const auto history_shape=ID3D12Resource_GetDesc(history->resource);
            if(history_shape.Width!=slot.width||history_shape.Height!=slot.height)return false;
        }
        void* mapped=nullptr;const D3D12_RANGE empty{0,0};
        if(FAILED(ID3D12Resource_Map(slot.detections,0,&empty,&mapped)))return false;
        std::memset(mapped,0,16*sizeof(Face));
        if(constants.face_count)std::memcpy(mapped,faces,constants.face_count*sizeof(Face));
        ID3D12Resource_Unmap(slot.detections,0,nullptr);
        auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
        auto srv=[&](unsigned i,ID3D12Resource* resource,DXGI_FORMAT format) {
            auto at=cpu;at.ptr+=static_cast<SIZE_T>(i)*stride_;
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
            ID3D12Device_CreateShaderResourceView(device_,resource,&d,at);
        };
        for(unsigned i=0;i<3;++i){slot.inputs[i]=resources[i];ID3D12Resource_AddRef(resources[i]);srv(i,resources[i],formats[i]);}
        srv(3,slot.guide->resource,DXGI_FORMAT_R32G32B32A32_FLOAT);
        for(unsigned i=0;i<8;++i) {
            if(i<history_count)slot.history[i]=inputs.history[i];
            srv(i+4,slot.history[i]?slot.history[i]->resource:nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT);
        }
        if(!ledger_.Record(slot.ledger,token)){ClearInputs(slot);return false;}
        // On reuse, all three outputs were left in these exact end states.
        Barrier(list,slot.guide->resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(list,slot.mask,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(list,slot.payload,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&slot.heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list,root_);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.heap));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,24,&constants,0);
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list,2,ID3D12Resource_GetGPUVirtualAddress(slot.detections));
        ID3D12GraphicsCommandList_SetPipelineState(list,pipelines_[0]);ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
        Uav(list,slot.payload);
        ID3D12GraphicsCommandList_SetPipelineState(list,pipelines_[1]);ID3D12GraphicsCommandList_Dispatch(list,(slot.width+7)/8,(slot.height+7)/8,1);
        Uav(list,slot.payload);
        Barrier(list,slot.guide->resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ID3D12GraphicsCommandList_SetPipelineState(list,pipelines_[2]);ID3D12GraphicsCommandList_Dispatch(list,(slot.width+7)/8,(slot.height+7)/8,1);
        Barrier(list,slot.mask,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list,slot.payload,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list,slot.readback,0,slot.payload,0,slot.width*slot.height*4ull+16);
        slot.recorded=true;return true;
    }
    std::shared_ptr<Texture> Commit(unsigned index) {
        if(index>=kCapacity||!slots_[index].recorded)return {};
        auto& slot=slots_[index];slot.committed=true;slot.guide->valid=true;return slot.guide;
    }
    ID3D12Resource* Mask(unsigned index)const{return index<kCapacity&&slots_[index].recorded?slots_[index].mask:nullptr;}
    void Discard(unsigned index) {
        if(index<kCapacity&&ledger_.DiscardArmed(slots_[index].ledger)) {
            slots_[index].ledger=nr_f8_gpu::kCapacity;ClearInputs(slots_[index]);--live_;
        }
    }
    void Submit(std::uint64_t token,void* queue,std::uint64_t fence){if(live_)ledger_.Submit(token,queue,fence);}
    void Invalidate(std::uint64_t token,bool unsubmitted){if(live_)ledger_.Invalidate(token,unsubmitted);}
    template<class Ready,class Consume> void Collect(Ready ready,Consume consume) {
        if(!live_)return;
        for(auto& slot:slots_) {
            if(slot.ledger==nr_f8_gpu::kCapacity)continue;
            const auto& entry=ledger_.Get(slot.ledger);
            if(entry.replayed||entry.quarantined||entry.unknown_submission)slot.guide->valid=false;
            bool completed=false;
            if(entry.state==nr_f8_gpu::TokenState::Submitted&&!entry.recording_alive&&!entry.unknown_submission&&ready(entry.queue_key,entry.fence_value)) {
                completed=ledger_.Ready(slot.ledger,entry.queue_key,entry.fence_value,true);
                if(completed&&slot.committed&&slot.guide->valid)consume(slot.identity,slot.readback,slot.width,slot.height);
            }else if(entry.state==nr_f8_gpu::TokenState::Completed){slot.guide->valid=false;completed=true;}
            if(completed) {
                ledger_.Reclaim(slot.ledger);slot.ledger=nr_f8_gpu::kCapacity;slot.recorded=false;
                ClearInputs(slot);--live_;
            }
        }
    }
    bool Empty()const{return live_==0;}
    bool ResetDevice() {
        if(live_)return false;
        for(auto& slot:slots_)Destroy(slot);
        for(auto& pipeline:pipelines_)ReleaseCom(pipeline);
        ReleaseCom(root_);ReleaseCom(device_);return true;
    }
private:
    bool CheckInput(ID3D12Resource* resource,DXGI_FORMAT format,unsigned width,unsigned height,unsigned x,unsigned y,
                    bool (*same_device)(ID3D12Device*,ID3D12Device*)) {
        if(!resource||!width||!height)return false;
        const auto d=ID3D12Resource_GetDesc(resource);
        if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1||
            (d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||x>d.Width||width>d.Width-x||y>d.Height||height>d.Height-y)return false;
        ID3D12Device* owner=nullptr;const auto hr=ID3D12Resource_GetDevice(resource,IID_ID3D12Device,reinterpret_cast<void**>(&owner));
        const bool match=SUCCEEDED(hr)&&owner&&(same_device?same_device(device_,owner):device_==owner);ReleaseCom(owner);if(!match)return false;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};support.Format=format;
        return SUCCEEDED(ID3D12Device_CheckFeatureSupport(device_,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))&&
            (support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
    }
    bool Resource(ID3D12Resource*& resource,D3D12_RESOURCE_DESC description,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,std::uint64_t& bytes) {
        const auto allocation=ID3D12Device_GetResourceAllocationInfo(device_,0,1,&description);
        if(!allocation.SizeInBytes||allocation.SizeInBytes==~UINT64(0)||!Reserve(allocation.SizeInBytes))return false;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
        if(FAILED(ID3D12Device_CreateCommittedResource(device_,&heap,D3D12_HEAP_FLAG_NONE,&description,state,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&resource)))) {
            allocated_bytes.fetch_sub(allocation.SizeInBytes);return false;
        }
        bytes+=allocation.SizeInBytes;return true;
    }
    bool Allocate(Slot& slot,unsigned width,unsigned height) {
        slot.guide=std::make_shared<Texture>();slot.width=width;slot.height=height;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
        d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        bool ok=Resource(slot.guide->resource,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,slot.guide->bytes);
        d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        if(ok)ok=Resource(slot.mask,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,slot.other_bytes);
        d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=width*height*4ull+16;d.Height=1;d.Format=DXGI_FORMAT_UNKNOWN;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(ok)ok=Resource(slot.payload,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_SOURCE,slot.other_bytes);
        d.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(ok)ok=Resource(slot.readback,d,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,slot.other_bytes);
        d.Width=16*sizeof(Face);
        if(ok)ok=Resource(slot.detections,d,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,slot.other_bytes);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=kDescriptors;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(ok)ok=SUCCEEDED(ID3D12Device_CreateDescriptorHeap(device_,&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&slot.heap)));
        if(!ok){Destroy(slot);return false;}
        auto at=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);at.ptr+=12*stride_;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;uav.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        ID3D12Device_CreateUnorderedAccessView(device_,slot.guide->resource,nullptr,&uav,at);at.ptr+=stride_;
        uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;ID3D12Device_CreateUnorderedAccessView(device_,slot.mask,nullptr,&uav,at);at.ptr+=stride_;
        uav={};uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;uav.Buffer.NumElements=width*height+4;
        ID3D12Device_CreateUnorderedAccessView(device_,slot.payload,nullptr,&uav,at);slot.allocated=true;return true;
    }
    static void ClearInputs(Slot& slot){for(auto& input:slot.inputs)ReleaseCom(input);for(auto& history:slot.history)history.reset();}
    static void Destroy(Slot& slot) {
        ClearInputs(slot);slot.guide.reset();ReleaseCom(slot.mask);ReleaseCom(slot.payload);ReleaseCom(slot.readback);ReleaseCom(slot.detections);ReleaseCom(slot.heap);
        allocated_bytes.fetch_sub(slot.other_bytes);slot=Slot{};
    }
    static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};ID3D12GraphicsCommandList_ResourceBarrier(list,1,&b);
    }
    static void Uav(ID3D12GraphicsCommandList* list,ID3D12Resource* resource) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;ID3D12GraphicsCommandList_ResourceBarrier(list,1,&b);
    }
    ID3D12Device* device_=nullptr;ID3D12RootSignature* root_=nullptr;ID3D12PipelineState* pipelines_[3]{};
    unsigned stride_=0,live_=0;Slot slots_[kCapacity]{};nr_f8_gpu::TokenLedger ledger_{};
};
}

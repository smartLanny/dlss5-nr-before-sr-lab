// Included after the existing F8 exact-submission ledger and D3D12 helpers.
// No global settings, game-specific heuristics, file I/O or CPU waits here.
#include "nr_replay_format.h"
#include "nr_replay_shader.h"
namespace nr_replay {
inline std::atomic<std::uint64_t> resident_bytes{0};
inline bool ReserveMemory(std::uint64_t bytes) {
    auto old=resident_bytes.load();
    do { if(bytes>kResidentLimit || old>kResidentLimit-bytes)return false; }
    while(!resident_bytes.compare_exchange_weak(old,old+bytes));
    return true;
}
inline DXGI_FORMAT ReadFormat(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS:return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:return format;
    }
}
struct Input {
    ID3D12Resource *resource=nullptr;
    DXGI_FORMAT view=DXGI_FORMAT_UNKNOWN;
    unsigned channels=4;
    Encoding encoding=Encoding::Float32;
    unsigned width=0,height=0,x=0,y=0;
};
struct GpuFrame {
    Header header{};
    ID3D12Resource *buffer=nullptr,*readback=nullptr;
    ID3D12Resource *inputs[kPlanes]{};
    ID3D12DescriptorHeap *heap=nullptr;
    bool finished=false;
    unsigned ledger_slot=nr_f8_gpu::kCapacity;
};
struct ExportJob {
    Header header{};
    ID3D12Resource *readback=nullptr;
    std::uint64_t bytes=0;
};
class Recorder {
public:
    static constexpr unsigned Invalid=kFrames;
    bool Ensure(ID3D12Device *device) {
        if(device_==device && pipeline_)return true;
        if(device_ || !device)return false;
        ID3DBlob *shader=nullptr,*error=nullptr,*serialized=nullptr;
        HRESULT hr=D3DCompile(kNrReplayShader,sizeof(kNrReplayShader)-1,"nr-replay",nullptr,nullptr,
            "ReplayCopyCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&error);
        ReleaseCom(error);
        if(FAILED(hr))return false;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
        ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable={2,ranges};
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants={0,0,8};
        D3D12_ROOT_SIGNATURE_DESC description{};description.NumParameters=2;description.pParameters=parameters;
        hr=D3D12SerializeRootSignature(&description,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error);
        ReleaseCom(error);
        if(SUCCEEDED(hr))hr=ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),
            ID3D10Blob_GetBufferSize(serialized),IID_ID3D12RootSignature,reinterpret_cast<void **>(&root_));
        ReleaseCom(serialized);
        if(SUCCEEDED(hr)) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_;
            pso.CS={ID3D10Blob_GetBufferPointer(shader),ID3D10Blob_GetBufferSize(shader)};
            hr=ID3D12Device_CreateComputePipelineState(device,&pso,IID_ID3D12PipelineState,
                reinterpret_cast<void **>(&pipeline_));
        }
        ReleaseCom(shader);
        if(FAILED(hr)){ReleaseCom(root_);ReleaseCom(pipeline_);return false;}
        device_=device;ID3D12Device_AddRef(device_);
        stride_=ID3D12Device_GetDescriptorHandleIncrementSize(device_,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }
    unsigned Arm(Header header,const Input (&inputs)[kPlanes],
                 bool (*matches_device)(ID3D12Device*,ID3D12Device*)=nullptr) {
        if(!device_ || header.index>=kFrames || frames_[header.index].buffer)return Invalid;
        for(unsigned i=0;i<kPlanes;++i) {
            if(!inputs[i].resource) {
                if(i!=static_cast<unsigned>(Role::Exposure))return Invalid;
                continue;
            }
            const auto desc=ID3D12Resource_GetDesc(inputs[i].resource);
            ID3D12Device *owner=nullptr;
            const auto queried=ID3D12Resource_GetDevice(inputs[i].resource,IID_ID3D12Device,reinterpret_cast<void **>(&owner));
            const bool same=SUCCEEDED(queried) && owner && (matches_device?matches_device(device_,owner):device_==owner);
            ReleaseCom(owner);if(!same)return Invalid;
            if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize!=1 ||
                desc.SampleDesc.Count!=1 || desc.Width>16384 || desc.Height>16384 ||
                (desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
                (inputs[i].channels==1 && inputs[i].encoding==Encoding::Float16))return Invalid;
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};support.Format=inputs[i].view;
            if(FAILED(ID3D12Device_CheckFeatureSupport(device_,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) ||
                !(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD))return Invalid;
            const auto width=inputs[i].width?inputs[i].width:static_cast<unsigned>(desc.Width);
            const auto height=inputs[i].height?inputs[i].height:desc.Height;
            if(inputs[i].x>desc.Width || width>desc.Width-inputs[i].x ||
               inputs[i].y>desc.Height || height>desc.Height-inputs[i].y)return Invalid;
            if(!AppendPlane(header,static_cast<Role>(i),width,height,
                inputs[i].channels,static_cast<unsigned>(desc.Format),static_cast<unsigned>(inputs[i].view),inputs[i].encoding))return Invalid;
            auto &plane=header.planes[i];plane.x=inputs[i].x;plane.y=inputs[i].y;
            plane.resource_width=static_cast<unsigned>(desc.Width);plane.resource_height=desc.Height;
        }
        if(!ValidHeader(header) || !ReserveMemory(header.payload_bytes*2))return Invalid;
        auto &frame=frames_[header.index];frame.header=header;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=header.payload_bytes;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr=ID3D12Device_CreateCommittedResource(device_,&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&frame.buffer));
        heap.Type=D3D12_HEAP_TYPE_READBACK;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(SUCCEEDED(hr))hr=ID3D12Device_CreateCommittedResource(device_,&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&frame.readback));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors=kPlanes*2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(SUCCEEDED(hr))hr=ID3D12Device_CreateDescriptorHeap(device_,&hd,IID_ID3D12DescriptorHeap,
            reinterpret_cast<void **>(&frame.heap));
        if(FAILED(hr)){DestroyFrame(frame,false);return Invalid;}
        const auto start=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(frame.heap);
        for(unsigned i=0;i<kPlanes;++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu=start;cpu.ptr+=std::size_t(i*2)*stride_;
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=inputs[i].resource?inputs[i].view:DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels=1;
            ID3D12Device_CreateShaderResourceView(device_,inputs[i].resource,&srv,cpu);
            cpu.ptr+=stride_;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_TYPELESS;
            uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
            uav.Buffer.NumElements=static_cast<UINT>(header.payload_bytes/4);
            ID3D12Device_CreateUnorderedAccessView(device_,frame.buffer,nullptr,&uav,cpu);
            frame.inputs[i]=inputs[i].resource;
            if(frame.inputs[i])ID3D12Resource_AddRef(frame.inputs[i]);
        }
        nr_f8_gpu::TokenIdentity key{};key.request=header.sequence;key.frame_index=header.index;
        const int ledger_slot=ledger_.Arm(key);
        if(ledger_slot<0){DestroyFrame(frame,false);return Invalid;}
        frame.ledger_slot=static_cast<unsigned>(ledger_slot);++live_;
        return header.index;
    }
    bool Begin(ID3D12GraphicsCommandList *list,unsigned index,std::uint64_t token) {
        if(index>=kFrames || !list || !token || !ledger_.Record(frames_[index].ledger_slot,token))return false;
        Copy(list,index,Role::Source);Copy(list,index,Role::Proxy);
        Copy(list,index,Role::Depth);Copy(list,index,Role::Motion);Copy(list,index,Role::Exposure);
        return true;
    }
    void Copy(ID3D12GraphicsCommandList *list,unsigned index,Role role) {
        auto &frame=frames_[index];const auto &plane=frame.header.planes[static_cast<unsigned>(role)];
        if(!plane.bytes)return;
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&frame.heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list,root_);
        ID3D12GraphicsCommandList_SetPipelineState(list,pipeline_);
        auto gpu=ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(frame.heap);
        gpu.ptr+=std::uint64_t(static_cast<unsigned>(role)*2)*stride_;
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,gpu);
        const std::uint32_t constants[]={plane.width,plane.height,plane.channels,
            plane.encoding==static_cast<unsigned>(Encoding::Float16)?1u:0u,
            static_cast<unsigned>(plane.offset),static_cast<unsigned>(frame.header.payload_bytes),plane.x,plane.y};
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,8,constants,0);
        ID3D12GraphicsCommandList_Dispatch(list,(plane.width+7)/8,(plane.height+7)/8,1);
    }
    bool Finish(ID3D12GraphicsCommandList *list,unsigned index,const Header &metadata) {
        if(index>=kFrames || !frames_[index].buffer ||
            ledger_.Get(frames_[index].ledger_slot).state!=nr_f8_gpu::TokenState::Recorded)return false;
        auto &frame=frames_[index];
        frame.header.evaluation=metadata.evaluation;frame.header.since_reset=metadata.since_reset;
        frame.header.frame=metadata.frame;frame.header.control_mask_after=metadata.control_mask_after;
        frame.header.auto_mask_after=metadata.auto_mask_after;
        Copy(list,index,Role::Neural);Copy(list,index,Role::Composite);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={frame.buffer,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list,frame.readback,0,frame.buffer,0,frame.header.payload_bytes);
        frame.finished=true;return true;
    }
    Header &Metadata(unsigned index){return frames_[index].header;}
    void Composition(unsigned index,const void *constants,unsigned count,unsigned flags,unsigned pass) {
        if(index>=kFrames || !frames_[index].buffer || count>64 || !constants)return;
        auto &frame=frames_[index];
        if(frame.finished || ledger_.Get(frame.ledger_slot).state!=nr_f8_gpu::TokenState::Recorded)return;
        frame.header.codec_constant_count=count;frame.header.codec_pass=pass;frame.header.codec_flags=flags;
        std::memcpy(frame.header.codec_constants,constants,count*4u);
    }
    void Discard(unsigned index) {
        if(index<kFrames && ledger_.DiscardArmed(frames_[index].ledger_slot)) {DestroyFrame(frames_[index],false);--live_;}
    }
    void Submit(std::uint64_t token,void *queue,std::uint64_t fence){if(live_)ledger_.Submit(token,queue,fence);}
    void Invalidate(std::uint64_t token,bool unsubmitted){if(live_)ledger_.Invalidate(token,unsubmitted);}
    template<class Ready,class Handoff> void Collect(Ready ready,Handoff handoff) {
        if(!live_)return;
        for(unsigned i=0;i<kFrames;++i) {
            if(!frames_[i].buffer)continue;
            const auto ledger_slot=frames_[i].ledger_slot;
            const auto &entry=ledger_.Get(ledger_slot);
            if(entry.state==nr_f8_gpu::TokenState::Submitted && ready(entry.queue_key,entry.fence_value) &&
                !entry.recording_alive && !entry.unknown_submission) {
                auto *job=new(std::nothrow)ExportJob{};
                if(!job)continue;
                if(!ledger_.Ready(ledger_slot,entry.queue_key,entry.fence_value,true)){delete job;continue;}
                auto &frame=frames_[i];job->header=frame.header;job->readback=frame.readback;
                job->bytes=frame.header.payload_bytes;
                job->header.completion=static_cast<unsigned>(frame.finished && !entry.replayed && !entry.quarantined
                    ?Completion::Complete:Completion::Invalidated);
                frame.readback=nullptr;DestroyFrame(frame,true);ledger_.Reclaim(ledger_slot);--live_;handoff(job);
            } else if(entry.state==nr_f8_gpu::TokenState::Completed && frames_[i].buffer) {
                // A list proved unsubmitted: no GPU data can be called a frame.
                auto *job=new(std::nothrow)ExportJob{};if(!job)continue;
                job->header=frames_[i].header;job->header.completion=static_cast<unsigned>(Completion::Invalidated);
                DestroyFrame(frames_[i],false);ledger_.Reclaim(ledger_slot);--live_;handoff(job);
            }
        }
    }
    bool Empty()const{return live_==0;}
    bool ResetDevice() {
        if(!Empty())return false;
        ReleaseCom(pipeline_);ReleaseCom(root_);ReleaseCom(device_);stride_=0;return true;
    }
private:
    static void DestroyFrame(GpuFrame &frame,bool readback_handed_off) {
        ReleaseCom(frame.buffer);ReleaseCom(frame.readback);ReleaseCom(frame.heap);
        for(auto &input:frame.inputs)ReleaseCom(input);
        resident_bytes.fetch_sub(frame.header.payload_bytes*(readback_handed_off?1:2));
        frame={};
    }
    ID3D12Device *device_=nullptr;
    ID3D12RootSignature *root_=nullptr;
    ID3D12PipelineState *pipeline_=nullptr;
    unsigned stride_=0;
    unsigned live_=0;
    GpuFrame frames_[kFrames]{};
    nr_f8_gpu::TokenLedger ledger_{};
};
} // namespace nr_replay

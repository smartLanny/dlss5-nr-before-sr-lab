// Separate opt-in MFG observation bridge. The existing 2x host remains frozen.
// Public SDK count/index fields request each interpolated frame individually.
#define HostCreateFg HostCreateFg2x
#define HostEvaluateFg HostEvaluateFg2x
#include "compat_native_fg_bootstrap.cpp"
#undef HostCreateFg
#undef HostEvaluateFg
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mfg_test {
unsigned requested=1,maximum=0;ID3D12Device* device=nullptr;ID3D12Resource* sentinel=nullptr;ID3D12Resource* pixel_sentinel=nullptr;
D3D12_PLACED_SUBRESOURCE_FOOTPRINT sentinel_footprint{};
ID3D12Resource* outputs[16]{};
struct Frame {ID3D12Resource *pixels=nullptr,*decision=nullptr;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes=0,source=0;unsigned index=0,count=0,width=0,height=0;};
std::vector<Frame> frames;
constexpr unsigned failure=0xBADF0022u;
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);}
bool Buffer(UINT64 size,D3D12_HEAP_TYPE type,ID3D12Resource** output){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(output)));
}
bool Observe(ID3D12GraphicsCommandList* list,ID3D12Resource* image,ID3D12Resource* decision,unsigned width,unsigned height,UINT64 source,unsigned index){
    Frame f{};f.source=source;f.index=index;f.count=requested;f.width=width;f.height=height;auto desc=image->GetDesc();
    device->GetCopyableFootprints(&desc,0,1,0,&f.footprint,nullptr,nullptr,&f.bytes);
    if(!Buffer(f.bytes,D3D12_HEAP_TYPE_READBACK,&f.pixels)||!Buffer(4,D3D12_HEAP_TYPE_READBACK,&f.decision))return false;
    Barrier(list,image,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=image;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=f.pixels;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=f.footprint;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(list,image,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barrier(list,decision,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(f.decision,0,decision,0,4);Barrier(list,decision,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    frames.push_back(f);return true;
}
}
extern "C" __declspec(dllexport) unsigned HostCreateFg(ID3D12GraphicsCommandList* list,
    unsigned width,unsigned height,unsigned format,void** handle,void** parameters){
    using namespace mfg_test;wchar_t count[32]{};
    if(GetEnvironmentVariableW(L"NR_TEST_MFG_COUNT",count,32))requested=std::wcstoul(count,nullptr,10);
    NVSDK_NGX_Parameter* caps=nullptr;const auto queried=NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    unsigned available=0;NVSDK_NGX_Result got=NVSDK_NGX_Result_Fail;
    if(caps){caps->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,&available);got=caps->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,&maximum);}
    std::printf("OFFICIAL_MFG_CAPS query=0x%08X available=%u maxResult=0x%08X maxGenerated=%u requested=%u multiplier=%u\n",unsigned(queried),available,unsigned(got),maximum,requested,requested+1);
    if(!requested||requested>16||(requested>1&&(got!=NVSDK_NGX_Result_Success||requested>maximum)))return failure;
    if(FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||!Buffer(4,D3D12_HEAP_TYPE_UPLOAD,&sentinel))return failure;
    void* mapped=nullptr;D3D12_RANGE none{};if(FAILED(sentinel->Map(0,&none,&mapped)))return failure;*static_cast<unsigned*>(mapped)=0xCCCCCCCC;sentinel->Unmap(0,nullptr);
    D3D12_RESOURCE_DESC sentinel_desc{};sentinel_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;sentinel_desc.Width=width;sentinel_desc.Height=height;
    sentinel_desc.DepthOrArraySize=sentinel_desc.MipLevels=sentinel_desc.SampleDesc.Count=1;sentinel_desc.Format=static_cast<DXGI_FORMAT>(format);UINT64 sentinel_bytes=0;
    device->GetCopyableFootprints(&sentinel_desc,0,1,0,&sentinel_footprint,nullptr,nullptr,&sentinel_bytes);
    if(format!=DXGI_FORMAT_R8G8B8A8_UNORM||!Buffer(sentinel_bytes,D3D12_HEAP_TYPE_UPLOAD,&pixel_sentinel)||FAILED(pixel_sentinel->Map(0,&none,&mapped)))return failure;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const unsigned pixel=0xD37E5AC1;std::memcpy(static_cast<char*>(mapped)+y*sentinel_footprint.Footprint.RowPitch+x*4,&pixel,4);}pixel_sentinel->Unmap(0,nullptr);
    for(unsigned i=0;i+1<requested;++i){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Format=static_cast<DXGI_FORMAT>(format);desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&outputs[i]))))return failure;
    }
    return HostCreateFg2x(list,width,height,format,handle,parameters);
}
extern "C" __declspec(dllexport) unsigned HostEvaluateFg(ID3D12GraphicsCommandList* list,
    void* feature,void* parameters,ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* motion,
    ID3D12Resource* real,ID3D12Resource* interpolated,ID3D12Resource* disable,ID3D12Resource* hudless,ID3D12Resource* alpha,
    unsigned width,unsigned height,unsigned long long frame){
    using namespace mfg_test;if(!disable||width!=640||height!=360)return failure;
    auto* params=static_cast<NVSDK_NGX_Parameter*>(parameters);NVSDK_NGX_DLSSG_Opt_Eval_Params options{};
    for(unsigned i=0;i<4;++i){options.cameraViewToClip[i][i]=options.clipToCameraView[i][i]=options.clipToLensClip[i][i]=options.clipToPrevClip[i][i]=options.prevClipToClip[i][i]=1;}
    options.mvecScale[0]=1.f/width;options.mvecScale[1]=1.f/height;options.cameraUp[1]=options.cameraRight[0]=options.cameraFwd[2]=1;
    options.cameraNear=.1f;options.cameraFar=1000;options.cameraFOV=1.04719755f;options.cameraAspectRatio=float(width)/height;
    options.cameraMotionIncluded=true;options.orthoProjection=true;options.reset=frame==0;options.motionVectorsInvalidValue=3.402823466e+38F;
    options.backbufferSubrectSize=options.outputInterpSubrectSize=options.outputRealSubrectSize=options.mvecsSubrectSize=options.depthSubrectSize={width,height};
    if(hudless)options.hudLessSubrectSize={width,height};if(alpha)options.uiAlphaSubrectSize={width,height};
    options.multiFrameCount=requested;params->Set(NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID,frame);
    for(unsigned index=1;index<=requested;++index){options.multiFrameIndex=index;
        NVSDK_NGX_D3D12_DLSSG_Eval_Params input{};input.pBackbuffer=color;input.pDepth=depth;input.pMVecs=motion;input.pHudless=hudless;input.pUIAlpha=alpha;
        input.pOutputRealFrame=real;input.pOutputInterpFrame=index==requested?interpolated:outputs[index-1];
        // The runtime writes a source-level eligibility decision at the first
        // inner index. Subsequent inner calls need not rewrite that optional
        // buffer; they consume the same source decision.
        input.pOutputDisableInterpolation=index==1?disable:nullptr;
        if(index==1){Barrier(list,disable,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(disable,0,sentinel,0,4);Barrier(list,disable,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
        Barrier(list,input.pOutputInterpFrame,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION initial{};initial.pResource=pixel_sentinel;initial.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;initial.PlacedFootprint=sentinel_footprint;
        D3D12_TEXTURE_COPY_LOCATION target{};target.pResource=input.pOutputInterpFrame;target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&target,0,0,0,&initial,nullptr);Barrier(list,input.pOutputInterpFrame,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto result=NGX_D3D12_EVALUATE_DLSSG(list,static_cast<NVSDK_NGX_Handle*>(feature),params,&input,&options);
        std::printf("OFFICIAL_MFG_EVAL source=%llu index=%u count=%u result=0x%08X\n",frame,index,requested,unsigned(result));
        if(result!=NVSDK_NGX_Result_Success)return unsigned(result);
        if(!Observe(list,input.pOutputInterpFrame,disable,width,height,frame,index))return failure;
    }return unsigned(NVSDK_NGX_Result_Success);
}
extern "C" __declspec(dllexport) unsigned HostExportMfg(const wchar_t* directory){
    using namespace mfg_test;if(frames.size()!=24u*requested)return failure;unsigned eligible[16]{};
    const auto folder=std::wstring(directory)+L"\\mfg";if(!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return failure;
    FILE* manifest=nullptr;_wfopen_s(&manifest,(folder+L"\\outputs.jsonl").c_str(),L"wb");if(!manifest)return failure;
    for(auto& f:frames){void* value=nullptr;D3D12_RANGE four{0,4};if(FAILED(f.decision->Map(0,&four,&value)))return failure;
        const unsigned disabled=*static_cast<unsigned char*>(value);D3D12_RANGE none{};f.decision->Unmap(0,&none);if(disabled>1)return failure;if(!disabled)++eligible[f.index-1];
        D3D12_RANGE range{0,static_cast<SIZE_T>(f.bytes)};if(FAILED(f.pixels->Map(0,&range,&value)))return failure;
        wchar_t name[96]{};swprintf_s(name,L"source-%03llu-index-%02u.rgba",f.source,f.index);FILE* pixels=nullptr;
        _wfopen_s(&pixels,(folder+L"\\"+name).c_str(),L"wb");if(!pixels)return failure;
        for(unsigned y=0;y<f.height;++y)if(std::fwrite(static_cast<char*>(value)+y*f.footprint.Footprint.RowPitch,4,f.width,pixels)!=f.width)return failure;
        std::fclose(pixels);f.pixels->Unmap(0,&none);
        std::fprintf(manifest,"{\"source\":%llu,\"index\":%u,\"count\":%u,\"disable_interpolation\":%u,\"decision_scope\":\"source shared across indexed outputs\",\"width\":%u,\"height\":%u}\n",f.source,f.index,f.count,disabled,f.width,f.height);
        f.pixels->Release();f.decision->Release();
    }std::fclose(manifest);
    for(unsigned i=0;i<requested;++i){std::printf("OFFICIAL_MFG_OUTPUT index=%u recorded=%u eligible=%u\n",i+1,24u,eligible[i]);if(eligible[i]<4)return failure;}
    std::printf("PASS OFFICIAL_MFG generatedPerSource=%u multiplier=%u outputs=%zu; exact GPU completion and all indexed readbacks, no physical Present cadence claim\n",requested,requested+1,frames.size());return 1;
}

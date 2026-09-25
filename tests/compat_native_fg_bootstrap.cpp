// Official NGX DLSSG helpers. Kept in the MSVC bridge so this test does not
// invent an NGX feature ABI or borrow the private NR bridge for frame generation.
#include "compat_present_ngx_bootstrap.cpp"
#include <nvsdk_ngx_helpers_dlssg.h>
#include <cstdio>
extern "C" __declspec(dllexport) unsigned HostCreateFg(ID3D12GraphicsCommandList* list,
    unsigned width,unsigned height,unsigned format,void** handle,void** parameters) {
    NVSDK_NGX_Parameter* caps=nullptr;
    const auto queried=NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    unsigned available=0,init=0;
    if(caps){caps->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,&available);
        caps->Get(NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult,&init);}
    std::printf("official-FG-capability query=0x%08X available=%u featureInit=0x%08X\n",unsigned(queried),available,init);
    NVSDK_NGX_Parameter* params=nullptr;
    auto result=NVSDK_NGX_D3D12_AllocateParameters(&params);if(result!=NVSDK_NGX_Result_Success)return unsigned(result);
    NVSDK_NGX_DLSSG_Create_Params create{};create.Width=create.RenderWidth=width;create.Height=create.RenderHeight=height;
    create.NativeBackbufferFormat=format;
    NVSDK_NGX_Handle* fg=nullptr;result=NGX_D3D12_CREATE_DLSSG(list,1,1,&fg,params,&create);
    *handle=fg;*parameters=params;return unsigned(result);
}
extern "C" __declspec(dllexport) unsigned HostEvaluateFg(ID3D12GraphicsCommandList* list,
    void* feature,void* parameters,ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* motion,
    ID3D12Resource* real,ID3D12Resource* interpolated,ID3D12Resource* disable,ID3D12Resource* hudless,ID3D12Resource* alpha,
    unsigned width,unsigned height,unsigned long long frame) {
    auto* params=static_cast<NVSDK_NGX_Parameter*>(parameters);
    NVSDK_NGX_D3D12_DLSSG_Eval_Params input{};input.pBackbuffer=color;input.pDepth=depth;input.pMVecs=motion;
    input.pOutputRealFrame=real;input.pOutputInterpFrame=interpolated;input.pOutputDisableInterpolation=disable;
    input.pHudless=hudless;input.pUIAlpha=alpha;
    NVSDK_NGX_DLSSG_Opt_Eval_Params options{};
    for(unsigned i=0;i<4;++i){options.cameraViewToClip[i][i]=1;options.clipToCameraView[i][i]=1;
        options.clipToLensClip[i][i]=1;options.clipToPrevClip[i][i]=1;options.prevClipToClip[i][i]=1;}
    options.mvecScale[0]=1.f/width;options.mvecScale[1]=1.f/height;
    options.cameraUp[1]=options.cameraRight[0]=options.cameraFwd[2]=1;
    options.cameraNear=.1f;options.cameraFar=1000;options.cameraFOV=1.04719755f;
    options.cameraAspectRatio=float(width)/height;options.cameraMotionIncluded=true;options.orthoProjection=true;
    options.reset=frame==0;options.motionVectorsInvalidValue=3.402823466e+38F;
    options.backbufferSubrectSize=options.outputInterpSubrectSize=options.outputRealSubrectSize=
        options.mvecsSubrectSize=options.depthSubrectSize={width,height};
    if(hudless)options.hudLessSubrectSize={width,height};if(alpha)options.uiAlphaSubrectSize={width,height};
    params->Set(NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID,frame);
    return unsigned(NGX_D3D12_EVALUATE_DLSSG(list,static_cast<NVSDK_NGX_Handle*>(feature),params,&input,&options));
}

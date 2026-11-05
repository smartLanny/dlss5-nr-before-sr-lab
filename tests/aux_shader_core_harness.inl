// Included after the actual generated Core; only pipeline creation is invoked.
// No NGX, borrowed command recording, queue submission, or game execution.
#include <cstdlib>
#include "ci_warp_runtime.h"
namespace aux_shader_test {
void Check(bool value,const char* name){if(!value){std::printf("FAIL %s\n",name);std::exit(41);}}
void Hr(HRESULT value,const char* name){Check(SUCCEEDED(value),name);}
template<class Blob> void Same(const char* body,SIZE_T size,const char* label,const char* entry,const char* target,UINT flags,Blob artifact){
    ID3DBlob *code=nullptr,*error=nullptr;
    Hr(D3DCompile(body,size,label,nullptr,nullptr,entry,target,flags,0,&code,&error),"independent original runtime compilation");
    Check(artifact.data&&artifact.size==ID3D10Blob_GetBufferSize(code)&&
        std::memcmp(artifact.data,ID3D10Blob_GetBufferPointer(code),artifact.size)==0,"AUX_BYTECODE_EXACT_ORACLE");
    std::printf("PASS byte-identical %s %s bytes=%zu\n",entry,target,artifact.size);
    ReleaseCom(code);ReleaseCom(error);
}
void Run(){
    Hr(nr_test_warp::Prepare(),"Prepare pinned CI WARP");
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"factory");
    Hr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void**>(&adapter)),"WARP");
    Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"device");
    Hr(nr_test_warp::Verify("aux-shader"),"Verify actual CI WARP");
    g_codec.device=device;ID3D12Device_AddRef(device);
    auto present=std::make_unique<nr_compat_present_runtime::Context>();present->device=device;
    present->format=DXGI_FORMAT_R8G8B8A8_UNORM;present->uses_native_guides=false;
    Check(nr_compat_present_runtime::MakePipelines(*present),"actual Present utility pipelines");
    std::string error;auto* dual=nr_dual::dx12::CreatePipelines(device,error);
    Check(dual!=nullptr,error.c_str());
    nr_source_runtime::NativeGuideSnapshotObjects guides{};
    Check(nr_source_runtime::BuildNativeGuideSnapshot(device,guides),"actual native snapshot pipeline");
    Check(EnsureRrDepthPipeline(device),"actual RR depth pipeline");
    const auto compile_calls=NrAuxObservedCompileCalls();
    Check(compile_calls==(NR_AUX_PRECOMPILED?0u:9u),"AUX_ACTUAL_D3DCOMPILE_CALLS_ORACLE");
    unsigned long long words[nr_aux_shader::kSnapshotWords]{};
    Check(nr_aux_shader::Snapshot(words,std::size(words))==std::size(words)&&nr_aux_shader::Frequency()>0,"bounded creation timing snapshot");
    constexpr unsigned expected_pipeline[]={1,1,1,0,1,1,1,1,1};
    for(unsigned shader=0;shader<9;++shader){
        const auto* compile=words+(shader*2)*nr_aux_shader::kMetrics;
        const auto* pipeline=compile+nr_aux_shader::kMetrics;
        Check(compile[0]==(NR_AUX_PRECOMPILED?0u:1u)&&compile[1]==0&&compile[2]==compile[0],"compile counter agrees with actual API interception");
        Check(pipeline[0]==expected_pipeline[shader]&&pipeline[1]==0&&pipeline[2]==pipeline[0],"PSO creation count and timing are independent");
        std::printf("TIMING shader=%u compile_calls=%llu compile_ticks=%llu pso_calls=%llu pso_ticks=%llu\n",shader,compile[0],compile[3],pipeline[0],pipeline[3]);
    }
    Check(nr_source_runtime::BuildNativeGuideSnapshot(device,guides)&&EnsureRrDepthPipeline(device),"existing pipeline fast return");
    unsigned long long warm[nr_aux_shader::kSnapshotWords]{};
    Check(nr_aux_shader::Snapshot(warm,std::size(warm))==std::size(warm)&&std::memcmp(words,warm,sizeof(words))==0,
        "warm lookup performs no shader compile PSO create or creation clock");
    std::printf("PASS actual Core auxiliary creation D3DCompile=%llu PSO=8 precompiled=%u frequency=%llu\n",compile_calls,unsigned(NR_AUX_PRECOMPILED),nr_aux_shader::Frequency());
    const UINT strict=D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3;
    for(const auto* entry:{"ConvertCS","InitColorCS","InitCS"})Same(kCompatPresentShader,sizeof(kCompatPresentShader)-1,"own-compat-present",entry,"cs_5_0",strict,nr_aux_present_precompiled::Find(entry,"cs_5_0"));
    Same(kCompatPresentShader,sizeof(kCompatPresentShader)-1,"own-compat-present","FullscreenVS","vs_5_0",strict,nr_aux_present_precompiled::Find("FullscreenVS","vs_5_0"));
    Same(kCompatPresentShader,sizeof(kCompatPresentShader)-1,"own-compat-present","BlitPS","ps_5_0",strict,nr_aux_present_precompiled::Find("BlitPS","ps_5_0"));
    Same(nr_dual::dx12::embedded::downsample,sizeof(nr_dual::dx12::embedded::downsample)-1,"nr_dual_downsample","DownsampleCS","cs_5_0",strict,nr_aux_dual_precompiled::Find("DownsampleCS","cs_5_0"));
    Same(nr_dual::dx12::embedded::residual,sizeof(nr_dual::dx12::embedded::residual)-1,"nr_dual_residual","ResidualCS","cs_5_0",strict,nr_aux_dual_precompiled::Find("ResidualCS","cs_5_0"));
    Same(nr_source_runtime::kNativeGuideSnapshotShader,sizeof(nr_source_runtime::kNativeGuideSnapshotShader)-1,"NR.NativeGuideSnapshot","SnapshotGuidesCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,nr_aux_guides_precompiled::Find("SnapshotGuidesCS","cs_5_0"));
    Same(kRrDepthShader,sizeof(kRrDepthShader)-1,"NR.RRDepth","ConvertRrDepthCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,nr_aux_rr_depth_precompiled::Find("ConvertRrDepthCS","cs_5_0"));
    ReleaseCom(present->blit);ReleaseCom(present->init_guides);ReleaseCom(present->init_color);ReleaseCom(present->convert);ReleaseCom(present->util_root);
    Check(nr_dual::dx12::DestroyPipelinesAfterRetirement(dual,[](void*){return true;},nullptr),"unrecorded spatial pipeline retirement");
    nr_source_runtime::ReleaseNativeGuideSnapshot(guides);ReleaseCom(g_codec.rr_depth_pipeline);ReleaseCom(g_codec.rr_depth_root);ReleaseCom(g_codec.device);
    ReleaseCom(device);ReleaseCom(adapter);ReleaseCom(factory);
    std::puts("PASS 9 auxiliary bytecode comparisons; actual Core creation only, WARP, no NGX/game/FPS claim");
}
}

// Full generated production codec; controlled P/N planes, NOT NGX inference.
#define main InheritedPostMain
#include "post_scaled_work_gpu.cpp"
#undef main

namespace reconstruction_test {
constexpr unsigned W=34,H=18,SX=3,SY=2,DX=2,DY=3;
constexpr unsigned Guard=1024u,Rgb=1073741824u;
static unsigned checks=0,cases=0;
void Test(bool ok,const char* why){++checks;Require(ok,why);}
float Diff(Float4 a,Float4 b){return std::max({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)});}
float Luma(Float4 p,unsigned domain){Float3 c{p.x,p.y,p.z};if(domain==32u)c=SrgbToLinear(c);return DotLuma(c);}
Float4 Store(Float3 c,unsigned domain,float alpha=1){if(domain==32u)c=LinearToSrgb(c);return {c.x,c.y,c.z,alpha};}
Float4 Model(Float3 c,unsigned domain){if(domain!=1u)c=LinearToSrgb(c);return {c.x,c.y,c.z,1};}
void Dispatch(Gpu& g,ID3D12PipelineState* p,const CodecConstants& c){
    ID3D12DescriptorHeap* hs[]={g.heap.p};g.list->SetDescriptorHeaps(1,hs);g.list->SetComputeRootSignature(g.root.p);
    g.list->SetComputeRootDescriptorTable(0,g.heap->GetGPUDescriptorHandleForHeapStart());
    g.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);g.list->SetPipelineState(p);
    g.list->Dispatch((c.width+7)/8,(c.height+7)/8,1);
}
struct Fixture {
    Gpu& gpu;unsigned nw,nh,domain;CodecConstants c{};
    std::vector<Float4> input,proxy,neural;
    Texture source,p,n,e;
    Float4 outside{-4,-3,-2,-.5f};
    Fixture(Gpu& g,unsigned mw,unsigned mh,unsigned d,bool edge,bool nearBlack=false,bool perturb=false):gpu(g),nw(mw),nh(mh),domain(d){
        std::printf("FIXTURE domain=%u size=%ux%u edge=%u near=%u perturb=%u\n",domain,nw,nh,edge,nearBlack,perturb);
        input.assign((W+SX+3)*(H+SY+2),outside);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            Float3 rgb=nearBlack?Float3{.4f,.3f,.2f}:edge?(x<16?Float3{.1f,.1f,.1f}:Float3{.8f,.8f,.8f}):Float3{.2f,.3f,.1f};
            if(d==0u){rgb.x*=4;rgb.y*=4;rgb.z*=4;}
            input[(y+SY)*(W+SX+3)+x+SX]=Store(rgb,d,float((x+y)%5+1)/8.f);
        }
        source=gpu.MakeTexture(W+SX+3,H+SY+2,kModelFormat,input);
        p=gpu.MakeTexture(nw,nh,kModelFormat,std::vector<Float4>(nw*nh));
        n=gpu.MakeTexture(nw,nh,kModelFormat,std::vector<Float4>(nw*nh));
        e=gpu.MakeTexture(W+DX+3,H+DY+2,kModelFormat,std::vector<Float4>((W+DX+3)*(H+DY+2),outside));
        gpu.TextureState(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.TextureState(p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        gpu.TextureState(n,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.TextureState(e,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        BindProductionDescriptors(gpu,source,p,n,e,nullptr);
        c.width=nw;c.height=nh;c.source_width=W;c.source_height=H;c.model_width=nw;c.model_height=nh;
        c.source_base_x=SX;c.source_base_y=SY;c.proxy_scale=1;c.source_to_canonical=1;c.pre_exposure=1;
        c.transfer_strength=1;c.color_strength=.5f;c.max_luma_ratio=2;c.frame_confidence=1;c.proxy_curve=1;c.passthrough=d;
        Dispatch(gpu,gpu.encode.p,c);gpu.ReadTexture(p);gpu.ReadTexture(source);gpu.Execute();
        proxy=gpu.TexturePixels(p);input=gpu.TexturePixels(source);neural=proxy;
        if(nearBlack){for(auto& v:neural)v=Model({.0001f,perturb?.00015f:.0001f,.0001f},d);}
        gpu.UploadTexture(n,neural);gpu.ReadTexture(n);gpu.Execute();neural=gpu.TexturePixels(n);
        c.width=W;c.height=H;c.destination_base_x=DX;c.destination_base_y=DY;
    }
    std::vector<Float4> Run(unsigned mode,bool guard,bool rgb=false,bool post=false,bool negative=false){
        std::printf("DISPATCH case=%u mode=%u guard=%u rgb=%u post=%u\n",cases,mode,guard,rgb,post);
        c.region_flags=(mode<<8)|(guard&&!negative?Guard:0u);c.passthrough=domain|(rgb?Rgb:0u);
        // Standalone CombineCS carries the real post-preparation receipt bit.
        if(post&&rgb)c.passthrough|=536870912u;
        gpu.TextureState(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.TextureState(p,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.TextureState(n,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.TextureState(e,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        BindProductionDescriptors(gpu,source,p,n,e,nullptr);
        Dispatch(gpu,post?gpu.combine.p:gpu.combine_v2.p,c);gpu.ReadTexture(e);gpu.Execute();
        auto output=gpu.TexturePixels(e);std::vector<Float4> active;
        for(unsigned y=0;y<H+DY+2;++y)for(unsigned x=0;x<W+DX+3;++x){
            const auto v=output[y*(W+DX+3)+x];
            if(x<DX||y<DY||x>=DX+W||y>=DY+H){Test(PixelEqual(v,outside,0,"outside"),"reconstruction wrote outside subrect");continue;}
            const auto old=input[(y-DY+SY)*(W+SX+3)+x-DX+SX];
            Test(v.w==old.w,"reconstruction changed original alpha");
            Test(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"nonfinite output escaped");active.push_back(v);
        }
        ++cases;return active;
    }
    void ChangeDarkSide(){
        for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){
            auto v=proxy[y*nw+x];Float3 linear{v.x,v.y,v.z};if(domain!=1u)linear=SrgbToLinear(linear);
            if((x+.5f)*W/nw<16){linear.x*=.2f;linear.y*=.2f;linear.z*=.2f;}
            neural[y*nw+x]=Model(linear,domain);
        }
        gpu.UploadTexture(n,neural);gpu.ReadTexture(n);gpu.Execute();neural=gpu.TexturePixels(n);
    }
};
void CapturePair(Fixture& f,unsigned mode) {
    auto& gpu=f.gpu;
    constexpr unsigned pixels=W*H,bytes=pixels*16*4+16;
    auto capture=gpu.Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto readback=gpu.Buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.TextureState(f.source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(f.p,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(f.n,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(f.e,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    NrSlot slot{};slot.source=f.source.resource.p;slot.proxy=f.p.resource.p;slot.neural=f.n.resource.p;slot.result=f.e.resource.p;
    slot.source_format=slot.source_srv_format=kModelFormat;
    slot.result_format=slot.result_srv_format=slot.result_uav_format=kModelFormat;
    slot.heap=gpu.heap.p;slot.stage_capture.armed=true;slot.stage_capture.buffer=capture.p;slot.stage_capture.bytes=bytes;
    WriteCodecDescriptorSet(gpu.device.p,slot,gpu.heap.p);
    // The legacy immediate descriptor writer deliberately leaves capture null.
    // Production's DescriptorSet plan programs an armed raw UAV separately;
    // reproduce that final binding here instead of changing production policy.
    D3D12_UNORDERED_ACCESS_VIEW_DESC raw{};
    raw.Format=DXGI_FORMAT_R32_TYPELESS;raw.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    raw.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;raw.Buffer.NumElements=bytes/4;
    gpu.device->CreateUnorderedAccessView(capture.p,nullptr,&raw,gpu.Cpu(kCodecCaptureUavIndex));
    f.c.region_flags=mode<<8;f.c.passthrough=f.domain;f.c.capture_stride=1;
    Dispatch(gpu,gpu.combine_v2.p,f.c);
    gpu.Barrier(capture.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyBufferRegion(readback.p,0,capture.p,0,bytes);
    gpu.ReadTexture(f.e);gpu.Execute();f.c.capture_stride=0;
    void* data=nullptr;D3D12_RANGE range{0,bytes};Check(readback->Map(0,&range,&data),"Map paired capture");
    std::vector<Float4> planes(pixels*4+1);std::memcpy(planes.data(),data,bytes);
    const D3D12_RANGE noWrite{0,0};readback->Unmap(0,&noWrite);
    auto output=gpu.TexturePixels(f.e);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        auto i=y*W+x;auto original=f.input[(y+SY)*(W+SX+3)+x+SX];
        Test(Diff(planes[i],original)==0&&planes[i].w==original.w,"capture is not same-source original");
        auto final=output[(y+DY)*(W+DX+3)+x+DX];
        Test(Diff(final,planes[pixels*3+i])<.002f,"capture final differs beyond storage rounding");
        if(x==16&&y==9){
            float p=Luma(planes[pixels+i],f.domain),n=Luma(planes[pixels*2+i],f.domain);
            Test(n/p>.99f,"capture exported old bilinear pair instead of effective guided pair");
        }
    }
    ++cases;
}
void RunAll(Gpu& gpu,bool negative){
    // These sizes exercise an odd reduced grid and padded/nonzero source and
    // destination origins. Only actual EncodeV2 produces the paired P plane.
    for(unsigned domain:{1u,16u,32u,0u})for(auto size:{std::pair<unsigned,unsigned>{W,H},{17,9},{9,5}}){
        Fixture f(gpu,size.first,size.second,domain,true);
        auto original=f.Run(0,false);
        for(unsigned mode:{0u,1u,2u})for(bool guard:{false,true}){
            auto result=f.Run(mode,guard);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                auto s=f.input[(y+SY)*(W+SX+3)+x+SX];
                Test(Diff(result[y*W+x],s)==0,"N=P source identity changed");
                Test(Diff(result[y*W+x],original[y*W+x])==0,"disabled/equal zero-edit output differs");
            }
        }
        f.ChangeDarkSide();original=f.Run(0,false);
        for(unsigned mode:{1u,2u}){
            auto result=f.Run(mode,false);
            for(size_t i=0;i<result.size();++i){
                Test(Luma(result[i],domain)>=0,"dark edit became negative");
                if(size.first==W){
                    if(Diff(result[i],original[i])!=0)std::fprintf(stderr,"IDENTITY domain=%u mode=%u pixel=%zu old=(%g,%g,%g) new=(%g,%g,%g) c_size=%zu region_offset=%zu\\n",domain,mode,i,original[i].x,original[i].y,original[i].z,result[i].x,result[i].y,result[i].z,sizeof(CodecConstants),offsetof(CodecConstants,region_flags));
                    Test(Diff(result[i],original[i])==0,"100 percent must preserve original sampler exactly");
                }
            }
            if(size.first==17){
                const auto index=9*W+16;
                auto src=f.input[(9+SY)*(W+SX+3)+16+SX];
                float before=std::abs(Luma(original[index],domain)-Luma(src,domain));
                float after=std::abs(Luma(result[index],domain)-Luma(src,domain));
                std::printf("EDGE domain=%u mode=%u before=%g after=%g\n",domain,mode,before,after);
                Test(after<before*.5f,"source guidance did not reduce synthetic cross-edge dark edit");
                // Same-surface darkening must remain, not all edits wiped out.
                Test(Luma(result[9*W+6],domain)<Luma(src,domain)*.5f,"legitimate dark side edit missing");
            }
        }
    }
    Fixture captureFixture(gpu,17,9,1u,true);
    captureFixture.ChangeDarkSide();
    CapturePair(captureFixture,1u);CapturePair(captureFixture,2u);
    // Master zero and invalid samples retain the exact source, never a partial
    // neighborhood reconstruction. These run through the actual full codec.
    Fixture refusal(gpu,17,9,1u,false);
    refusal.ChangeDarkSide();refusal.c.transfer_strength=0;
    for(unsigned mode:{1u,2u}){
        auto output=refusal.Run(mode,true,true);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
            Test(Diff(output[y*W+x],refusal.input[(y+SY)*(W+SX+3)+x+SX])==0,"zero master modified pixels");
    }
    refusal.c.transfer_strength=1;
    for(auto& v:refusal.neural)v.x=std::numeric_limits<float>::quiet_NaN();
    gpu.UploadTexture(refusal.n,refusal.neural);gpu.Execute();
    for(unsigned mode:{1u,2u}){
        auto output=refusal.Run(mode,true,true);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
            Test(Diff(output[y*W+x],refusal.input[(y+SY)*(W+SX+3)+x+SX])==0,"invalid footprint modified pixels");
    }
    // Actual colour-recovery functions before/after B, full default and protected
    // RGB compositors, with a controlled dark model result and tiny G difference.
    for(unsigned domain:{1u,16u,0u})for(bool rgb:{false,true})for(bool post:{false,true}){
        Fixture a(gpu,W,H,domain,false,true,false);
        auto oldA=a.Run(0,false,rgb,post),newA=a.Run(0,true,rgb,post,negative);
        Fixture b(gpu,W,H,domain,false,true,true);
        auto oldB=b.Run(0,false,rgb,post),newB=b.Run(0,true,rgb,post,negative);
        float oldNoise=0,newNoise=0,lightError=0;
        for(size_t i=0;i<oldA.size();++i){
            oldNoise=std::max(oldNoise,Diff(oldA[i],oldB[i]));newNoise=std::max(newNoise,Diff(newA[i],newB[i]));
            lightError=std::max(lightError,std::abs(Luma(oldA[i],domain)-Luma(newA[i],domain)));
        }
        std::printf("CHROMA domain=%u rgb=%u post=%u old=%g new=%g light_error=%g\n",domain,rgb,post,oldNoise,newNoise,lightError);
        Test(lightError<.002f*std::max(1.f,Luma(oldA[0],domain)),"guard altered chosen new luminance beyond FP16 rounding");
        if(domain==1u&&rgb){
            Test(oldNoise>.005f,"negative control must observe original sensitivity");
            Test(newNoise<oldNoise*.1f,"near-black chroma GPU protection did not intervene");
        }
    }
    // Credible chromatic edits remain; enabling B is exact in this domain.
    Fixture colour(gpu,W,H,1u,false);
    for(auto& v:colour.neural){v.z+=.06f;}
    gpu.UploadTexture(colour.n,colour.neural);gpu.Execute();
    auto credible=colour.Run(0,false,true),protectedColour=colour.Run(0,true,true);
    for(size_t i=0;i<credible.size();++i)Test(Diff(credible[i],protectedColour[i])==0,"credible new colour was weakened");
    std::printf("PASS reconstruction GPU: %u complete-codec cases / %u assertions; controlled N, NOT NGX/game/FPS evidence\n",cases,checks);
}
}
static LONG WINAPI ReconstructionTestFault(EXCEPTION_POINTERS* pointers) {
    MEMORY_BASIC_INFORMATION memory{};char module[MAX_PATH]{};
    auto pc=pointers->ExceptionRecord->ExceptionAddress;
    if(VirtualQuery(pc,&memory,sizeof(memory)))GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH);
    std::fprintf(stderr,"TEST_PROCESS_FAULT code=%08lx module=%s offset=%llx\n",
        static_cast<unsigned long>(pointers->ExceptionRecord->ExceptionCode),module,
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pc)-reinterpret_cast<std::uintptr_t>(memory.AllocationBase)));
    return EXCEPTION_EXECUTE_HANDLER; // Diagnostic only; never resumes execution.
}
int main(int argc,char** argv){
    SetUnhandledExceptionFilter(ReconstructionTestFault);
    try{
        std::setvbuf(stdout,nullptr,_IONBF,0);bool hardware=false,negative=false;const char* artifacts=nullptr;
        for(int i=1;i<argc;++i){
            if(std::strcmp(argv[i],"--hardware")==0)hardware=true;
            else if(std::strcmp(argv[i],"--negative-disable-guard")==0)negative=true;
            else if(std::strncmp(argv[i],"--artifacts=",12)==0)artifacts=argv[i]+12;
            else throw std::runtime_error("Unknown reconstruction GPU argument");
        }
        std::puts("BEGIN complete codec device/PSO initialization");
        Gpu gpu(hardware,"EncodeV2CS",true,artifacts);
        std::puts("BEGIN complete codec GPU cases");
        reconstruction_test::RunAll(gpu,negative);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}

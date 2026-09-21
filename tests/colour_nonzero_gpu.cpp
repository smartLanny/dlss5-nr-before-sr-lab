// Reuse the exact extracted production shader/descriptor fixture, not a second
// shader implementation. N is a controlled FP16 test plane, NOT Feature 18.
#define main InheritedPostScaledWorkMain
#include "post_scaled_work_gpu.cpp"
#undef main

namespace colour_nonzero {
// Odd active extent keeps 100% exact while production alignment selects a
// genuinely different 256x128 raster for 99%; 256x128 itself rounds back up.
constexpr unsigned W = 257, H = 129, SX = 5, SY = 3, DX = 3, DY = 2;
constexpr unsigned Rgb = 1073741824u, Raw = 134217728u;
struct Scene { const char *name; unsigned domain; Float3 left, right; bool flat; bool gradient=false; };
enum class Edit { Identity, LightPlus, LightMinus, ColourPlus, ColourMinus, TinyPlus, TinyMinus };
const char *EditName(Edit edit) {
    switch (edit) {
    case Edit::Identity: return "identity";
    case Edit::LightPlus: return "light+";
    case Edit::LightMinus: return "light-";
    case Edit::ColourPlus: return "colour+";
    case Edit::ColourMinus: return "colour-";
    case Edit::TinyPlus: return "tiny+";
    case Edit::TinyMinus: return "tiny-";
    }
    return "invalid";
}
Float3 Decode(Float4 p, unsigned domain, float source_to_canonical=1.f) {
    if (domain == 32u) return SrgbToLinear({p.x,p.y,p.z});
    if (domain == 0u) return {p.x*source_to_canonical,p.y*source_to_canonical,p.z*source_to_canonical};
    return {p.x,p.y,p.z};
}
Float3 ModelDecode(Float4 p) { return SrgbToLinear({p.x,p.y,p.z}); }
Float3 Prepare(Float3 p, unsigned domain) {
    if (domain != 0u) return p;
    const float peak=std::max({p.x,p.y,p.z});
    if (peak<=0.75f) return p;
    const float excess=(peak-0.75f)/0.25f;
    const float mapped=0.75f+0.25f*excess/std::sqrt(excess*excess+1.f);
    return {p.x*mapped/peak,p.y*mapped/peak,p.z*mapped/peak};
}
Float3 Bilinear(const std::vector<Float4>& plane,unsigned nw,unsigned nh,
                unsigned x,unsigned y,bool shifted=false) {
    const double px=(x+0.5)*nw/W-0.5+(shifted?0.25:0.0);
    const double py=(y+0.5)*nh/H-0.5;
    const int x0=static_cast<int>(std::floor(px)),y0=static_cast<int>(std::floor(py));
    const double fx=px-x0,fy=py-y0;
    Float3 sum{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){
        const auto v=ModelDecode(plane[std::clamp(y0+j,0,int(nh)-1)*nw+
                                       std::clamp(x0+i,0,int(nw)-1)]);
        const float weight=float((i?fx:1-fx)*(j?fy:1-fy));
        sum.x+=v.x*weight;sum.y+=v.y*weight;sum.z+=v.z*weight;
    }
    return sum;
}
Float4 Perturb(Float4 stored,Edit edit) {
    // Signed micro-edit in the actual encoded model domain, before the real
    // FP16 upload. Readback below measures what survived storage.
    if(edit==Edit::TinyPlus||edit==Edit::TinyMinus){
        const float delta=(edit==Edit::TinyPlus?1.f:-1.f)/4096.f;
        return {std::clamp(stored.x+delta,0.f,1.f),std::clamp(stored.y+delta,0.f,1.f),
                std::clamp(stored.z+delta,0.f,1.f),stored.w};
    }
    Float3 n=ModelDecode(stored);
    float* channels[]={&n.x,&n.y,&n.z};
    if(edit==Edit::LightPlus)for(auto* c:channels)*c+=(1.f-*c)/32.f;
    if(edit==Edit::LightMinus)for(auto* c:channels)*c*=31.f/32.f;
    if(edit==Edit::ColourPlus || edit==Edit::ColourMinus){
        const float sign=edit==Edit::ColourPlus?1.f:-1.f;
        n.x=std::clamp(n.x+sign/64.f,0.f,1.f);
        n.y=std::clamp(n.y-sign/128.f,0.f,1.f);
        n.z=std::clamp(n.z+sign/256.f,0.f,1.f);
    }
    if(edit==Edit::Identity)return stored;
    const auto encoded=LinearToSrgb(n);
    return {encoded.x,encoded.y,encoded.z,1.f};
}
void Dispatch(Gpu& gpu,ID3D12PipelineState* pso,const CodecConstants& c) {
    ID3D12DescriptorHeap* heaps[]={gpu.heap.p};
    gpu.list->SetDescriptorHeaps(1,heaps);
    gpu.list->SetComputeRootSignature(gpu.root.p);
    gpu.list->SetComputeRootDescriptorTable(0,gpu.heap->GetGPUDescriptorHandleForHeapStart());
    gpu.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);
    gpu.list->SetPipelineState(pso);
    gpu.list->Dispatch((c.width+7)/8,(c.height+7)/8,1);
}
struct Result { std::vector<Float4> e; double mean_luma=0,mean_edit=0; float max_edit=0; };

void CheckGpuHalfStorage(Gpu& gpu) {
    const float values[]={1.9998f,.49997f,.12499f,1.00048828125f,1.00146484375f,
                          0.0000000298023223876953125f,0.000000059604644775390625f,
                          -1.9998f,65504.f,65520.f};
    const unsigned count=sizeof(values)/sizeof(values[0]);
    // D3D format conversion (Functional Spec 3.2.2) truncates towards zero
    // and saturates finite overflow. This differs from our CPU upload's IEEE
    // nearest-even conversion; do not require identical halfway/carry results.
    const uint16_t expected[]={0x3fff,0x37ff,0x2fff,0x3c00,0x3c01,
                               0x0000,0x0001,0xbfff,0x7bff,0x7bff};
    std::vector<Float4> input;for(float value:values)input.push_back({value,-value,value,1.f});
    auto s=gpu.MakeTexture(count,1,DXGI_FORMAT_R32G32B32A32_FLOAT,input);
    auto p=gpu.MakeTexture(count,1,kModelFormat,std::vector<Float4>(count));
    auto n=gpu.MakeTexture(count,1,kModelFormat,std::vector<Float4>(count));
    auto e=gpu.MakeTexture(count,1,kModelFormat,std::vector<Float4>(count));
    gpu.TextureState(s,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(p,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(n,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(e,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    BindProductionDescriptors(gpu,s,p,n,e,nullptr);
    CodecConstants c{};c.width=count;c.height=1;
    Dispatch(gpu,gpu.copy.p,c);gpu.ReadTexture(e);gpu.Execute();
    const auto stored=gpu.TexturePixels(e);
    bool matches=true;
    for(unsigned i=0;i<count;++i){
        std::printf("HALF_GPU,index=%u,input=%.9g,positive=%04x,negative=%04x,ieee=%04x\n",
                    i,values[i],FloatToHalf(stored[i].x),FloatToHalf(stored[i].y),FloatToHalf(values[i]));
        matches=matches && FloatToHalf(stored[i].x)==expected[i] &&
                FloatToHalf(stored[i].y)==(expected[i]^0x8000u);
    }
    Require(matches,"GPU R16_FLOAT store differs from specified round-to-zero conversion");
    std::puts("PASS production CopyCS FP32-to-FP16 GPU store: signed truncation, ties, subnormals, finite overflow");
}

std::vector<Result> Run(Gpu& gpu,const Scene& scene,unsigned percent,Edit edit,
                       bool wrong_origin, bool actual_controls,float colour_strength=1.f,bool research=false) {
    unsigned nw=W,nh=H;
    if(percent!=100){
        const auto plan=nr_post_compat::Work({W,H},percent);
        SelectAlignedWorkExtent(plan.work.w,plan.work.h,W,H,nw,nh);
    }
    Require(percent==100 ? nw==W && nh==H : nw<W && nh<H,
            "requested reduced scale must really change the model raster");
    const unsigned sw=W+SX+4,sh=H+SY+4,rw=W+DX+4,rh=H+DY+4;
    const Float4 guard{-13.f,-7.f,-3.f,-0.5f};
    std::vector<Float4> source_pixels(sw*sh,guard);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        // Non-central, slanted edge exercises subpixel area and restore phases.
        Float3 value=scene.flat || x<103u+(y/11u)%3u ? scene.left:scene.right;
        if(scene.gradient){const float f=float(x)/float(W-1);
            value={scene.left.x+(scene.right.x-scene.left.x)*f,
                   scene.left.y+(scene.right.y-scene.left.y)*f,
                   scene.left.z+(scene.right.z-scene.left.z)*f};}
        if(scene.domain==32u)value=LinearToSrgb(value);
        source_pixels[(y+SY)*sw+x+SX]={value.x,value.y,value.z,
                                      float(1u+(x+y)%7u)/8.f};
    }
    auto source=gpu.MakeTexture(sw,sh,kModelFormat,source_pixels);
    auto proxy=gpu.MakeTexture(nw,nh,kModelFormat,std::vector<Float4>(nw*nh));
    auto neural=gpu.MakeTexture(nw,nh,kModelFormat,std::vector<Float4>(nw*nh));
    auto result=gpu.MakeTexture(rw,rh,kModelFormat,std::vector<Float4>(rw*rh,guard));
    gpu.TextureState(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(proxy,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    gpu.TextureState(neural,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(result,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    BindProductionDescriptors(gpu,source,proxy,neural,result,nullptr);
    CodecConstants c{};
    c.width=nw;c.height=nh;c.model_width=nw;c.model_height=nh;
    c.source_width=W;c.source_height=H;c.source_base_x=wrong_origin?0:SX;c.source_base_y=SY;
    c.proxy_scale=1.f;c.source_to_canonical=1.f;c.pre_exposure=1.f;
    if(actual_controls){
        c.pre_exposure=1.6f;c.source_to_canonical=1.f/c.pre_exposure;
        c.lighting_lock=.2f;c.edge_guard=.25f;
    }
    c.transfer_strength=1.f;c.color_strength=colour_strength;c.max_luma_ratio=4.f;
    c.frame_confidence=1.f;c.proxy_curve=1u;c.passthrough=scene.domain;
    Dispatch(gpu,gpu.encode.p,c);
    gpu.ReadTexture(source);gpu.ReadTexture(proxy);gpu.Execute();
    const auto s=gpu.TexturePixels(source),p=gpu.TexturePixels(proxy);
    // Independent area-overlap integral from actual FP16 source, followed by
    // declared source-first preparation. Never compare against unquantized S.
    float max_proxy_error=0.f;
    for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){
        const double x0=double(x)*W/nw,x1=double(x+1)*W/nw;
        const double y0=double(y)*H/nh,y1=double(y+1)*H/nh;
        double sums[3]={},weight=0;
        for(unsigned iy=unsigned(std::floor(y0));iy<unsigned(std::ceil(y1));++iy)
            for(unsigned ix=unsigned(std::floor(x0));ix<unsigned(std::ceil(x1));++ix){
                const auto value=Decode(s[(iy+SY)*sw+ix+SX],scene.domain,c.source_to_canonical);
                const double a=(std::min(x1,double(ix+1))-std::max(x0,double(ix)))*
                               (std::min(y1,double(iy+1))-std::max(y0,double(iy)));
                sums[0]+=value.x*a;sums[1]+=value.y*a;sums[2]+=value.z*a;weight+=a;
            }
        Float3 expected=Prepare({float(sums[0]/weight),float(sums[1]/weight),float(sums[2]/weight)},scene.domain);
        expected=LinearToSrgb(expected);
        const auto actual=p[y*nw+x];
        const float error=std::max({std::fabs(expected.x-actual.x),std::fabs(expected.y-actual.y),std::fabs(expected.z-actual.z)});
        max_proxy_error=std::max(max_proxy_error,error);
        Require(error<0.0016f,"production EncodeV2 FP16/source-origin/area oracle mismatch");
        Require(actual.w==1.f,"model proxy alpha must be opaque");
    }
    std::vector<Float4> requested_n=p;
    for(auto& pixel:requested_n)pixel=Perturb(pixel,edit);
    gpu.UploadTexture(neural,requested_n);gpu.ReadTexture(neural);gpu.Execute();
    const auto n=gpu.TexturePixels(neural);
    unsigned nonzero=0;
    for(size_t i=0;i<p.size();++i)if(!RgbEqual(p[i],n[i],0.f))++nonzero;
    Require(edit==Edit::Identity ? nonzero==0 : nonzero>0,
            "actual FP16 neural plane must preserve requested zero/nonzero edit");
    gpu.TextureState(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(proxy,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(neural,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    c.width=W;c.height=H;c.destination_base_x=DX;c.destination_base_y=DY;
    std::vector<Result> results;
    for(unsigned mode=0;mode<3;++mode){
        c.passthrough=scene.domain|(mode?Rgb:0u)|(mode==2?Raw:0u);
        gpu.TextureState(result,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Dispatch(gpu,gpu.combine_v2.p,c);gpu.ReadTexture(result);gpu.Execute();
        Result measure;const auto all=gpu.TexturePixels(result);measure.e.reserve(W*H);
        unsigned invariant_pixels=0,changed=0;float max_raw_error=0.f;
        for(unsigned y=0;y<rh;++y)for(unsigned x=0;x<rw;++x){
            const auto value=all[y*rw+x];
            if(x<DX || y<DY || x>=DX+W || y>=DY+H){
                Require(PixelEqual(value,guard,0.f,"outside-subrect"),"write escaped destination subrect");continue;
            }
            const unsigned ix=x-DX,iy=y-DY;const auto stored=s[(iy+SY)*sw+ix+SX];
            Require(value.w==stored.w,"nonzero compose changed native alpha");
            Require(std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z),"non-finite composed pixel");
            const auto sv=Decode(stored,scene.domain,c.source_to_canonical),ev=Decode(value,scene.domain,c.source_to_canonical);
            const float delta=std::max({std::fabs(ev.x-sv.x),std::fabs(ev.y-sv.y),std::fabs(ev.z-sv.z)});
            measure.mean_luma+=DotLuma(ev);measure.mean_edit+=delta;measure.max_edit=std::max(measure.max_edit,delta);
            measure.e.push_back(value);if(delta>0.001f)++changed;
            if(edit==Edit::Identity)Require(RgbEqual(value,stored,0.001f),"zero-edit source identity lost");
            if(edit==Edit::LightPlus)Require(DotLuma(ev)>=DotLuma(sv)-0.002f*std::max(1.f,DotLuma(sv)),"positive model edit became darkening");
            if(edit==Edit::LightMinus)Require(DotLuma(ev)<=DotLuma(sv)+0.002f*std::max(1.f,DotLuma(sv)),"negative model edit became brightening");
            if(mode==2&&colour_strength==1.f&&!research){
                const auto pp=Bilinear(p,nw,nh,ix,iy),nn=Bilinear(n,nw,nh,ix,iy);
                const auto prepared=Prepare(sv,scene.domain);
                const float k=std::max({sv.x,sv.y,sv.z})/std::max({prepared.x,prepared.y,prepared.z});
                const Float3 candidate{sv.x+(nn.x-pp.x)*k,sv.y+(nn.y-pp.y)*k,sv.z+(nn.z-pp.z)*k};
                const float lum=DotLuma(candidate),sl=DotLuma(sv);
                // This invariant avoids all aesthetic/gamut/ratio policy: an
                // in-gamut, in-ratio T=C=1 edit must equal S + (N-P)/sourceScale.
                if(std::min({candidate.x,candidate.y,candidate.z})>=0.f &&
                   (scene.domain==0u || std::max({candidate.x,candidate.y,candidate.z})<=1.f) &&
                   lum>=sl/4.f && lum<=sl*4.f){
                    const float error=std::max({std::fabs(ev.x-candidate.x),std::fabs(ev.y-candidate.y),std::fabs(ev.z-candidate.z)});
                    const float tolerance=0.0025f*std::max(1.f,std::max({candidate.x,candidate.y,candidate.z}));
                    max_raw_error=std::max(max_raw_error,error);
                    if(error>tolerance)std::fprintf(stderr,"raw invariant scene=%s scale=%u edit=%s pixel=%u,%u error=%g tolerance=%g S=(%g,%g,%g) P=(%g,%g,%g) N=(%g,%g,%g) E=(%g,%g,%g) expected=(%g,%g,%g)\n",scene.name,percent,EditName(edit),ix,iy,error,tolerance,sv.x,sv.y,sv.z,pp.x,pp.y,pp.z,nn.x,nn.y,nn.z,ev.x,ev.y,ev.z,candidate.x,candidate.y,candidate.z);
                    Require(error<=tolerance,"raw RGB nonzero paired reconstruction invariant failed");++invariant_pixels;
                }
            }
        }
        // Research includes legitimate dark rejection, colour=0 and micro-edits
        // below output storage precision. Report their transfer rather than
        // weakening the established large-edit oracles above.
        if(!research)Require(edit==Edit::Identity || changed>0,"nonzero model edit never reached composition");
        if(mode==2&&colour_strength==1.f&&!research)Require(invariant_pixels>W*H/2,"insufficient unclamped raw RGB oracle coverage");
        measure.mean_luma/=W*H;measure.mean_edit/=W*H;
        std::printf("CASE,%s,%u,%u,%u,%s,%u,%.9g,%.9g,%.9g,%.9g,%u,%.9g\n",
            scene.name,percent,nw,nh,EditName(edit),mode,measure.mean_luma,
            measure.mean_edit,measure.max_edit,max_proxy_error,invariant_pixels,max_raw_error);
        if(research)std::printf("RESEARCH,%s,%u,%s,mode=%u,colour=%.9g,stored_edit_pixels=%u,output_above_001=%u,mean_edit=%.9g,max_edit=%.9g\n",
            scene.name,percent,EditName(edit),mode,colour_strength,nonzero,changed,measure.mean_edit,measure.max_edit);
        results.push_back(std::move(measure));
    }
    return results;
}
}

int main(int argc,char** argv){
    try{
        std::setvbuf(stdout,nullptr,_IONBF,0);
        bool hardware=false,wrong_origin=false,half_only=false,inherited=false,actual_controls=false,research=false;
        const char* artifacts=nullptr;
        for(int i=1;i<argc;++i){
            if(std::strcmp(argv[i],"--hardware")==0)hardware=true;
            else if(std::strcmp(argv[i],"--negative-source-origin")==0)wrong_origin=true;
            else if(std::strcmp(argv[i],"--half-only")==0)half_only=true;
            else if(std::strcmp(argv[i],"--inherited-post")==0)inherited=true;
            else if(std::strcmp(argv[i],"--actual-controls")==0)actual_controls=true;
            else if(std::strcmp(argv[i],"--research-matrix")==0)research=true;
            else if(std::strncmp(argv[i],"--artifacts=",12)==0)artifacts=argv[i]+12;
            else throw std::runtime_error("unknown colour validation argument");
        }
        const std::pair<float,std::uint16_t> halves[]={
            {1.9998f,0x4000u},{0.49997f,0x3800u},{0.12499f,0x3000u},
            {1.00048828125f,0x3c00u},{1.00146484375f,0x3c02u},
            {0.0000000298023223876953125f,0u},{0.000000059604644775390625f,1u},
            {-1.9998f,0xc000u},{65504.f,0x7bffu},{65520.f,0x7c00u},
        };
        for(const auto& value:halves)Require(FloatToHalf(value.first)==value.second,"IEEE FP16 fixture boundary/round-to-even mismatch");
        Require(std::isinf(HalfToFloat(0x7c00u)) && std::isnan(HalfToFloat(0x7e00u)),"FP16 readback must not hide nonfinite values");
        std::puts("PASS IEEE FP16 fixture exponent carry/ties/subnormals/nonfinite");
        if(half_only)return 0;
        if(inherited)return InheritedPostScaledWorkMain(argc,argv);
        Gpu gpu(hardware,"EncodeV2CS",false,artifacts,true);
        using namespace colour_nonzero;
        CheckGpuHalfStorage(gpu);
        if(research){
            const Scene research_scenes[]={
                {"dark-gradient",16u,{1.f/65536,1.f/65536,1.f/65536},{1.f/128,1.f/128,1.f/128},false,true},
                {"saturated-edge",16u,{.875f,.03125f,.0625f},{.03125f,.8125f,.046875f},false},
                {"hdr-highlight",0u,{4.f,1.f,.25f},{16.f,6.f,1.5f},false}};
            unsigned count=0;
            for(const auto& scene:research_scenes)for(float strength:{0.f,.7f,1.f})
                for(unsigned percent:{100u,99u,75u,50u,33u,25u})
                    for(auto edit:{Edit::Identity,Edit::TinyPlus,Edit::TinyMinus,Edit::ColourPlus,Edit::ColourMinus}){
                        Run(gpu,scene,percent,edit,wrong_origin,false,strength,true);count+=3;
                    }
            std::printf("PASS colour research: %u GPU observations with storage/source/finite/alpha/subrect oracles; transfer magnitude is measured, not a colour-quality acceptance\n",count);
            return 0;
        }
        std::printf("CONTROLS,preExposure=%g,sourceToCanonical=%g,LightingLock=%g,EdgeGuard=%g\n",
            actual_controls?1.6:1.,actual_controls?.625:1.,actual_controls?.2:0.,actual_controls?.25:0.);
        const Scene scenes[]={
            {actual_controls?"hdr-flat":"flat",actual_controls?0u:16u,{.25f,.25f,.25f},{.25f,.25f,.25f},true},
            {"gray-step",16u,{.0625f,.0625f,.0625f},{.75f,.75f,.75f},false},
            {"colour-step",16u,{.125f,.25f,.375f},{.625f,.4375f,.1875f},false},
            {"saturated-step",16u,{.875f,.0625f,.125f},{.0625f,.8125f,.09375f},false},
            {"hdr-step",0u,{4.f,1.f,.25f},{16.f,6.f,1.5f},false},
            {"srgb-step",32u,{.125f,.25f,.375f},{.625f,.4375f,.1875f},false},
        };
        unsigned scenarios=0;
        std::puts("CASE,scene,requested_percent,model_w,model_h,edit,restore_mode,mean_luma,mean_abs_edit,max_abs_edit,max_proxy_error,raw_oracle_pixels,max_raw_error");
        for(const auto& scene:scenes)for(auto edit:{Edit::Identity,Edit::LightPlus,Edit::LightMinus,Edit::ColourPlus,Edit::ColourMinus}){
            std::vector<Result> reference;
            for(unsigned percent:{100u,99u,75u,50u}){
                auto result=Run(gpu,scene,percent,edit,wrong_origin,actual_controls);scenarios+=3;
                if(percent==100){reference=std::move(result);continue;}
                for(unsigned mode=0;mode<3;++mode){
                    double difference=0;float peak=0;
                    for(size_t i=0;i<result[mode].e.size();++i){
                        const float d=OutputRgbDistance(result[mode].e[i],reference[mode].e[i]);difference+=d;peak=std::max(peak,d);
                    }
                    if(scene.flat)Require(peak<=0.001f,"flat nonzero edit changes with model scale");
                    std::printf("SCALE,%s,%s,%u,%u,%.9g,%.9g\n",scene.name,EditName(edit),percent,mode,difference/(W*H),peak);
                }
            }
        }
        std::printf("PASS colour nonzero: %u real D3D12 cases; FP16 S/P/N/E; actual EncodeV2/CombineV2; controlled N, NOT NVIDIA inference/game/native SR O\n",scenarios);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL colour nonzero: %s\n",e.what());return 1;}
}

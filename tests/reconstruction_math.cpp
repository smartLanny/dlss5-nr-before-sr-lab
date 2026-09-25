// Includes the production HLSL functions through the existing float3 adapter.
#include "reconstruction.generated.hpp"
#include "nr_reconstruction_policy.h"
static unsigned checks=0;
static void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL %s at %u\n",message,checks);std::exit(1);}}
static void near(float3 a,float3 b,float tol,const char* why){check(ColourLabAbsPeak(a-b)<=tol,why);}
static float3 protect(float3 s,float3 n,float3 r,bool on=true,bool sdr=false){return NrReconPreserveColour(s,s,s,n,r,sdr,on);}
int main(){
    using namespace nr_reconstruction;
    // Exhaustive policy: requested != recorded. The new bits do not touch any
    // old region/generic bit and are absent for unsupported/diagnostic routes.
    for(int request=-1;request<=3;++request)for(unsigned b=0;b<1024;++b){
        bool guard=b&1,combine=b&2,known=b&4,raw=b&8,v2=b&16,scaled=b&32;
        bool old=b&64,field=b&128,colour=b&256,preview=b&512;
        auto s=Select(request,guard,combine,known,raw,v2,scaled,old,field,colour,preview);
        bool common=combine&&known&&!raw&&!preview;
        check(s.mode==(common&&v2&&scaled&&!old&&!field?Normalize(request):0),"policy sampling scope");
        check(s.chroma==(guard&&common&&colour),"policy chroma scope");
        check((s.flags&63u)==0&&((s.flags>>8)&3u)==unsigned(s.mode)&&bool(s.flags&kChromaFlag)==s.chroma,"actual root word flags");
    }
    for(float peak:{0.f,.0001f,.001f,.004f,.008f,.1f}){
        float prev=1.f;
        for(float gain:{1.f,2.f,4.f,8.f,64.f}){
            float w=NrReconChromaConfidence(peak,gain);
            check(w>=0&&w<=1&&w<=prev,"bounded monotone gain");prev=w;
            if(gain<=2||peak>=1.f/128)check(w==1,"credible colour exactly unchanged");
        }
    }
    check(NrReconChromaConfidence(.0001f,8)==0,"both risks intervene");
    check(NrReconChromaConfidence(.02f,8)==1,"not a global darkness/saturation gate");
    for(float phase:{0.f,.01f,.3f,.5f,.99f,1.f}){
        float w=0;for(int i=-1;i<3;++i)w+=NrReconCubicWeight(float(i)-phase);
        check(std::abs(w-1.f)<2e-7f,"cubic constant partition");
    }
    std::mt19937 rng(3072026);std::uniform_real_distribution<float> u(0.f,1.f);
    for(unsigned i=0;i<20000;++i){
        const float scale=std::exp2(u(rng)*20.f-10.f);
        float3 source{(.1f+u(rng))*scale,(.1f+u(rng))*scale,(.1f+u(rng))*scale};
        float target=dot(source,kLuma)*(.5f+u(rng));
        float3 colour{.1f+u(rng),.1f+u(rng),.1f+u(rng)};
        colour=colour*(target/dot(colour,kLuma));
        float3 prepared=source*(.5f/ColourLabPeak(source));
        float3 proxy=prepared;
        float3 candidate{.0001f,.0001f+u(rng)*.00001f,.0001f};
        float3 out=NrReconPreserveColour(source,prepared,proxy,candidate,colour,false,true);
        check(all(isfinite(out)),"finite HDR/SDR output");
        check(std::abs(dot(out,kLuma)-target)<=std::max(target*6e-7f,1e-7f),"preserve NEW target light, not original light");
        near(NrReconPreserveColour(source,prepared,proxy,proxy,colour,false,true),colour,0,"zero edit exact");
        near(NrReconPreserveColour(source,prepared,proxy,candidate,colour,false,false),colour,0,"off exact");
        near(NrReconPreserveColour(source,prepared,proxy,prepared*.9f,colour,false,true),colour,0,"credible changed colours preserved");
    }
    const float3 s{.4f,.3f,.2f};
    auto old1=ColourLabRestoreRgb(s,s,s,{.0001f,.0001f,.0001f},-.999f,-.5f,.5f,1,2,1,0,0,false,false,false);
    auto old2=ColourLabRestoreRgb(s,s,s,{.0001f,.00015f,.0001f},-.999f,-.5f,.5f,1,2,1,0,0,false,false,false);
    auto a=protect(s,{.0001f,.0001f,.0001f},old1.rgb);
    auto b=protect(s,{.0001f,.00015f,.0001f},old2.rgb);
    float before=ColourLabAbsPeak(old2.rgb-old1.rgb),after=ColourLabAbsPeak(b-a);
    check(before>.01f&&after<.00002f,"actual existing near-black sensitivity reduced");
    check(std::abs(dot(a,kLuma)-dot(old1.rgb,kLuma))<1e-7f,"protected luminance not changed");
    std::printf("synthetic_near_black maxRGB before=%.9g after=%.9g (not game noise)\n",before,after);
    float3 cyan{0,.6f,.8f};near(protect({.2f,.2f,0},cyan,cyan),cyan,0,"reliable zero-channel new colour preserved");
    // A saturated original may be outside gamut when rescaled to the NEW
    // target. The existing whole-vector gamut mapping preserves that target.
    auto saturated=protect({1,0,0},{.0001f,.0001f,.0001f},{.5f,.5f,.5f},true,true);
    check(ColourLabPeak(saturated)<=1.000001f&&std::abs(dot(saturated,kLuma)-.5f)<1e-7f,"bounded SDR keeps new light");
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        near(protect(s,{bad,.001f,.001f},{.2f,.2f,.2f}),{.2f,.2f,.2f},0,"bad candidate leaves prior safe result");
    }
    near(protect(s,{-1,.01f,.01f},{.2f,.2f,.2f}),{.2f,.2f,.2f},0,"signed unsupported candidate is not secretly rectified");
    std::printf("PASS %u actual-production math and policy assertions; no inference or game claim\n",checks);
}

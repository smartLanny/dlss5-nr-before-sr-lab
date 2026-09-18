// The generated include contains the actual HLSL math, not a CPU reimplementation.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include "../src/nr_colour_restore_policy.h"
using uint = unsigned;
using std::abs; using std::min; using std::max; using std::clamp; using std::isfinite;
struct bool3 { bool x,y,z; };
bool all(bool3 b) { return b.x && b.y && b.z; }
bool any(bool3 b) { return b.x || b.y || b.z; }
struct float3 {
    float x,y,z;
    float3(float a,float b,float c):x(a),y(b),z(c) {}
    float3():float3(0,0,0) {}
    float& operator[](uint i) { return i==0?x:i==1?y:z; }
    float operator[](uint i) const { return i==0?x:i==1?y:z; }
    float3& operator+=(float3 b) { x+=b.x;y+=b.y;z+=b.z;return *this; }
    float3& operator*=(float b) { x*=b;y*=b;z*=b;return *this; }
};
float3 operator+(float3 a,float3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
float3 operator-(float3 a,float3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
float3 operator*(float3 a,float b) { return {a.x*b,a.y*b,a.z*b}; }
float3 operator/(float3 a,float b) { return {a.x/b,a.y/b,a.z/b}; }
bool3 operator<(float3 a,float b) { return {a.x<b,a.y<b,a.z<b}; }
bool3 operator==(float3 a,float b) { return {a.x==b,a.y==b,a.z==b}; }
bool3 isfinite(float3 a) { return {isfinite(a.x),isfinite(a.y),isfinite(a.z)}; }
float3 min(float3 a,float b) { return {min(a.x,b),min(a.y,b),min(a.z,b)}; }
float3 max(float3 a,float b) { return {max(a.x,b),max(a.y,b),max(a.z,b)}; }
float3 clamp(float3 a,float3 l,float3 h) { return {clamp(a.x,l.x,h.x),clamp(a.y,l.y,h.y),clamp(a.z,l.z,h.z)}; }
float3 lerp(float3 a,float3 b,float t) { return a+(b-a)*t; }
float dot(float3 a,float3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float rsqrt(float a) { return 1.0f/std::sqrt(a); }
float saturate(float a) { return clamp(a,0.0f,1.0f); }
const float3 kLuma{0.2126f,0.7152f,0.0722f};
#include "colour_restore_math.generated.hpp"

static unsigned checks = 0;
void require(bool condition,const char* label) {
    ++checks;
    if (!condition) { std::fprintf(stderr,"FAIL: %s (check %u)\n",label,checks);std::exit(1); }
}
void close(float3 a,float3 b,float tolerance,const char* label) {
    require(ColourLabAbsPeak(a-b)<=tolerance,label);
}
ColourLabRgbResult restore(float3 s,float3 p,float3 n,float gain=1.0f,float c=1.0f,
                           float confidence=1.0f,float maxRatio=64.0f,bool sdr=false,
                           float region=1.0f,float face=0.0f,float originalColor=0.0f,bool cap=false,
                           bool rawReference=false) {
    const float raw = dot(n-p,kLuma)/max(dot(p,kLuma),1.0f/4096.0f);
    return ColourLabRestoreRgb(s,p,p,n,raw,raw*gain,c,confidence,maxRatio,region,face,originalColor,cap,sdr,rawReference);
}

int main() {
    static_assert(nr_colour_restore::Normalize(-1)==0);
    static_assert(nr_colour_restore::Normalize(2)==2);
    static_assert(nr_colour_restore::Normalize(1)==1);
    static_assert(nr_colour_restore::Flags(1,false,nr_colour_restore::Encoding::Post)==0);
    static_assert(nr_colour_restore::Flags(0,true,nr_colour_restore::Encoding::Post)==0);
    static_assert(nr_colour_restore::Flags(1,true,nr_colour_restore::Encoding::V2)==1073741824u);
    static_assert(nr_colour_restore::Flags(1,true,nr_colour_restore::Encoding::Post)==1610612736u);
    static_assert(nr_colour_restore::Flags(2,true,nr_colour_restore::Encoding::V2)==1207959552u);
    static_assert(nr_colour_restore::Flags(2,true,nr_colour_restore::Encoding::Post)==1744830464u);
    static_assert(nr_colour_restore::Flags(1,true,nr_colour_restore::Encoding::Unknown)==0);
    constexpr nr_colour_restore::EncodingReceipt receipt{nr_colour_restore::Encoding::Post,42};
    static_assert(receipt.For(42)==nr_colour_restore::Encoding::Post);
    static_assert(receipt.For(43)==nr_colour_restore::Encoding::Unknown);
    static_assert(receipt.For(0)==nr_colour_restore::Encoding::Unknown);
    static_assert(nr_colour_restore::EffectiveInputCurve(1,0u)==1);
    static_assert(nr_colour_restore::EffectiveInputCurve(1,128u)==1);
    static_assert(nr_colour_restore::EffectiveInputCurve(1,1u)==0);
    static_assert(nr_colour_restore::EffectiveInputCurve(1,16u)==0);
    static_assert(nr_colour_restore::EffectiveInputCurve(1,32u)==0);
    static_assert(nr_colour_restore::EffectiveInputCurve(0,0u)==0);
    static_assert(nr_colour_restore::EffectiveInputCurve(99,0u)==0);
    static_assert(nr_colour_restore::EffectiveScaledProxyMode(1,true,true)==1);
    static_assert(nr_colour_restore::EffectiveScaledProxyMode(1,true,false)==0);
    static_assert(nr_colour_restore::EffectiveScaledProxyMode(1,false,true)==0);
    static_assert(nr_colour_restore::EffectiveScaledProxyMode(0,true,true)==0);
    // Demonstrates the zero-channel failure that a multiplicative compositor
    // cannot repair: the model adds blue to a source with no blue channel.
    const float3 s{.2f,.3f,0};const float3 n{.2f,.3f,.05f};
    auto value=restore(s,s,n);
    require(value.valid,"zero-channel valid");close(value.rgb,n,2e-6f,"zero-channel colour survives");
    auto mono=restore(s,s,n,1,0);
    close(mono.rgb,s*(dot(n,kLuma)/dot(s,kLuma)),2e-6f,"C0 only luminance");
    require(mono.rgb.z==0,"C0 does not create blue");
    auto amplified=restore(s,s,n,2,1);
    close(value.rgb/dot(value.rgb,kLuma),amplified.rgb/dot(amplified.rgb,kLuma),2e-6f,
          "independent C not divided by T");
    close(restore(s,s,n,1,1,1,64,false,2,1,1,true).rgb,mono.rgb,2e-6f,"face original colour retained");
    close(restore(s,s,n,1,1,1,1).rgb,s,2e-6f,"ratio one is identity");
    close(restore(s,s,n,0,0).rgb,s,0,"all adopted edits off is exact identity");
    close(restore(s,s,n,1,1,0).rgb,s,0,"zero frame confidence identity");
    close(restore(s,s,s).rgb,s,0,"model proxy identity");
    require(ColourLabLumaGain(.1f,-.1f)==0,"no sign reversal");
    require(ColourLabLumaGain(0,.1f)==0,"no invented luma from zero");
    require(ColourLabLumaGain(.1f,.1f)==1,"exact 1x gain");
    auto protectedWeak=restore(s,s,n,.15f,1,1,64,false,1,0,0,false,false);
    auto rawReference=restore(s,s,n,.15f,1,0,64,false,.1f,1,1,true,true);
    close(rawReference.rgb,n,2e-6f,"raw reference keeps model edit despite aesthetic attenuation");
    require(ColourLabAbsPeak(rawReference.rgb-protectedWeak.rgb)>1e-4f,
            "raw reference differs from protected luma");
    close(restore(s,s,n,1,1,0,64,false,.1f,1,1,true,true).rgb,n,2e-6f,
          "raw reference ignores confidence region face damping");

    // Source-anchored N2 test: neural is decoded model linear, not an inverse
    // curve input. A new neutral grey comes back on the source's own scale.
    const float3 hdr{1,4,12};float k=1/std::sqrt(1+12.0f*12.0f);
    const float decoded=.6038273389f; // sRGB 0.8 decoded to linear.
    float3 grey{decoded,decoded,decoded};auto sourceRef=restore(hdr,ColourLabN2Prepare(hdr),grey);
    require(sourceRef.valid,"N2 recovery valid");close(sourceRef.rgb,grey/k,3e-5f,"N2 source reference not neural inverse");
    require(sourceRef.rgb.x>7,"HDR energy not normalized into SDR");

    // A local full source differs from the filtered work-grid input. Restoring
    // only the matched edit preserves the full source's existing texture.
    float3 local{.43f,.38f,.25f}, pair{.2f,.2f,.2f}, edit{.01f,.02f,.03f};
    float raw=dot(edit,kLuma)/dot(pair,kLuma);
    auto highFrequency=ColourLabRestoreRgb(local,local,pair,pair+edit,raw,raw,1,1,64,1,0,0,false,false,false);
    close(highFrequency.rgb,local+edit,2e-6f,"restore residual not low resolution full image");

    for (float bad: {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        require(!restore(s,s,{bad,.2f,.2f}).valid,"invalid neural rejected");
        require(!restore({bad,.2f,.2f},s,n).valid,"invalid original rejected");
        require(!restore(s,s,n,1,bad).valid,"invalid constant rejected");
    }
    require(!restore({-.01f,.2f,.3f},s,n).valid,"unknown signed preparation rejected");
    require(!restore(s,{0,0,0},n).valid,"no source-scale proof rejected");
    close(restore({0,0,0},{0,0,0},n).rgb,{0,0,0},0,"black not invented");

    std::mt19937 rng(0xD210C012u);
    std::uniform_real_distribution<float> u(.08f,.70f), v(.85f,1.15f), compression(.025f,1.0f);
    for (unsigned i=0;i<20000;++i) {
        float3 source{u(rng),u(rng),u(rng)};
        float3 desired{source.x*v(rng),source.y*v(rng),source.z*v(rng)};
        float scale=compression(rng);
        float3 proxy=source*scale, neural=desired*scale;
        auto r=restore(source,proxy,neural,1,1,1,64,true);
        require(r.valid,"random neutral valid");close(r.rgb,desired,3e-6f,"random full RGB anchor");
        auto identity=restore(source,proxy,proxy,3,2,1,64,true);
        close(identity.rgb,source,0,"random bit-exact matched identity");
        auto luma=restore(source,proxy,neural,1,0);
        close(luma.rgb,source*(dot(desired,kLuma)/dot(source,kLuma)),3e-6f,"random C0 luma");
        auto boost=restore(source,proxy,neural,2,1);
        require(boost.valid,"random boost valid");
        close(boost.rgb/dot(boost.rgb,kLuma),r.rgb/dot(r.rgb,kLuma),4e-6f,"random hue independent of T");
        auto bounded=restore(source,proxy,{2.5f,0,2.0f},4,2,1,4,true,4,.6f,.2f,true);
        require(bounded.valid && all(isfinite(bounded.rgb)),"gamut guard finite");
        require(!any(bounded.rgb < -2e-6f) && ColourLabPeak(bounded.rgb)<=1.000002f,"SDR gamut bound");
        require(dot(bounded.rgb,kLuma)<=dot(source,kLuma)*4+3e-6f,"luma ratio bound");
    }
    // HDR source scale sweeps. No neural inverse pole as model approaches 1.
    for (float peak: {1.0f,4.0f,12.0f,100.0f,1000.0f,16000.0f}) {
        float3 source{peak*.25f,peak*.5f,peak};float scale=1/std::sqrt(1+peak*peak);
        for(float m: {.9f,.99f,1.0f,1.01f}) {
            float3 model{m*.25f,m*.5f,m};auto r=restore(source,ColourLabN2Prepare(source),model);
            require(r.valid && all(isfinite(r.rgb)),"HDR source reference finite");
            close(r.rgb,model/scale,max(1e-5f,peak*2e-6f),"HDR source reference oracle");
        }
    }
    close(ColourLabN2Prepare({0,0,0}),{0,0,0},0,"N2 black");
    close(ColourLabN2Prepare({-1,.4f,.8f}),ColourLabN2Prepare({0,.4f,.8f}),0,"N2 matches existing positive preparation");
    close(ColourLabN2Prepare({std::numeric_limits<float>::infinity(),1,1}),{0,0,0},0,"N2 invalid becomes finite model input");
    for(unsigned i=0;i<10000;++i) {
        float peak=std::exp2(-10.0f+24.0f*float(i)/9999.0f);
        float3 x{peak*.15f,peak*.60f,peak};auto prepared=ColourLabN2Prepare(x);
        require(all(isfinite(prepared)) && ColourLabPeak(prepared)<=1.000001f,"N2 bounded sweep");
        close(prepared, x*(1/std::sqrt(1+peak*peak)),2e-6f,"N2 shared preparation scalar");
    }
    std::printf("colour_restore_math: %u checks passed; production HLSL math on host CPU, not GPU/NGX/game verification\n",checks);
}

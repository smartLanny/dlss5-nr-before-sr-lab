// The included functions are extracted verbatim from the production shader.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include "../src/nr_colour_edge_policy.h"
#include "../src/nr_colour_restore_policy.h"
using uint = unsigned;
using std::abs; using std::min; using std::max; using std::isfinite;
struct bool3 { bool x,y,z; };
bool all(bool3 b) { return b.x&&b.y&&b.z; }
struct float3 {
    float x,y,z;
    float3():x(0),y(0),z(0) {}
    float3(float a,float b,float c):x(a),y(b),z(c) {}
    float3& operator+=(float3 b) { x+=b.x;y+=b.y;z+=b.z;return *this; }
};
float3 operator+(float3 a,float3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
float3 operator-(float3 a,float3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
float3 operator*(float3 a,float b) { return {a.x*b,a.y*b,a.z*b}; }
float3 operator/(float3 a,float b) { return {a.x/b,a.y/b,a.z/b}; }
bool3 isfinite(float3 a) { return {isfinite(a.x),isfinite(a.y),isfinite(a.z)}; }
float ColourLabAbsPeak(float3 a) { return max(abs(a.x),max(abs(a.y),abs(a.z))); }
#include "colour4_pair.generated.hpp"
static unsigned checks=0;
void require(bool condition,const char* what) {
    ++checks;
    if(!condition) {std::fprintf(stderr,"FAIL %s at %u\n",what,checks);std::exit(1);}
}
void close(float3 a,float3 b,float tolerance,const char* what) {
    require(ColourLabAbsPeak(a-b)<=tolerance,what);
}

int main() {
    using namespace nr_colour_edge;
    static_assert(kFlag == (1u<<24));
    static_assert((kFlag & ((1u<<25)|nr_colour_restore::kRgbFlag|
        nr_colour_restore::kPostEncodingFlag|nr_colour_restore::kInputCurveFlag|
        nr_colour_restore::kRawEditFlag|nr_colour_restore::kProxyFirstScaleFlag))==0u);
    for(int edge=-1;edge<=2;++edge) for(unsigned state=0;state<32;++state) {
        bool combine=state&1,rgb=state&2,known=state&4,v2=state&8,scaled=state&16;
        unsigned expected=edge==1&&combine&&rgb&&known&&v2&&scaled ? kFlag:0u;
        require(Flags(edge,combine,rgb,known,v2,scaled)==expected,"actual dispatch eligibility");
    }
    std::mt19937 rng(0xD210C004u);
    std::uniform_real_distribution<float> positive(0.0001f,1.0f), unit(0.0f,1.0f);
    for(unsigned i=0;i<30000;++i) {
        const float scale=std::exp2(-12.0f+unit(rng)*24.0f);
        const float3 reference{positive(rng)*scale,positive(rng)*scale,positive(rng)*scale};
        const float3 constantEdit{scale*.01f,scale*-.02f,scale*.03f};
        const float fx=unit(rng),fy=unit(rng);
        const float weights[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
        auto identity=ColourLabEmptyPair(), edit=ColourLabEmptyPair();
        float3 lo{scale,scale,scale},hi{0,0,0};
        for(unsigned j=0;j<4;++j) {
            float3 p{positive(rng)*scale,positive(rng)*scale,positive(rng)*scale};
            float w=ColourLabGuideWeight(reference,p);
            require(w>0&&w<=1&&isfinite(w),"positive finite guide weight");
            identity=ColourLabAddPair(identity,reference,p,p,weights[j]);
            edit=ColourLabAddPair(edit,reference,p,p+constantEdit,weights[j]);
            lo={min(lo.x,p.x),min(lo.y,p.y),min(lo.z,p.z)};
            hi={max(hi.x,p.x),max(hi.y,p.y),max(hi.z,p.z)};
        }
        identity=ColourLabFinishPair(identity);edit=ColourLabFinishPair(edit);
        require(identity.valid&&edit.valid,"valid pair");
        close(identity.proxy,identity.neural,0,"bit-exact N=P after both accumulations");
        close(edit.proxy,identity.proxy,0,"neural never controls guidance");
        close(edit.neural-edit.proxy,constantEdit,scale*8e-7f,"constant edit preserved within float precision");
        const auto p=identity.proxy;
        require(p.x>=lo.x-scale*2e-7f&&p.x<=hi.x+scale*2e-7f&&
                p.y>=lo.y-scale*2e-7f&&p.y<=hi.y+scale*2e-7f&&
                p.z>=lo.z-scale*2e-7f&&p.z<=hi.z+scale*2e-7f,"convex proxy range");
    }
    // Synthetic surface boundary: deliberately edit only the dark-side sample.
    // This is an algorithmic case, NOT a claim about the owner's game.
    const float3 dark{.1f,.1f,.1f}, bright{.8f,.8f,.8f}, changed{0,0,0};
    auto boundary=ColourLabEmptyPair();
    boundary=ColourLabAddPair(boundary,bright,dark,changed,.5f);
    boundary=ColourLabAddPair(boundary,bright,bright,bright,.5f);
    boundary=ColourLabFinishPair(boundary);
    const float baseline=0.05f;
    const float matched=ColourLabAbsPeak(boundary.neural-boundary.proxy);
    require(boundary.valid&&matched<baseline*.1f,"reduced foreign-surface edit in controlled case");
    std::printf("synthetic_boundary: bilinear_edit=%.8f matched_edit=%.8f (not game measurement)\n",baseline,matched);
    require(!ColourLabFinishPair(ColourLabEmptyPair()).valid,"empty pair rejected");
    for(float bad:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        require(!ColourLabFinishPair(ColourLabAddPair(ColourLabEmptyPair(),bright,dark,{bad,0,0},1)).valid,"invalid neural rejected");
        require(!ColourLabFinishPair(ColourLabAddPair(ColourLabEmptyPair(),{bad,0,0},dark,dark,1)).valid,"invalid reference rejected");
        require(!ColourLabFinishPair(ColourLabAddPair(ColourLabEmptyPair(),bright,dark,dark,bad)).valid,"invalid spatial weight rejected");
    }
    require(!ColourLabFinishPair(ColourLabAddPair(ColourLabEmptyPair(),bright,dark,dark,-1)).valid,"negative spatial weight rejected");
    auto signedPair=ColourLabFinishPair(ColourLabAddPair(ColourLabEmptyPair(),bright,dark,{-1,.2f,-.1f},1));
    require(signedPair.valid,"signed edit stays signed until existing gamut stage");
    close(signedPair.neural,{-1,.2f,-.1f},1e-6f,"signed sample not saturated by reconstruction");
    std::printf("colour_edge_math: %u checks passed; production math on host CPU, NOT HLSL/GPU/game validation\n",checks);
}

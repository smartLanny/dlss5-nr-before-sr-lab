// Production HLSL helper/restore math, with a minimal host vector adapter.
// This does not execute a D3D shader, NGX inference or a game.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include "../src/nr_generic_residual.h"
using uint = unsigned;
using std::isfinite;
float min(float a,float b){return std::min(a,b);}
float max(float a,float b){return std::max(a,b);}
float abs(float a){return std::abs(a);}
float pow(float a,float b){return std::pow(a,b);}
float clamp(float a,float lo,float hi){return std::clamp(a,lo,hi);}
float saturate(float a){return clamp(a,0,1);}
float frac(float a){return a-std::floor(a);}
float lerp(float a,float b,float t){return a+(b-a)*t;}
float smoothstep(float a,float b,float v){const float t=saturate((v-a)/(b-a));return t*t*(3-2*t);}
using std::log2;
using std::sqrt;
struct float2{
    float x,y;
    float2(float a,float b):x(a),y(b){}
    float2& operator+=(float2 v){x+=v.x;y+=v.y;return *this;}
};
float2 operator+(float2 a,float2 b){return {a.x+b.x,a.y+b.y};}
float2 operator-(float2 a,float2 b){return {a.x-b.x,a.y-b.y};}
float2 operator-(float2 a,float b){return {a.x-b,a.y-b};}
float2 operator*(float2 a,float2 b){return {a.x*b.x,a.y*b.y};}
float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
float2 operator*(float a,float2 b){return {a*b.x,a*b.y};}
float2 round(float2 v){return {std::round(v.x),std::round(v.y)};}
struct int2{int x,y;explicit int2(float2 v):x(int(v.x)),y(int(v.y)){};};
struct float4{
    float x,y,z,w;
    float4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
};
struct bool3{bool x,y,z;};
bool all(bool3 b){return b.x&&b.y&&b.z;}
bool any(bool3 b){return b.x||b.y||b.z;}
struct float3{
    union{struct{float x,y,z;};struct{float r,g,b;};};
    float3():x(0),y(0),z(0){}
    explicit float3(float a):x(a),y(a),z(a){}
    float3(float a,float b_,float c):x(a),y(b_),z(c){}
    float& operator[](uint i){return i==0?x:i==1?y:z;}
    float operator[](uint i)const{return i==0?x:i==1?y:z;}
    float3& operator+=(float3 v){x+=v.x;y+=v.y;z+=v.z;return *this;}
    float3& operator*=(float v){x*=v;y*=v;z*=v;return *this;}
};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float3 operator+(float3 a,float b){return a+float3(b);}
float3 operator+(float a,float3 b){return float3(a)+b;}
float3 operator-(float3 a,float b){return a-float3(b);}
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float3 operator/(float3 a,float b){return {a.x/b,a.y/b,a.z/b};}
bool3 operator==(float3 a,float b){return {a.x==b,a.y==b,a.z==b};}
bool3 operator<(float3 a,float b){return {a.x<b,a.y<b,a.z<b};}
bool3 operator<=(float3 a,float b){return {a.x<=b,a.y<=b,a.z<=b};}
bool3 isfinite(float3 a){return {isfinite(a.x),isfinite(a.y),isfinite(a.z)};}
float3 abs(float3 a){return {abs(a.x),abs(a.y),abs(a.z)};}
float3 min(float3 a,float b){return {min(a.x,b),min(a.y,b),min(a.z,b)};}
float3 max(float3 a,float b){return {max(a.x,b),max(a.y,b),max(a.z,b)};}
float3 clamp(float3 a,float3 lo,float3 hi){return {clamp(a.x,lo.x,hi.x),clamp(a.y,lo.y,hi.y),clamp(a.z,lo.z,hi.z)};}
float3 saturate(float3 a){return {saturate(a.x),saturate(a.y),saturate(a.z)};}
float3 frac(float3 a){return {frac(a.x),frac(a.y),frac(a.z)};}
float3 lerp(float3 a,float3 b,float t){return a+(b-a)*t;}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
const float3 kLuma{.2126f,.7152f,.0722f};
float decode(float s){return s<=.04045f?s/12.92f:pow((s+.055f)/1.055f,2.4f);}
float encode(float s){return s<=.0031308f?s*12.92f:1.055f*pow(s,1.f/2.4f)-.055f;}
float3 decode(float3 s){return {decode(s.x),decode(s.y),decode(s.z)};}
float3 encode(float3 s){return {encode(s.x),encode(s.y),encode(s.z)};}

// Texture adapters retain the production nearest-grid sampling and detail
// loop below. Fixtures are either uniform or a three-column luminance patch.
uint RegionFlags=0,Width=64,Height=64,ModelWidth=64,ModelHeight=64;
float RegionWideBrightLuma=1,RegionWideBrightColor=1,RegionFineBrightLuma=1,
      RegionFineBrightColor=1,RegionWideDarkLuma=1,DetailStability=0;
float3 tapProxy(.25f),tapNeural(.5f);
bool tapPattern=false,tapPrecontrolled=false;
float3 LoadProxyLinearV2(int2){return tapProxy;}
float3 LoadNeuralLinearV2(int2 p){
    float3 n=tapNeural;
    if(tapPattern)n+=float3(float((p.x%3+3)%3-1)*.2f);
    // An independently prepared grey texture for bright=2, total/light=1.
    // Flag-off sampling of this texture is the detail-filter reference.
    if(tapPrecontrolled)n=tapProxy+(n-tapProxy)*2;
    return n;
}

// The runner injects the exact production markers and the exact legacy HSL
// functions used only by the deliberately failing regression variant.
#include "generic_continuity.generated.hpp"

static unsigned checks=0;
void require(bool b,const char* label){++checks;if(!b){std::fprintf(stderr,"FAIL %s (%u)\n",label,checks);std::exit(41);}}
void close(float a,float b,float tolerance,const char* label){require(abs(a-b)<=tolerance,label);}
void close(float3 a,float3 b,float tolerance,const char* label){require(ColourLabAbsPeak(a-b)<=tolerance,label);}

// Exact production RGB terminal, driven by the helper's separate luminance.
// source==prepared==proxy makes the expected source scale unambiguous. Other
// source scales and existing region/face guards stay in their existing tests.
float3 rgbTerminal(float3 original,const GenericControlledV2& c,float globalColour=1.f){
    const float delta=c.lumaLinear-dot(original,kLuma);
    const float raw=delta/max(dot(original,kLuma),1.f/4096.f);
    auto out=ColourLabRestoreRgb(original,original,original,c.colorLinear,
        raw,raw,globalColour,1,64,1,0,0,false,true,false,true,delta);
    require(out.valid,"controlled RGB terminal valid");
    return out.rgb;
}

// Reference for the native default ratio terminals under neutral face,
// lighting and region controls, unit confidence and no gamut clipping. It
// intentionally retains the two established Core ratio representations.
float3 ratioTerminal(float3 source,const GenericControlledV2& c,bool post){
    const float y=dot(source,kLuma),target=clamp(c.lumaLinear,y/64,y*64);
    const float3 lumaOnly=source*(target/y);
    float3 result;
    if(post){
        const float3 ratio={clamp(c.colorLinear.x/max(source.x,1.f/1024.f),1.f/64,64),
            clamp(c.colorLinear.y/max(source.y,1.f/1024.f),1.f/64,64),
            clamp(c.colorLinear.z/max(source.z,1.f/1024.f),1.f/64,64)};
        result={source.x*ratio.x,source.y*ratio.y,source.z*ratio.z};
    }else{
        const float protectedLumaLog=std::log2(max(c.lumaLinear,1.f/4096.f)/max(y,1.f/4096.f));
        for(uint i=0;i<3;++i){
            const float rgbLog=std::log2(max(c.colorLinear[i],1.f/4096.f)/max(source[i],1.f/4096.f));
            result[i]=source[i]*std::exp2(clamp(rgbLog-protectedLumaLog,-6,6));
        }
    }
    const float ry=dot(result,kLuma);
    return ry>1.f/65536.f?result*(target/ry):lumaOnly;
}

void controlledNeighborhood(){
    const float2 center(16.5f,16.5f);
    RegionFlags=64;RegionWideBrightColor=2;RegionFineBrightColor=0;
    auto c=GenericControlLinearV2(tapProxy,tapNeural,1,2,1,0,1);
    const float py=dot(tapProxy,kLuma);
    const float raw=clamp(log2(c.lumaLinear/py),-8,8);
    const float mean=ModelLightingMeanV2(center,py,c.lumaLinear);
    const float protectedLog=clamp(raw-mean,min(raw,0),max(raw,0));
    auto protectedValue=c;
    protectedValue.lumaLinear=py*std::exp2(protectedLog);
    // Reviewer reproducer: source/proxy=.25, NR=.5, bright=2, colour=0.
    // Raw-neighbour sampling yields a 2.4 mean and an erroneous .3125 output.
    // Both actual compositors call the extracted mean above; their terminal
    // ratio references must return .25 with full LightingLock and unit transfer.
    for(bool post:{true,false})
        close(ratioTerminal(tapProxy,protectedValue,post),tapProxy,2e-7f,
              "LightingLock removes uniform controlled drift in both ratio terminals");
    const float relative=(c.lumaLinear-py)/py;
    auto rgb=ColourLabRestoreRgb(tapProxy,tapProxy,tapProxy,c.colorLinear,
        relative,std::exp2(protectedLog)-1,1,1,64,1,0,0,false,true,false,true,c.lumaLinear-py);
    require(rgb.valid,"LightingLock controlled RGB valid");
    close(rgb.rgb,tapProxy,2e-7f,"LightingLock removes uniform controlled drift in RGB terminal");

    DetailStability=.8f;tapPattern=true;
    auto decision=DetailDecisionV2(center,raw,py);
    tapPrecontrolled=true;RegionFlags=0;
    auto expected=DetailDecisionV2(center,raw,py);
    require(expected.x<.99f,"detail fixture exercises nonuniform attenuation");
    close(decision.x,expected.x,2e-6f,"detail neighbourhood uses same controlled luminance");
    close(decision.w,expected.w,2e-6f,"detail terminal matches precontrolled model field");
    tapPattern=false;tapPrecontrolled=false;DetailStability=0;

    // Legacy/neutral flag-off must ignore retained continuous slider values.
    close(GenericNeuralLumaV2(tapProxy,tapNeural),dot(tapNeural,kLuma),0,
          "flag-off scalar tap exactly raw");
    close(ModelLumaRatioV2(center),2,0,"flag-off detail sample exactly raw");
    close(ModelLightingTapV2(center,py).x,2,0,"flag-off lighting sample exactly raw");
    RegionFlags=64;
    for(float total:{1.f,1.5f,2.f})for(float gain:{0.f,.5f,1.f,2.f}){
        RegionWideBrightLuma=total;RegionWideBrightColor=gain;
        RegionFineBrightLuma=2-gain;RegionWideDarkLuma=.7f;
        for(float level:{.125f,.25f,.5f}){
            tapNeural=float3(level);
            auto value=GenericControlLinearV2(tapProxy,tapNeural,total,gain,2-gain,0,.7f);
            close(GenericNeuralLumaV2(tapProxy,tapNeural),value.lumaLinear,0,
                  "centre and scalar tap gains match exactly");
            const float expectedRatio=clamp(max(value.lumaLinear,1.f/4096.f)/py,1.f/256,256);
            close(ModelLumaRatioV2(center),expectedRatio,0,"controlled detail ratio matches centre");
            close(ModelLightingTapV2(center,py).x,expectedRatio,0,"controlled lighting ratio matches centre");
        }
    }
    RegionFlags=0;RegionWideBrightLuma=RegionWideBrightColor=RegionFineBrightLuma=
        RegionFineBrightColor=RegionWideDarkLuma=1;
    tapNeural=float3(.5f);
}

int main(){
    struct Config{bool generic_residual=false;unsigned generic_algorithm=0;float generic_gains[5]{1,1,1,1,1};} c;
    using nr_generic::Reason;
    require(nr_generic::Select(c,true,true,false,false).flags==0,"default off");
    c.generic_residual=true;
    require(nr_generic::Select(c,false,false,true,true).flags==32,"legacy full consumer policy retained");
    c.generic_algorithm=1;
    require(nr_generic::Select(c,true,true,false,false).reason==Reason::Neutral,"neutral exact bypass policy");
    c.generic_gains[3]=.5f;
    require(nr_generic::Select(c,true,true,false,false).flags==64,"verified SDR final admitted");
    require(nr_generic::Select(c,true,false,false,false).flags==0,"unknown HDR not relabelled SDR");
    require(nr_generic::Select(c,true,true,true,false).reason==Reason::RawReference,"raw identity stays raw");
    require(nr_generic::Select(c,true,true,false,true).reason==Reason::LowGridConsumer,"no unsupported low-grid claim");
    require(nr_generic::Select(c,false,true,false,false).flags==0,"new controls never applied at field builders");
    c.generic_algorithm=99;c.generic_gains[0]=0;c.generic_gains[1]=std::nanf("");nr_generic::Normalize(c);
    require(c.generic_algorithm==0&&c.generic_gains[0]==1&&c.generic_gains[1]==1,"version/range normalization preserves legacy default");

    const float3 p=decode(float3(.4f,.5f,.7f)),n=decode(float3(.55f,.6f,.72f));
    auto neutral=GenericControlLinearV2(p,n,1,1,1,1,1);
    close(neutral.colorLinear,n,0,"all-neutral exact candidate");
    close(rgbTerminal(p,neutral),n,2e-6f,"neutral RGB terminal anchor");
    auto brightness=GenericControlLinearV2(p,n,1,.5f,1.5f,0,1);
    close(brightness.colorLinear,p,0,"colour zero exact original candidate");
    require(brightness.lumaLinear>dot(p,kLuma),"colour zero does not erase brightness signal");
    const auto lightOnly=p*(brightness.lumaLinear/dot(p,kLuma));
    close(rgbTerminal(p,brightness),lightOnly,2e-6f,"RGB terminal preserves brightness with colour zero");
    close(ratioTerminal(p,brightness,true),lightOnly,2e-6f,"post ratio preserves brightness with colour zero");
    close(ratioTerminal(p,brightness,false),lightOnly,2e-6f,"V2 ratio preserves brightness with colour zero");
    auto colourOnly=GenericControlLinearV2(p,n,1,.5f,1.5f,1,0);
    close(colourOnly.lumaLinear,dot(p,kLuma),0,"light zero does not rewrite colour signal");
    auto color=rgbTerminal(p,colourOnly);
    require(ColourLabAbsPeak(color-p)>1e-4f,"RGB terminal keeps colour-only edits");
    close(dot(color,kLuma),dot(p,kLuma),2e-6f,"colour-only terminal preserves target Y");
    require(ColourLabAbsPeak(ratioTerminal(p,colourOnly,true)-p)>1e-4f,"post ratio keeps colour-only edits");
    require(ColourLabAbsPeak(ratioTerminal(p,colourOnly,false)-p)>1e-4f,"V2 ratio keeps colour-only edits");
    close(dot(ratioTerminal(p,colourOnly,true),kLuma),dot(p,kLuma),2e-6f,"post ratio independent Y");
    close(dot(ratioTerminal(p,colourOnly,false),kLuma),dot(p,kLuma),2e-6f,"V2 ratio independent Y");
    for(float gain:{0.f,.5f,1.f,2.f}){
        auto value=GenericControlLinearV2(p,n,1,.5f,1.5f,gain,1);
        close(value.lumaLinear,brightness.lumaLinear,0,"colour slider never changes target Y");
        close(dot(rgbTerminal(p,value),kLuma),brightness.lumaLinear,3e-6f,"independent Y survives final Core RGB controls");
        auto zero=GenericControlLinearV2(p,n,0,2,2,gain,2);
        close(zero.colorLinear,p,0,"pure helper total zero candidate");
        close(rgbTerminal(p,zero),p,0,"pure helper total zero final identity");
    }
    controlledNeighborhood();

    // Actual #274 grey-axis reproducer, swept through the discontinuity.
    float maxGreyStep=0;
    float3 previous{};
    for(int i=-100;i<=100;++i){
        auto q=decode(float3(.5f+i*1e-6f,.5f,.5f));
        auto out=GenericControlLinearV2(p,q,1,1,1,.5f,1);
        require(out.valid,"grey-axis finite");
        auto final=rgbTerminal(p,out);
        if(i>-100)maxGreyStep=max(maxGreyStep,ColourLabAbsPeak(final-previous));
        previous=final;
    }
    require(maxGreyStep<2e-5f,"grey-axis continuous at final RGB terminal");

    // Nonzero chroma crosses exactly zero linear luminance edit. The negative
    // control reinstates the old full-vector bright/dark multiplier here.
    float maxDirectionStep=0;
    const float3 axisP{.4f,.5f,.7f};
    const float zeroG=(dot(axisP,kLuma)-.7f*kLuma.x-.3f*kLuma.z)/kLuma.y;
    for(int i=-20;i<=20;++i){
        float3 q{.7f,zeroG+i*1e-6f,.3f};
        auto out=GenericControlLinearV2(axisP,q,1,.5f,1.5f,1,1);
        auto final=rgbTerminal(axisP,out);
        if(i>-20)maxDirectionStep=max(maxDirectionStep,ColourLabAbsPeak(final-previous));
        previous=final;
    }
    require(maxDirectionStep<2e-5f,"bright-dark zero crossing continuous at final RGB terminal");

    // Finite signed/over-range candidates are not clamped when a slider moves
    // infinitesimally off neutral. The final Core validity/gamut rules remain.
    for(float3 answer:{float3(-.03f,.2f,1.2f),float3(2,.4f,.3f),float3(.9f,-.1f,.1f)}){
        auto base=GenericControlLinearV2(p,answer,1,1,1,1,1);
        close(base.colorLinear,answer,0,"signed over-range exact neutral");
        for(float epsilon:{-1e-6f,1e-6f}){
            auto nearby=GenericControlLinearV2(p,answer,1,1,1,1+epsilon,1);
            require(nearby.valid,"near-neutral signed finite");
            close(nearby.colorLinear,answer,1e-5f,"near-neutral no new clamp discontinuity");
        }
    }
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        require(!GenericControlLinearV2(p,{bad,.2f,.3f},1,1,1,.5f,1).valid,"invalid neural rejected");
        require(!GenericControlLinearV2(p,n,1,1,1,bad,1).valid,"invalid gain rejected");
    }
    std::mt19937 rng(0x2743109u);std::uniform_real_distribution<float> u(.15f,.45f),e(-.025f,.025f);
    for(unsigned i=0;i<2048;++i){
        float3 a{u(rng),u(rng),u(rng)},b=a+float3(e(rng),e(rng),e(rng));
        auto base=GenericControlLinearV2(a,b,1,1,1,1,1);
        close(base.colorLinear,b,0,"random neutral exact");
        close(rgbTerminal(a,base),b,3e-6f,"random final neutral anchor");
        for(float k:{0.f,.5f,1.f,2.f}){
            auto out=GenericControlLinearV2(a,b,1,.4f,1.6f,k,.8f);
            require(out.valid,"random controls finite");
            const float raw=dot(b,kLuma)-dot(a,kLuma);
            const float y=dot(a,kLuma)+(max(raw,0)*.4f+min(raw,0)*1.6f)*.8f;
            close(out.lumaLinear,y,0,"random independent scalar formula");
            close(dot(rgbTerminal(a,out),kLuma),y,3e-6f,"random final protected Y");
            auto identity=GenericControlLinearV2(a,a,2,.5f,1.5f,k,.8f);
            close(identity.colorLinear,a,0,"random no-edit exact");
            close(rgbTerminal(a,identity),a,0,"random no-edit terminal exact");
        }
    }
    std::printf("generic_continuity: %u checks passed; grey_max_step=%.9g direction_max_step=%.9g; exact HLSL math/CPU terminal references, no GPU/NGX/game execution\n",checks,maxGreyStep,maxDirectionStep);
}

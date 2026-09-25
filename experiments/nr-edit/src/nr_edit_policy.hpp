#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace nr_edit_policy {
enum class Mode:unsigned {Off=0,Deferred=1,AcrossRr=2};
struct Settings {
    unsigned mode=0,budget_mib=512;
    float carrier_range=4.0f,history_rate=.08f;
    bool rr_history=true;
    friend bool operator==(const Settings&,const Settings&)=default;
};
inline Settings Sanitize(Settings s) {
    if(s.mode>2)s.mode=0;
    s.budget_mib=std::clamp(s.budget_mib,64u,1024u);
    s.carrier_range=std::isfinite(s.carrier_range)?std::clamp(s.carrier_range,.125f,64.f):4.f;
    s.history_rate=std::isfinite(s.history_rate)?std::clamp(s.history_rate,.01f,1.f):.08f;
    return s;
}
inline bool Selected(const Settings& s,unsigned feature) {
    return (s.mode==1&&feature==1)||(s.mode==2&&feature==13);
}
using Matrix=std::array<double,16>;
inline Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix c{};for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)for(unsigned k=0;k<4;k++)c[y*4+x]+=a[y*4+k]*b[k*4+x];return c;
}
inline bool Invert(const Matrix& a,Matrix& inverse) {
    double m[4][8]{};
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){
        if(!std::isfinite(a[y*4+x]))return false;
        m[y][x]=a[y*4+x];m[y][x+4]=y==x?1:0;
    }
    for(unsigned col=0;col<4;col++) {
        unsigned pivot=col;for(unsigned y=col+1;y<4;y++)if(std::abs(m[y][col])>std::abs(m[pivot][col]))pivot=y;
        if(!std::isfinite(m[pivot][col])||std::abs(m[pivot][col])<1e-12)return false;
        for(unsigned x=0;x<8;x++)std::swap(m[col][x],m[pivot][x]);
        const double d=m[col][col];for(double& x:m[col])x/=d;
        for(unsigned y=0;y<4;y++)if(y!=col){const double f=m[y][col];for(unsigned x=0;x<8;x++)m[y][x]-=f*m[col][x];}
    }
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)inverse[y*4+x]=m[y][x+4];
    const auto check=Multiply(a,inverse);for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)
        if(!std::isfinite(inverse[y*4+x])||std::abs(check[y*4+x]-(x==y?1:0))>1e-5)return false;
    return true;
}
inline Matrix ColumnMajor(const float* values) {
    Matrix out{};for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)out[y*4+x]=values[x*4+y];return out;
}
struct Camera {Matrix world_to_clip{};bool verified=false;};
inline bool PreviousClip(const Camera& current,const Camera& previous,float (&out)[16]) {
    if(!current.verified||!previous.verified)return false;
    Matrix inv{};if(!Invert(current.world_to_clip,inv))return false;
    auto m=Multiply(previous.world_to_clip,inv);
    for(unsigned i=0;i<16;i++){if(!std::isfinite(m[i])||std::abs(m[i])>1e9)return false;out[i]=static_cast<float>(m[i]);}
    return true;
}
// Official NGX: HDR=1, MVLowRes=2, MVJittered=4, DepthInverted=8, AutoExposure=64.
inline unsigned CarrierFlags(unsigned native_flags,bool inverted) {return 2u|(native_flags&4u)|(inverted?8u:0u);}
inline bool Consecutive(std::uint64_t previous,std::uint64_t current) {
    return previous&&previous!=UINT64_MAX&&current==previous+1;
}
}

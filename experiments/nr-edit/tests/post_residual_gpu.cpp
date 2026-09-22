// Real shader pixels and production dispatch/transport wrappers. Reconstruction
// is an explicit analytic fixture, never an NR or private SR inference claim.
#define NR_EDIT_SHADER_VALIDATION_LIBRARY
#include "windows_shader_validation.cpp"
#include "nr_post_residual_dx12.hpp"
#include <algorithm>
namespace {
using Pixel=std::array<float,4>;
std::uint64_t checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::uint16_t Pack(float value){
    std::uint32_t bits=0;std::memcpy(&bits,&value,4);const auto sign=(bits>>16)&0x8000;
    if((bits&0x7f800000)==0x7f800000)return std::uint16_t(sign|((bits&0x7fffff)?0x7e00:0x7c00));
    const float a=std::abs(value);if(a==0)return std::uint16_t(sign);
    int e=0;std::frexp(a,&e);--e;
    if(e< -14)return std::uint16_t(sign|unsigned(std::nearbyint(std::ldexp(a,24))));
    unsigned mantissa=unsigned(std::nearbyint(std::ldexp(a,10-e)));
    if(mantissa==2048){mantissa=1024;++e;}
    return std::uint16_t(sign|unsigned(e+15)*1024|(mantissa-1024));
}
float Round(float v){return std::isfinite(v)?Unhalf(Pack(v)):v;}
ID3D12Resource* Pixels(Harness& h,ResourceOwner& owner,unsigned w,unsigned height,const std::vector<Pixel>& values){
    auto* p=h.Texture(owner,w,height,10,0,0,false);auto desc=p->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;h.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto* upload=h.Buffer(owner,bytes,D3D12_HEAP_TYPE_UPLOAD);void* mapped=nullptr;D3D12_RANGE read{0,0};Check(upload->Map(0,&read,&mapped),"post upload");
    std::memset(mapped,0,static_cast<std::size_t>(bytes));
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c){
        auto q=Pack(values[y*w+x][c]);std::memcpy(static_cast<char*>(mapped)+y*fp.Footprint.RowPitch+x*8+c*2,&q,2);
    }
    upload->Unmap(0,nullptr);Transition(h.list,p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    to.pResource=p;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(h.list,p,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);return p;
}
std::vector<Pixel> ReadPixels(const Harness::Readback& r){
    void* data=nullptr;Check(r.buffer->Map(0,nullptr,&data),"post readback");std::vector<Pixel> out(r.w*r.h);
    for(unsigned y=0;y<r.h;++y)for(unsigned x=0;x<r.w;++x)for(unsigned c=0;c<4;++c){
        const auto* p=static_cast<char*>(data)+r.footprint.Offset+y*r.footprint.Footprint.RowPitch+x*PixelBytes(r.format)+(r.format==10?2:4)*c;
        if(r.format==10){std::uint16_t q=0;std::memcpy(&q,p,2);out[y*r.w+x][c]=(q&0x7c00)==0x7c00?
            ((q&1023)?std::numeric_limits<float>::quiet_NaN():((q&0x8000)?-1.f:1.f)*std::numeric_limits<float>::infinity()):Unhalf(q);}
        else std::memcpy(&out[y*r.w+x][c],p,4);
    }
    D3D12_RANGE none{0,0};r.buffer->Unmap(0,&none);return out;
}
void Near(float actual,float expected,float tolerance,const char* what,unsigned x,unsigned y,unsigned c){
    ++checks;
    if(!std::isfinite(expected)&&((std::isnan(expected)&&std::isnan(actual))||actual==expected))return;
    if(!std::isfinite(actual)||std::abs(actual-expected)>tolerance){
        std::cerr<<what<<" "<<x<<","<<y<<","<<c<<" got="<<actual<<" expected="<<expected<<"\n";
        throw std::runtime_error(what);
    }
}
struct Footprint{int x[4],y[4];float w[4];};
Footprint Bilinear(unsigned x,unsigned y,unsigned w,unsigned height,unsigned nw,unsigned nh){
    const int ax=int((2*x+1)*nw)-int(w),ay=int((2*y+1)*nh)-int(height),dx=int(2*w),dy=int(2*height);
    const int ix=ax/dx-(ax<0&&ax%dx!=0),iy=ay/dy-(ay<0&&ay%dy!=0);
    const float fx=float(ax-ix*dx)/dx,fy=float(ay-iy*dy)/dy;Footprint f{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){const auto k=j*2+i;f.x[k]=std::clamp(ix+i,0,int(nw)-1);f.y[k]=std::clamp(iy+j,0,int(nh)-1);f.w[k]=(i?fx:1-fx)*(j?fy:1-fy);}return f;
}
void Scenario(Harness& h,unsigned percent,unsigned kind,bool alias_target){
    using namespace nr_edit_dx12;using namespace nr_post_residual_dx12;ResourceOwner owner;
    constexpr unsigned w=129,height=97,aw=140,ah=109;
    const unsigned nw=(w*percent+99)/100,nh=(height*percent+99)/100;
    const Rect br{3,5,w,height},zr{6,2,w,height},tr=alias_target?zr:Rect{4,4,w,height};
    const Pixel sentinel{.4375f,.4375f,.4375f,.75f};
    std::vector<Pixel> b(aw*ah,sentinel),z(aw*ah,sentinel),target(aw*ah,sentinel);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){
        Pixel base{.25f+float(x%4)/16,.5f,.75f-float(y%4)/16,.5f},candidate=base;candidate[3]=.75f;
        for(unsigned c=0;c<3;++c){
            float d=kind==0?0:kind==1?1.f/1024:kind==2?-1.f/2048:
                kind==3?float(int((x+3*y+c)%7)-3)/32:kind==4?.1875f:.0625f;
            candidate[c]=Round(base[c]+d);
        }
        if((kind==5||kind==6)&&x==w/2&&y==height/2)base[0]=kind==5?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
        if((kind==8||kind==9)&&x==w/2&&y==height/2)candidate[0]=kind==8?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
        b[(y+br.y)*aw+x+br.x]=base;z[(y+zr.y)*aw+x+zr.x]=candidate;target[(y+tr.y)*aw+x+tr.x]=candidate;
    }
    const float range=kind==4?.125f:4;
    std::vector<Pixel> expectedCarrier(nw*nh),expectedTail(nw*nh);
    for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){
        const unsigned lx=x*w,hx=(x+1)*w,ly=y*height,hy=(y+1)*height;
        double sum[3]{},total=0;bool valid=true;
        for(unsigned sy=ly/nh;sy<(hy+nh-1)/nh;++sy)for(unsigned sx=lx/nw;sx<(hx+nw-1)/nw;++sx){
            const auto weight=double(std::min(hx,(sx+1)*nw)-std::max(lx,sx*nw))*(std::min(hy,(sy+1)*nh)-std::max(ly,sy*nh));
            const auto& bv=b[(sy+br.y)*aw+sx+br.x];const auto& zv=z[(sy+zr.y)*aw+sx+zr.x];
            for(unsigned c=0;c<3;++c){valid&=std::isfinite(bv[c])&&std::isfinite(zv[c]);if(std::isfinite(bv[c]))sum[c]+=(zv[c]-bv[c])*weight;}
            total+=weight;
        }
        Pixel carrier{.5f,.5f,.5f,1},tail{};bool support=false;
        if(valid){for(unsigned c=0;c<3;++c){float d=float(sum[c]/total);carrier[c]=Round(.5f+.5f*std::clamp(d,-range,range)/range);
            float represented=(2*carrier[c]-1)*range;tail[c]=d-represented;support|=represented!=0;}tail[3]=support?2.f:1.f;}
        expectedCarrier[y*nw+x]=carrier;expectedTail[y*nw+x]=tail;
    }
    std::vector<Pixel> reconstructed(w*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){
        const auto f=Bilinear(x,y,w,height,nw,nh);Pixel pixel{0,0,0,1};
        for(unsigned c=0;c<3;++c){float sum=0;for(unsigned k=0;k<4;++k)sum+=expectedCarrier[f.y[k]*nw+f.x[k]][c]*f.w[k];pixel[c]=kind<=2?.625f:Round(sum);}
        if(kind==7&&x==w/2&&y==height/2)pixel[0]=std::numeric_limits<float>::infinity();reconstructed[y*w+x]=pixel;
    }
    auto* bp=Pixels(h,owner,aw,ah,b);auto* zp=Pixels(h,owner,aw,ah,z);
    auto* tp=alias_target?zp:Pixels(h,owner,aw,ah,target);auto* up=Pixels(h,owner,w,height,reconstructed);
    auto* carrier=h.Texture(owner,nw,nh,10,0,0,false);auto* tail=h.Texture(owner,nw,nh,2,0,0,false);
    Pair pair{};pair.base=h.View(bp,10);pair.candidate=h.View(zp,10);pair.base_rect=br;pair.candidate_rect=zr;
    pair.base_source=pair.candidate_source={1,1,h.epoch};pair.domain=Domain::SdrEncodedFp16;pair.same_encoding_verified=true;
    pair.work_width=nw;pair.work_height=nh;pair.carrier_range=range;
    // Every refusal is before dispatch and must not consume a packet.
    auto bad=pair;bad.domain=Domain::Unknown;Require(!Encode(h.kernels,h.list,h.Rec(),bad,h.View(carrier,10),h.View(tail,2)),"unknown domain refused");
    bad=pair;bad.same_encoding_verified=false;Require(!Encode(h.kernels,h.list,h.Rec(),bad,h.View(carrier,10),h.View(tail,2)),"unproven encoding refused");
    bad=pair;bad.candidate_source.sequence++;Require(!Encode(h.kernels,h.list,h.Rec(),bad,h.View(carrier,10),h.View(tail,2)),"different source refused");
    bad=pair;bad.candidate=bad.base;Require(!Encode(h.kernels,h.list,h.Rec(),bad,h.View(carrier,10),h.View(tail,2)),"B cannot alias Z");
    bad=pair;bad.base_rect.x=aw;Require(!Encode(h.kernels,h.list,h.Rec(),bad,h.View(carrier,10),h.View(tail,2)),"out-of-range source refused");
    bad=pair;bad.work_width=1;Require(!Encode(h.kernels,h.list,h.Rec(),bad,h.View(carrier,10),h.View(tail,2)),"below-quarter raster refused");
    auto e=Encode(h.kernels,h.list,h.Rec(),pair,h.View(carrier,10),h.View(tail,2));Require(bool(e),"post extraction");
    Reconstruction r{};r.carrier=h.View(up,10);r.target=h.View(tp,10);r.target_rect=tr;r.recording=h.Rec();
    r.source=pair.base_source;r.encoded_token=e.dispatch.token;r.succeeded=r.target_contains_candidate=true;
    auto invalid=r;invalid.target=pair.base;Require(!Compose(h.kernels,h.list,e,invalid).recorded,"do not overwrite B");
    invalid=r;invalid.target_contains_candidate=false;Require(!Compose(h.kernels,h.list,e,invalid).recorded,"missing candidate fallback refused");
    invalid=r;invalid.source.sequence++;Require(!Compose(h.kernels,h.list,e,invalid).recorded,"stale reconstruction refused");
    invalid=r;invalid.encoded_token.serial++;Require(!Compose(h.kernels,h.list,e,invalid).recorded,"different carrier generation refused");
    invalid=r;invalid.succeeded=false;Require(!Compose(h.kernels,h.list,e,invalid).recorded,"failed reconstruction refused");
    invalid=r;invalid.target=invalid.carrier;invalid.target_rect={0,0,w,height};Require(!Compose(h.kernels,h.list,e,invalid).recorded,"same dispatch read/write refused");
    auto carrierRead=h.CopyOut(owner,carrier,10),tailRead=h.CopyOut(owner,tail,2);
    Require(Compose(h.kernels,h.list,e,r).recorded,"post compose");auto targetRead=h.CopyOut(owner,tp,10),baseRead=h.CopyOut(owner,bp,10);h.Submit();
    const auto gotCarrier=ReadPixels(carrierRead),gotTail=ReadPixels(tailRead),gotTarget=ReadPixels(targetRead),gotBase=ReadPixels(baseRead);
    for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x)for(unsigned c=0;c<4;++c){
        Near(gotCarrier[y*nw+x][c],expectedCarrier[y*nw+x][c],0,"carrier nearest-even",x,y,c);
        Near(gotTail[y*nw+x][c],expectedTail[y*nw+x][c],2e-7f,"exact area tail",x,y,c);
    }
    for(unsigned y=0;y<ah;++y)for(unsigned x=0;x<aw;++x){
        for(unsigned c=0;c<4;++c)Near(gotBase[y*aw+x][c],b[y*aw+x][c],0,"B remains immutable",x,y,c);
        Pixel expected=sentinel;
        if(x>=tr.x&&x<tr.x+w&&y>=tr.y&&y<tr.y+height){
            const unsigned px=x-tr.x,py=y-tr.y;expected=z[(py+zr.y)*aw+px+zr.x];const auto& base=b[(py+br.y)*aw+px+br.x];
            const auto& u=reconstructed[py*w+px];const auto f=Bilinear(px,py,w,height,nw,nh);bool valid=true,support=false;float total=0;Pixel t{};
            for(unsigned c=0;c<4;++c)valid&=std::isfinite(base[c]);for(unsigned c=0;c<3;++c)valid&=std::isfinite(u[c]);
            for(unsigned k=0;k<4;++k){const auto& v=expectedTail[f.y[k]*nw+f.x[k]];if(f.w[k]>0){valid&=v[3]>=.5f;support|=v[3]>=1.5f;}
                for(unsigned c=0;c<3;++c)t[c]+=v[c]*f.w[k];total+=f.w[k];}
            if(valid){for(unsigned c=0;c<3;++c)expected[c]=Round(base[c]+(support?(2*std::clamp(u[c],0.f,1.f)-1)*range:0)+t[c]/total);expected[3]=base[3];}
        }
        for(unsigned c=0;c<4;++c)Near(gotTarget[y*aw+x][c],expected[c],kind==3||kind==4?0.00048828125f:0,"B plus once / untouched atlas",x,y,c);
    }
    h.Reset();std::cout<<"post percent="<<percent<<" actual="<<nw<<'x'<<nh<<" kind="<<kind<<" targetIsCandidate="<<alias_target<<" passed\n";
}
}
int wmain(int argc,wchar_t** argv){try{
    Require(argc==2||(argc==3&&std::wstring(argv[2])==L"--nvidia"),"usage: post_residual_gpu <shaders> [--nvidia]");
    Harness h(std::filesystem::path(argv[1]),argc==3);
    for(unsigned percent:{25u,33u,50u,75u,99u,100u})for(unsigned kind=0;kind<10;++kind)Scenario(h,percent,kind,kind%2==0);
    std::cout<<"PASS post-final-residual "<<(argc==3?"NVIDIA":"WARP")<<" cases=60 checks="<<checks<<"; analytic reconstruction, no NR/private SR/game claim\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

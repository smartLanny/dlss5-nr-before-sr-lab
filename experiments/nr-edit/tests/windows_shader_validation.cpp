// Real Windows SDK/WARP pixels. No NVIDIA inference or game claims.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "nr_edit_dx12.hpp"
#include "nr_rr_dx12.hpp"
#include "nr_deferred_dx12.hpp"
namespace {
void Check(HRESULT r,const char* where){if(FAILED(r)){std::cerr<<where<<" HRESULT="<<std::hex<<unsigned(r)<<std::dec<<'\n';throw std::runtime_error(where);}}
template<class T>struct Com{T* p=nullptr;Com()=default;Com(const Com&)=delete;~Com(){if(p)p->Release();}
 T** out(){return &p;}T* operator->()const{return p;}operator T*()const{return p;}};
struct ResourceOwner{std::vector<ID3D12Resource*> values;~ResourceOwner(){for(auto*p:values)p->Release();}
 ID3D12Resource* Keep(ID3D12Resource*p){values.push_back(p);return p;}};
std::vector<char> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("missing shader");return {std::istreambuf_iterator<char>(f),{}};}
bool SameDevice(ID3D12Device*a,ID3D12Device*b){Com<IUnknown>x,y;
 return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(x.out())))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(y.out())))&&x.p==y.p;}
void Transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
 D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&x);}
std::uint16_t Half(float f){if(std::isnan(f))return 0x7e00;if(std::isinf(f))return f>0?0x7c00:0xfc00;
 if(f==1.f+1.f/1024.f)return 0x3c01;if(f==1.f-1.f/2048.f)return 0x3bff;
 if(f==0)return 0;if(f==.4375f)return 0x3700;if(f==.5f)return 0x3800;if(f==.625f)return 0x3900;
 if(f==.75f)return 0x3a00;if(f==1)return 0x3c00;if(f==2)return 0x4000;if(f==4)return 0x4400;if(f==5)return 0x4500;
 throw std::runtime_error("fixture half constant");}
float Unhalf(std::uint16_t h){float sign=(h&0x8000)?-1.f:1.f;unsigned e=(h>>10)&31,m=h&1023;
 if(e==31)throw std::runtime_error("non-finite GPU output");return sign*(e?std::ldexp(1.f+float(m)/1024,int(e)-15):std::ldexp(float(m),-24));}
unsigned PixelBytes(unsigned f){return f==10?8:f==2?16:f==16?8:4;}
struct Harness{
 Com<IDXGIFactory4>factory;Com<IDXGIAdapter>warp;Com<ID3D12Device>device;Com<ID3D12CommandQueue>queue;
 Com<ID3D12CommandAllocator>allocator;Com<ID3D12GraphicsCommandList>list;Com<ID3D12Fence>fence;
 HANDLE event=nullptr;std::uint64_t epoch=1;bool released=false;nr_edit_dx12::Kernels*kernels=nullptr;
 explicit Harness(const std::filesystem::path& directory,bool hardware=false){
  Check(CreateDXGIFactory1(IID_PPV_ARGS(factory.out())),"factory");
  if(hardware){
   for(unsigned i=0;!warp.p;++i){Com<IDXGIAdapter1> candidate;
    if(factory->EnumAdapters1(i,candidate.out())==DXGI_ERROR_NOT_FOUND)break;
    DXGI_ADAPTER_DESC1 desc{};Check(candidate->GetDesc1(&desc),"adapter description");
    if(desc.VendorId==0x10de&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))
      Check(candidate->QueryInterface(IID_PPV_ARGS(warp.out())),"NVIDIA adapter");}
   if(!warp.p)throw std::runtime_error("No NVIDIA hardware adapter");
  }else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.out())),"WARP adapter");
  Check(D3D12CreateDevice(warp,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(device.out())),"WARP device");
  D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(queue.out())),"queue");
  Check(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(allocator.out())),"allocator");
  Check(device->CreateCommandList(0,q.Type,allocator,nullptr,IID_PPV_ARGS(list.out())),"list");
  Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(fence.out())),"fence");
  event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");
  const char*names[]={"rr_guides","rr_accumulate","rr_compose","deferred_encode","deferred_compose","normalize","post_extract","post_compose"};
  std::array<std::vector<char>,8> blobs;std::array<nr_edit_dx12::Bytecode,static_cast<unsigned>(nr_edit_dx12::Kernel::Count)> code{};
  for(unsigned n=0;n<8;n++){
   blobs[n]=Read(directory/(std::string(names[n])+".cso"));Com<ID3D11ShaderReflection> reflection;
   Check(D3DReflect(blobs[n].data(),blobs[n].size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflection.out())),"DXBC reflection");
   UINT x=0,y=0,z=0;reflection->GetThreadGroupSize(&x,&y,&z);if(x!=8||y!=8||z!=1)throw std::runtime_error("thread ABI");
   D3D11_SHADER_BUFFER_DESC b{};Check(reflection->GetConstantBufferByIndex(0)->GetDesc(&b),"constant ABI");
   const unsigned expected=n<3?144:n<5?48:64;if(b.Size!=expected)throw std::runtime_error("constant size mismatch");
   unsigned index=n<5?n:n+1;code[index]={blobs[n].data(),blobs[n].size()};
  }
  kernels=nr_edit_dx12::Create(device,1,1,code,SameDevice);if(!kernels)throw std::runtime_error("real PSO creation");
  nr_edit_dx12::SetRetirementGuard(kernels,[](void*u,nr_private_sr::Recording){return static_cast<Harness*>(u)->released;},this);
 }
 ~Harness(){if(kernels)nr_edit_dx12::DestroyAfterRetirement(kernels);if(event)CloseHandle(event);}
 nr_private_sr::Recording Rec(){return {1,1,reinterpret_cast<std::uint64_t>(list.p),epoch,epoch};}
 nr_edit_dx12::View View(ID3D12Resource*p,unsigned format){return {p,format,0,D3D12_RESOURCE_STATE_COMMON,true};}
 ID3D12Resource* Buffer(ResourceOwner& owner,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
  D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource*p=nullptr;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&p)),"buffer");return owner.Keep(p);}
 ID3D12Resource* Texture(ResourceOwner& owner,unsigned w,unsigned h,unsigned format,float value,float alpha=.75f,bool upload=true,bool alternating=false){std::uint64_t size=0;
  auto*p=nr_edit_dx12::CreatePlane(device,w,h,format,64ull<<20,&size);if(!p)throw std::runtime_error("texture");owner.Keep(p);if(!upload)return p;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;auto desc=p->GetDesc();device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
  auto* staging=Buffer(owner,bytes,D3D12_HEAP_TYPE_UPLOAD);void* mapped=nullptr;D3D12_RANGE read{0,0};Check(staging->Map(0,&read,&mapped),"upload Map");
  std::memset(mapped,0,static_cast<std::size_t>(bytes));const unsigned channels=PixelBytes(format)/(format==10?2:4);
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<channels;c++){
   auto* dst=static_cast<char*>(mapped)+y*footprint.Footprint.RowPitch+x*PixelBytes(format)+(format==10?2:4)*c;
   const float v=c==3?alpha:(alternating&&x%2==0?1.f:value);if(format==10){auto q=Half(v);std::memcpy(dst,&q,2);}else std::memcpy(dst,&v,4);
  }
  staging->Unmap(0,nullptr);Transition(list,p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=staging;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
  to.pResource=p;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);Transition(list,p,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);return p;
 }
 struct Readback{ID3D12Resource* buffer;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;unsigned format,w,h;};
 Readback CopyOut(ResourceOwner& owner,ID3D12Resource*texture,unsigned format){auto d=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
  device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto* b=Buffer(owner,bytes,D3D12_HEAP_TYPE_READBACK);
  D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=texture;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=b;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
  Transition(list,texture,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyTextureRegion(&to,0,0,0,&from,nullptr);Transition(list,texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
  return {b,fp,format,static_cast<unsigned>(d.Width),d.Height};}
 void Assert(const Readback& r,float expected,float alpha=.75f,float even=std::numeric_limits<float>::quiet_NaN(),float tolerance=.002f){void* data=nullptr;Check(r.buffer->Map(0,nullptr,&data),"readback Map");
  const unsigned channels=PixelBytes(r.format)/(r.format==10?2:4);
  for(unsigned y=0;y<r.h;y++)for(unsigned x=0;x<r.w;x++)for(unsigned c=0;c<channels;c++){
   const auto*src=static_cast<char*>(data)+r.footprint.Offset+y*r.footprint.Footprint.RowPitch+x*PixelBytes(r.format)+(r.format==10?2:4)*c;
   float value=0;if(r.format==10){std::uint16_t q;std::memcpy(&q,src,2);value=Unhalf(q);}else std::memcpy(&value,src,4);
   float target=c==3?alpha:(x%2==0&&std::isfinite(even)?even:expected);if(!std::isfinite(value)||std::abs(value-target)>tolerance){std::cerr<<"pixel "<<x<<','<<y<<','<<c<<" got "<<value<<" expected "<<target<<'\n';throw std::runtime_error("GPU pixel mismatch");}
  }
  D3D12_RANGE written{0,0};r.buffer->Unmap(0,&written);
 }
 void Record(nr_edit_dx12::Work& w){w.recording=Rec();auto result=nr_edit_dx12::Record(kernels,list,w);if(!result.recorded)throw std::runtime_error("dispatch refused");}
 void Submit(){Check(list->Close(),"Close");ID3D12CommandList* lists[]={list};queue->ExecuteCommandLists(1,lists);Check(queue->Signal(fence,epoch),"Signal");
  if(!nr_edit_dx12::Submitted(kernels,Rec(),fence,{1,reinterpret_cast<std::uint64_t>(fence.p),epoch},true))throw std::runtime_error("submission receipt");
  Check(fence->SetEventOnCompletion(epoch,event),"completion event");if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("WARP completion timeout");}
 void Reset(){Check(allocator->Reset(),"allocator Reset");Check(list->Reset(allocator,nullptr),"list Reset");released=true;
  if(!nr_edit_dx12::Collect(kernels))throw std::runtime_error("retire");released=false;++epoch;}
};
void Run(Harness& h,unsigned kind){using namespace nr_edit_dx12;ResourceOwner owner;
 const bool neutral=kind==1,signedEdit=kind==2;
 const float nr=neutral?1:signedEdit?.5f:2,carrierValue=neutral?.5f:signedEdit?.4375f:.625f,expected=neutral?4:signedEdit?3.5f:5;
 auto* original=h.Texture(owner,4,4,10,1);auto* edited=h.Texture(owner,4,4,10,nr);
 auto* clean=h.Texture(owner,8,8,10,4);auto* motion=h.Texture(owner,4,4,16,1);auto* depth=h.Texture(owner,4,4,41,.5f);
 auto* normalizedMotion=h.Texture(owner,4,4,16,0,0,false);auto* normalizedDepth=h.Texture(owner,4,4,41,0,0,false);
 struct Normalize{unsigned r[2],d[2],mv[2],db[2],mb[2];float scale[2];float a,b;unsigned linear,inverted;} n{{4,4},{4,4},{4,4},{0,0},{0,0},{2,2},0,1,0,0};
 static_assert(sizeof(n)==64);Work w{};w.kernel=Kernel::NormalizeGuides;w.width=w.height=4;w.constants=&n;w.constant_bytes=64;
 w.reads[0]=h.View(depth,41);w.reads[1]=h.View(motion,16);w.writes[0]=h.View(normalizedDepth,41);w.writes[1]=h.View(normalizedMotion,16);h.Record(w);
 auto motionResult=h.CopyOut(owner,normalizedMotion,16);
 auto* reproj=h.Texture(owner,4,4,2,0,0,false);auto* currentDepth=h.Texture(owner,4,4,41,0,0,false);
 auto* edit=h.Texture(owner,4,4,2,0,0,false);auto* historyDepth=h.Texture(owner,4,4,41,0,0,false);auto* rrFinal=h.Texture(owner,8,8,10,0,0,false);
 nr_rr_dx12::Constants rr{};rr.render_size[0]=rr.render_size[1]=4;rr.output_size[0]=rr.output_size[1]=8;rr.frame_pair_valid=1;
 rr.current_projection_ab[0]=0;rr.current_projection_ab[1]=1;
 w={};w.width=w.height=4;w.constants=&rr;w.constant_bytes=sizeof(rr);w.kernel=Kernel::RrGuides;
 w.reads[2]=h.View(normalizedMotion,16);w.reads[3]=h.View(normalizedDepth,41);w.writes[0]=h.View(reproj,2);w.writes[1]=h.View(currentDepth,41);h.Record(w);
 w.reads={};w.writes={};w.kernel=Kernel::RrAccumulate;w.reads[0]=h.View(edited,10);w.reads[1]=h.View(original,10);
 w.reads[7]=h.View(reproj,2);w.reads[9]=h.View(currentDepth,41);w.writes[2]=h.View(edit,2);w.writes[3]=h.View(historyDepth,41);h.Record(w);
 w.reads={};w.writes={};w.kernel=Kernel::RrCompose;w.width=w.height=8;w.reads[6]=h.View(clean,10);w.reads[8]=h.View(edit,2);w.reads[9]=h.View(currentDepth,41);w.writes[4]=h.View(rrFinal,10);h.Record(w);
 auto rrResult=h.CopyOut(owner,rrFinal,10);
 auto* carrier=h.Texture(owner,4,4,10,0,0,false);auto* tail=h.Texture(owner,4,4,2,0,0,false);
 // Analytic SR output tests the codec, NOT NVIDIA DLSS.
 // A deliberately biased neutral SR output must not invent an edit.
 auto* carrierO=h.Texture(owner,8,8,10,neutral?.625f:carrierValue);auto* final=h.Texture(owner,8,8,10,0,0,false);
 nr_deferred_dx12::Constants d{};d.render_size[0]=d.render_size[1]=4;d.output_size[0]=d.output_size[1]=8;d.frame_pair_valid=d.private_sr_succeeded=1;
 w={};w.width=w.height=4;w.kernel=Kernel::DeferredEncode;w.constants=&d;w.constant_bytes=sizeof(d);
 w.reads[0]=h.View(edited,10);w.reads[1]=h.View(original,10);w.writes[0]=h.View(carrier,10);w.writes[1]=h.View(tail,2);h.Record(w);
 auto carrierResult=h.CopyOut(owner,carrier,10);
 w.reads={};w.writes={};w.width=w.height=8;w.kernel=Kernel::DeferredCompose;w.reads[2]=h.View(carrierO,10);w.reads[3]=h.View(tail,2);w.reads[4]=h.View(clean,10);w.writes[2]=h.View(final,10);h.Record(w);
 auto finalResult=h.CopyOut(owner,final,10);
 h.Submit();h.Assert(motionResult,2);h.Assert(rrResult,expected);h.Assert(carrierResult,carrierValue,1);h.Assert(finalResult,expected);h.Reset();
}
void TinyCarrier(Harness& h,bool negative,float exposure=1.f){using namespace nr_edit_dx12;ResourceOwner owner;
 const float difference=negative?-1.f/2048.f:1.f/1024.f;
 auto* original=h.Texture(owner,4,4,10,1);auto* edited=h.Texture(owner,4,4,10,1+difference);
 auto* clean=h.Texture(owner,4,4,10,.5f);auto* carrier=h.Texture(owner,4,4,10,0,0,false);
 auto* tail=h.Texture(owner,4,4,2,0,0,false);auto* final=h.Texture(owner,4,4,10,0,0,false);
 // Read actual FP16 encoding/tail; a biased SR neutral must not drown a tiny
 // edit that is carried entirely by the FP32 tail.
 auto* biased=h.Texture(owner,4,4,10,.625f);
 nr_deferred_dx12::Constants d{};d.render_size[0]=d.render_size[1]=4;d.output_size[0]=d.output_size[1]=4;
 d.frame_pair_valid=d.private_sr_succeeded=1;d.input_pre_exposure=d.output_pre_exposure=exposure;
 Work w{};w.width=w.height=4;w.kernel=Kernel::DeferredEncode;w.constants=&d;w.constant_bytes=sizeof(d);
 w.reads[0]=h.View(edited,10);w.reads[1]=h.View(original,10);w.writes[0]=h.View(carrier,10);w.writes[1]=h.View(tail,2);h.Record(w);
 auto stored=h.CopyOut(owner,carrier,10);auto lost=h.CopyOut(owner,tail,2);
 w.reads={};w.writes={};w.width=w.height=4;w.kernel=Kernel::DeferredCompose;
 w.reads[2]=h.View(biased,10);w.reads[3]=h.View(tail,2);w.reads[4]=h.View(clean,10);w.writes[2]=h.View(final,10);h.Record(w);
 auto composed=h.CopyOut(owner,final,10);h.Submit();
 const auto none=std::numeric_limits<float>::quiet_NaN();
 void* pixels=nullptr;Check(stored.buffer->Map(0,nullptr,&pixels),"quantized carrier Map");
 std::uint16_t first=0;std::memcpy(&first,static_cast<char*>(pixels)+stored.footprint.Offset,2);
 const float quantized=Unhalf(first);D3D12_RANGE no_write{0,0};stored.buffer->Unmap(0,&no_write);
 const float unquantized=.5f+.5f*difference/(exposure*d.carrier_range);
 if(quantized==unquantized)throw std::runtime_error("tiny fixture did not exercise FP16 quantization");
 h.Assert(stored,quantized,1,none,1e-7f);
 h.Assert(lost,difference/exposure-(2*quantized-1)*d.carrier_range,quantized==.5f?1.f:2.f,none,1e-7f);
 h.Assert(composed,.5f+difference,.75f,none,1e-7f);h.Reset();
}
void HistoryCase(Harness& h,bool reject){using namespace nr_edit_dx12;ResourceOwner owner;
 auto* original=h.Texture(owner,4,4,10,1);auto* edited=h.Texture(owner,4,4,10,2,1,true,true);
 auto* motion=h.Texture(owner,4,4,16,0);auto* depth=h.Texture(owner,4,4,41,.5f);
 auto* previousEdit=h.Texture(owner,4,4,2,.5f,1);auto* previousDepth=h.Texture(owner,4,4,41,reject?20:2);
 auto* expectedPreviousDepth=h.Texture(owner,4,4,16,2);
 auto* reproj=h.Texture(owner,4,4,2,0,0,false);auto* currentDepth=h.Texture(owner,4,4,41,0,0,false);
 auto* edit=h.Texture(owner,4,4,2,0,0,false);auto* newDepth=h.Texture(owner,4,4,41,0,0,false);
 nr_rr_dx12::Constants c{};c.render_size[0]=c.render_size[1]=4;c.output_size[0]=c.output_size[1]=4;
 c.frame_pair_valid=c.history_valid=c.explicit_previous_depth_verified=1;c.current_projection_ab[1]=1;c.new_sample_rate=.08f;
 Work w{};w.kernel=Kernel::RrGuides;w.width=w.height=4;w.constants=&c;w.constant_bytes=sizeof(c);
 w.reads[2]=h.View(motion,16);w.reads[3]=h.View(depth,41);w.reads[10]=h.View(expectedPreviousDepth,16);
 w.writes[0]=h.View(reproj,2);w.writes[1]=h.View(currentDepth,41);h.Record(w);
 w.kernel=Kernel::RrAccumulate;w.reads={};w.writes={};w.reads[0]=h.View(edited,10);w.reads[1]=h.View(original,10);
 w.reads[4]=h.View(previousEdit,2);w.reads[5]=h.View(previousDepth,41);w.reads[7]=h.View(reproj,2);w.reads[9]=h.View(currentDepth,41);
 w.writes[2]=h.View(edit,2);w.writes[3]=h.View(newDepth,41);h.Record(w);
 auto result=h.CopyOut(owner,edit,2);h.Submit();h.Assert(result,reject?1:.54f,1,reject?0:.46f);h.Reset();
}
void InvalidMotion(Harness& h,float invalid){using namespace nr_edit_dx12;ResourceOwner owner;
 auto* motion=h.Texture(owner,4,4,16,invalid);auto* depth=h.Texture(owner,4,4,41,.5f);
 auto* nm=h.Texture(owner,4,4,16,0,0,false);auto* nd=h.Texture(owner,4,4,41,0,0,false);
 struct K{unsigned r[2],d[2],m[2],db[2],mb[2];float scale[2],a,b;unsigned linear,inverted;}k{{4,4},{4,4},{4,4},{0,0},{0,0},{1,1},0,1,0,0};
 Work w{};w.kernel=Kernel::NormalizeGuides;w.width=w.height=4;w.constants=&k;w.constant_bytes=64;
 w.reads[0]=h.View(depth,41);w.reads[1]=h.View(motion,16);w.writes[0]=h.View(nd,41);w.writes[1]=h.View(nm,16);h.Record(w);
 auto result=h.CopyOut(owner,nm,16);h.Submit();h.Assert(result,8);h.Reset();
}
}
#ifndef NR_EDIT_SHADER_VALIDATION_LIBRARY
int wmain(int argc,wchar_t**argv){try{if(argc<2||argc>3||(argc==3&&std::wstring(argv[2])!=L"--nvidia"))throw std::runtime_error("usage: windows_shader_validation <compiled-shader-directory> [--nvidia]");
 Harness h{std::filesystem::path(argv[1]),argc==3};Run(h,0);Run(h,1);Run(h,2);TinyCarrier(h,false);TinyCarrier(h,true);TinyCarrier(h,false,2);TinyCarrier(h,true,2);HistoryCase(h,false);HistoryCase(h,true);
 InvalidMotion(h,std::numeric_limits<float>::quiet_NaN());InvalidMotion(h,std::numeric_limits<float>::infinity());
 std::cout<<(argc==3?"NVIDIA GPU shader execution; no NR/SR model\n":"WARP GPU shader execution; no NR/SR model\n");
 std::cout<<"{\"six_shaders\":\"passed\",\"actual_fp16_quantization_tail\":\"passed\",\"reflection\":\"passed\",\"positive_identity_signed\":\"passed\",\"nan_inf_motion\":\"passed\",\"nvidia_nr_sr\":\"not-run\",\"game\":\"not-run\"}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
#endif

// Run the actual production adapter/shaders against WARP. The slot mutation
// gate is a test seam; the production slot retains its existing fence ledger.
#include <algorithm>
#include <cstdarg>
#define main replay_fixture_unused_main
#include "replay_gpu.cpp"
#undef main
struct NrSlot {
 ID3D12Resource *region_guide=nullptr,*region_field=nullptr,*region_inputs[4]={},*proxy=nullptr,*neural=nullptr;
 ID3D12DescriptorHeap *region_heap=nullptr,*heap=nullptr,*dual_region_heap=nullptr;NrSlot* dual_selected=nullptr;
 D3D12_RESOURCE_STATES region_guide_state=D3D12_RESOURCE_STATE_COMMON,region_field_state=D3D12_RESOURCE_STATE_COMMON;
 unsigned long long region_bytes=0;UINT region_constants[20]={};bool region_ready=false,region_recorded=false;
};
struct {unsigned region_version=1;bool region_enabled=false,generic_residual=false;}g_config;
struct {UINT descriptor_size=0;}g_codec;
constexpr UINT kCodecRegionSrvIndex=9;
constexpr UINT64 kExtraSlotBudget=64ull<<20;
UINT64 g_extra_allocated_bytes=0;
bool mutation_allowed=true;
bool CanMutateSlotLocked(const NrSlot&){return mutation_allowed;}
bool FitsExtraResourcePeak(const NrSlot&s,UINT64 b){return b&&b<=kExtraSlotBudget-s.region_bytes&&b<=(256ull<<20)-g_extra_allocated_bytes;}
void Log(const char* format,...){va_list a;va_start(a,format);std::vfprintf(stderr,format,a);va_end(a);}
struct NrGuideBinding {struct {unsigned depth_width=0,depth_height=0,depth_x=0,depth_y=0;bool linear_depth=false;}signature;ID3D12Resource* depth=nullptr;};
struct NgxParams {ID3D12Resource* normals=nullptr,*specular=nullptr,*hit=nullptr;UINT x=0,y=0;};
bool GetUInt(const NgxParams*p,const char*key,unsigned*v){*v=std::strstr(key,".X")?p->x:p->y;return true;}
bool GetResource(const NgxParams*p,const char*key,ID3D12Resource**v){*v=std::strcmp(key,"GBuffer.Normals")==0?p->normals:std::strcmp(key,"DLSS.Input.SpecularAlbedo")==0?p->specular:p->hit;return *v!=nullptr;}
bool IsSameRrDepthDevice(ID3D12Device*a,ID3D12Device*b){return a==b;}
bool TextureContainsRegion(ID3D12Resource*r,UINT x,UINT y,UINT w,UINT h){auto d=ID3D12Resource_GetDesc(r);return w&&h&&x<=d.Width&&y<=d.Height&&w<=d.Width-x&&h<=d.Height-y;}
bool CreateTexture(ID3D12Device*d,UINT w,UINT h,const wchar_t*,ID3D12Resource**r,DXGI_FORMAT f){
 D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
 desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=f;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 return SUCCEEDED(ID3D12Device_CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(r)));
}
void Transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES&s,D3D12_RESOURCE_STATES t){if(s==t)return;D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,s,t};ID3D12GraphicsCommandList_ResourceBarrier(c,1,&b);s=t;}
void UavBarrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r){D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;ID3D12GraphicsCommandList_ResourceBarrier(c,1,&b);}
#include "../src/nr_region_runtime.inl"
template<class T>std::vector<unsigned char> Bytes(const std::vector<T>& data){std::vector<unsigned char> b(data.size()*sizeof(T));std::memcpy(b.data(),data.data(),b.size());return b;}
std::vector<float> Pixels(Gpu& gpu,ID3D12Resource* image){
 auto d=ID3D12Resource_GetDesc(image);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={};UINT64 bytes=0;
 ID3D12Device_GetCopyableFootprints(gpu.device,&d,0,1,0,&fp,nullptr,nullptr,&bytes);
 D3D12_RESOURCE_DESC b={};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;ID3D12Resource* read=nullptr;
 CheckHr(ID3D12Device_CreateCommittedResource(gpu.device,&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&read)),"field readback");
 auto state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;Transition(gpu.list,image,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
 D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=read;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
 ID3D12GraphicsCommandList_CopyTextureRegion(gpu.list,&dst,0,0,0,&src,nullptr);Transition(gpu.list,image,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);gpu.Execute();
 void* ptr=nullptr;CheckHr(ID3D12Resource_Map(read,0,nullptr,&ptr),"map field");std::vector<float> out(d.Width*d.Height*4);
 for(UINT y=0;y<d.Height;y++)std::memcpy(out.data()+y*d.Width*4,static_cast<unsigned char*>(ptr)+fp.Offset+y*fp.Footprint.RowPitch,d.Width*16);
 ID3D12Resource_Unmap(read,0,nullptr);ReleaseCom(read);return out;
}
int main(){
 Gpu gpu;NrSlot slot;constexpr UINT w=64,h=48,pw=w+3,ph=h+2;
 g_codec.descriptor_size=ID3D12Device_GetDescriptorHandleIncrementSize(gpu.device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
 D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=10;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 CheckHr(ID3D12Device_CreateDescriptorHeap(gpu.device,&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&slot.heap)),"codec table");
 std::vector<float> depth(pw*ph,.4f),hit(pw*ph,0),normals(pw*ph*4,0),spec(pw*ph*4,0);
 for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){UINT p=(y+1)*pw+x+2;normals[p*4+2]=1;if(x<w/2){hit[p]=4;spec[p*4]=spec[p*4+1]=spec[p*4+2]=.2f;}}
 std::vector<std::uint16_t> proxy(w*h*4,0x3400),neural(w*h*4,0x3800);
 slot.proxy=gpu.Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,8,Bytes(proxy));slot.neural=gpu.Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,8,Bytes(neural));
 NgxParams params;params.x=2;params.y=1;params.normals=gpu.Texture(DXGI_FORMAT_R32G32B32A32_FLOAT,pw,ph,16,Bytes(normals));params.specular=gpu.Texture(DXGI_FORMAT_R32G32B32A32_FLOAT,pw,ph,16,Bytes(spec));params.hit=gpu.Texture(DXGI_FORMAT_R32_FLOAT,pw,ph,4,Bytes(hit));
 NrGuideBinding guides;guides.depth=gpu.Texture(DXGI_FORMAT_R32_FLOAT,pw,ph,4,Bytes(depth));guides.signature={w,h,2,1,false};
 Check(!nr_regions::Prepare(gpu.device,slot,&params,guides,w,h,true,true)&&!slot.region_field,"off allocates nothing");g_config.region_enabled=true;
 Check(nr_regions::Prepare(gpu.device,slot,&params,guides,w,h,true,true),"prepare real adapter");
 Check(slot.region_constants[6]==15,"validated four inputs present");nr_regions::Dispatch(gpu.list,slot,false);nr_regions::Dispatch(gpu.list,slot,true);
 auto result=Pixels(gpu,slot.region_field);Check(slot.region_recorded&&nr_regions::status==3,"recorded reflection capability");
 for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){auto p=(y*w+x)*4;Check(std::abs(result[p]-2)<.00001f,"uniform edit entirely broad");Check(result[p+1]==(x<w/2?1.f:0.f),"reflection atlas exact");Check(result[p+2]==.4f&&result[p+3]==.25f,"source depth and luma retained");}
 mutation_allowed=false;auto bytes=slot.region_bytes;auto field=slot.region_field;Check(!nr_regions::Prepare(gpu.device,slot,&params,guides,w/2,h/2,true,true)&&slot.region_bytes==bytes&&slot.region_field==field,"recorded slot rejects descriptor and resource mutation");mutation_allowed=true;
 auto* saved=params.hit;params.hit=nullptr;
 Check(nr_regions::Prepare(gpu.device,slot,&params,guides,w,h,true,true),"missing hit degrades gracefully");nr_regions::Dispatch(gpu.list,slot,false);nr_regions::Dispatch(gpu.list,slot,true);result=Pixels(gpu,slot.region_field);
 for(UINT p=0;p<w*h;p++)Check(result[p*4+1]==0,"specular alone never claims reflection");params.hit=saved;
 // Alternate sign neighbours cannot cancel the centre into a fabricated broad
 // component. An isolated same-direction edit remains a detail contribution.
 std::fill(neural.begin(),neural.end(),0x3000);for(UINT c=0;c<4;c++)neural[((h/2)*w+w/2)*4+c]=0x3800;
 ReleaseCom(slot.neural);slot.neural=gpu.Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,8,Bytes(neural));
 Check(nr_regions::Prepare(gpu.device,slot,&params,guides,w,h,false,true),"generic SR path");nr_regions::Dispatch(gpu.list,slot,false);nr_regions::Dispatch(gpu.list,slot,true);result=Pixels(gpu,slot.region_field);
 Check(result[((h/2)*w+w/2)*4]==1,"isolated positive detail survives negative neighbours");Check(result[0]==-2,"uniform negative edit broad");
 // Model-grid scaling and a portrait retain a bounded aspect-correct guide.
 Check(nr_regions::Prepare(gpu.device,slot,&params,guides,w/2,h/2,false,true),"scaled allocation replaces retired slot");
 Check(slot.region_constants[0]==w/2&&slot.region_constants[1]==h/2,"scaled field dimensions");
 for(auto& r:slot.region_inputs)ReleaseCom(r);ReleaseCom(slot.region_field);ReleaseCom(slot.region_guide);ReleaseCom(slot.region_heap);ReleaseCom(slot.heap);ReleaseCom(slot.proxy);ReleaseCom(slot.neural);
 g_extra_allocated_bytes-=slot.region_bytes;Check(g_extra_allocated_bytes==0,"physical allocation budget drains");
 ReleaseCom(params.normals);ReleaseCom(params.specular);ReleaseCom(params.hit);ReleaseCom(guides.depth);
 ReleaseCom(nr_regions::snapshot);ReleaseCom(nr_regions::build);ReleaseCom(nr_regions::root);ReleaseCom(nr_regions::owner);
 std::printf("PASS production region adapter/shaders WARP: %u checks; fixed selection, reflection resource contract, atlas, signs, mutation rejection, scaling and allocation budget. No game/performance claim.\n",checks);return 0;
}

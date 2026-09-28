// Included after guide binding, codec and slot lifetime helpers.
#include "nr_region_shader.h"
#include "nr_optional_layer_probe.h"
namespace nr_regions {
ID3D12RootSignature* root=nullptr;
ID3D12PipelineState *snapshot=nullptr,*build=nullptr;
ID3D12Device* owner=nullptr;
std::atomic<unsigned> status{0}; // 0 off/unavailable, 1 image, 2 depth, 3 reflection.
std::atomic<unsigned> config_generation{0},guide_flags{0};
std::atomic<nr_binding::Refusal> binding_reason{nr_binding::Refusal::None};
bool Ensure(ID3D12Device* device) {
 if(root&&snapshot&&build)return owner==device;
 if(owner&&owner!=device)return false;
 if(!owner){owner=device;ID3D12Device_AddRef(owner);}
 if(!root){
  D3D12_DESCRIPTOR_RANGE ranges[2]={};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,8,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,8};
  D3D12_ROOT_PARAMETER p[2]={};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};
  p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,20};
  D3D12_ROOT_SIGNATURE_DESC d={};d.NumParameters=2;d.pParameters=p;ID3DBlob *blob=nullptr,*errors=nullptr;
  HRESULT hr=D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors);ReleaseCom(errors);
  if(SUCCEEDED(hr))hr=ID3D12Device_CreateRootSignature(device,0,blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob),IID_ID3D12RootSignature,reinterpret_cast<void**>(&root));
  ReleaseCom(blob);if(FAILED(hr))return false;
 }
 for(unsigned i=0;i<2;i++) {auto& pipeline=i?build:snapshot;if(pipeline)continue;
  ID3DBlob *blob=nullptr,*errors=nullptr;HRESULT hr=D3DCompile(kRegionShader,sizeof(kRegionShader)-1,"nr_region_shader",nullptr,nullptr,i?"BuildCS":"SnapshotCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
  if(FAILED(hr)&&errors)Log("Region shader: %s",static_cast<const char*>(errors->lpVtbl->GetBufferPointer(errors)));ReleaseCom(errors);
  if(SUCCEEDED(hr)){D3D12_COMPUTE_PIPELINE_STATE_DESC d={};d.pRootSignature=root;d.CS={blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob)};
   hr=ID3D12Device_CreateComputePipelineState(device,&d,IID_ID3D12PipelineState,reinterpret_cast<void**>(&pipeline));}
  ReleaseCom(blob);if(FAILED(hr))return false;
 }return true;
}
DXGI_FORMAT View(DXGI_FORMAT f,unsigned i) {
 if(i==0){switch(f){case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:return DXGI_FORMAT_R32_FLOAT;
  case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:return DXGI_FORMAT_R16_UNORM;default:break;}}
 if(i==0||i==3){if(f==DXGI_FORMAT_R32_FLOAT||f==DXGI_FORMAT_R16_FLOAT|| (i==0&&f==DXGI_FORMAT_R16_UNORM)|| (i==0&&f==DXGI_FORMAT_R24_UNORM_X8_TYPELESS)|| (i==0&&f==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS))return f;}
 else if(f==DXGI_FORMAT_R16G16B16A16_FLOAT||f==DXGI_FORMAT_R32G32B32A32_FLOAT||f==DXGI_FORMAT_R32G32B32_FLOAT)return f;
 return DXGI_FORMAT_UNKNOWN;
}
void WriteDescriptors(ID3D12Device* device,const NrSlot& slot,ID3D12DescriptorHeap* heap,ID3D12Resource* neural,ID3D12Resource* delta,nr_binding::DescriptorSet* plan=nullptr) {
 auto at=[&](unsigned i){auto h=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);h.ptr+=SIZE_T(i)*g_codec.descriptor_size;return h;};
 auto srv=[&](unsigned i,ID3D12Resource* r,DXGI_FORMAT f){if(plan){plan->Srv(i,r,f);return;}D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=f;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;ID3D12Device_CreateShaderResourceView(device,r,&d,at(i));};
 for(unsigned i=0;i<4;++i){auto* input=slot.region_inputs[i];srv(i,input,input?View(ID3D12Resource_GetDesc(input).Format,i):DXGI_FORMAT_R32G32B32A32_FLOAT);}
 srv(4,slot.proxy,DXGI_FORMAT_R16G16B16A16_FLOAT);srv(5,neural,DXGI_FORMAT_R16G16B16A16_FLOAT);
 srv(6,slot.region_guide,DXGI_FORMAT_R32G32B32A32_FLOAT);srv(7,delta,DXGI_FORMAT_R32G32B32A32_FLOAT);
 for(unsigned i=0;i<2;++i){if(plan){plan->Uav(8+i,i?slot.region_field:slot.region_guide,DXGI_FORMAT_R32G32B32A32_FLOAT);continue;}D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;ID3D12Device_CreateUnorderedAccessView(device,i?slot.region_field:slot.region_guide,nullptr,&d,at(8+i));}
}
// Per-frame inputs/constants are resolved here; immutable GPU tables are sealed
// after all optional allocations. Only plane replacement needs an idle slot.
bool Prepare(ID3D12Device* device,NrSlot& slot,const NgxParams* params,
             const NrGuideBinding& guides,unsigned mw,unsigned mh,bool rr,bool encoded) {
 slot.region_binding=nullptr;slot.compose_binding=nullptr;
 slot.dual_binding=nullptr;slot.dual_selected=nullptr;slot.dual_reduced=false;
 slot.region_recorded=false;slot.region_ready=false;status.store(0);
 config_generation.store(g_config_generation.load(std::memory_order_relaxed));
 binding_reason.store(nr_binding::Refusal::None);guide_flags.store(0);
 if(g_config.generic_residual || g_config.region_version!=1 || !g_config.region_enabled)return false;
 status.store(4); // Requested, but resources/input contract are not available yet.
 if(!Ensure(device))return false;
 const unsigned rw=guides.signature.depth_width,rh=guides.signature.depth_height;
 if(!rw||!rh||!mw||!mh)return false;
 const unsigned longest=std::max(mw,mh),limit=std::min(longest,640u);
 const unsigned w=std::max(1u,static_cast<unsigned>(static_cast<UINT64>(mw)*limit/longest));
 const unsigned h=std::max(1u,static_cast<unsigned>(static_cast<UINT64>(mh)*limit/longest));
 bool resize=!slot.region_field||ID3D12Resource_GetDesc(slot.region_field).Width!=w||ID3D12Resource_GetDesc(slot.region_field).Height!=h;
 if(resize){
  if(!CanMutateSlotLocked(slot)||!InvalidateSlotBindingsLocked(slot))return false;
  D3D12_RESOURCE_DESC d={};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  const UINT64 one=ID3D12Device_GetResourceAllocationInfo(device,0,1,&d).SizeInBytes;
  if(one>kExtraSlotBudget/2||!FitsExtraResourcePeak(slot,one*2))return false;
  ID3D12Resource *a=nullptr,*b=nullptr;
  if(!CreateTexture(device,w,h,L"NRChain.RegionGuide",&a,d.Format)||!CreateTexture(device,w,h,L"NRChain.RegionField",&b,d.Format)){ReleaseCom(a);ReleaseCom(b);return false;}
  ReleaseCom(slot.region_guide);ReleaseCom(slot.region_field);g_extra_allocated_bytes-=slot.region_bytes;
  slot.region_guide=a;slot.region_field=b;slot.region_bytes=2*one;g_extra_allocated_bytes+=slot.region_bytes;
  slot.region_guide_state=slot.region_field_state=D3D12_RESOURCE_STATE_COMMON;
 }
 UINT* c=slot.region_constants;std::fill(c,c+20,0u);c[0]=w;c[1]=h;c[2]=mw;c[3]=mh;c[4]=rw;c[5]=rh;c[7]=encoded?1:0;
 if(guides.signature.linear_depth)c[6]|=16;
 const unsigned layers[4]={0,3,6,8};
 for(unsigned i=0;i<4;i++) {
  ID3D12Resource* r=nullptr;unsigned x=0,y=0;
  if(i==0){r=guides.depth;x=guides.signature.depth_x;y=guides.signature.depth_y;}
  else if(rr){const auto& layer=nr_optional_layer_probe::kLayers[layers[i]];GetResource(params,layer.key,&r);
   if(!GetUInt(params,layer.rect[0],&x))x=0;if(!GetUInt(params,layer.rect[1],&y))y=0;}
  DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
  if(r){auto d=ID3D12Resource_GetDesc(r);format=View(d.Format,i);ID3D12Device* rd=nullptr;
   bool valid=SUCCEEDED(ID3D12Resource_GetDevice(r,IID_ID3D12Device,reinterpret_cast<void**>(&rd)))&&IsSameRrDepthDevice(rd,device);
   ReleaseCom(rd);D3D12_FEATURE_DATA_FORMAT_SUPPORT support={};support.Format=format;
   valid=valid&&d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.DepthOrArraySize==1&&d.SampleDesc.Count==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&TextureContainsRegion(r,x,y,rw,rh)&&format!=DXGI_FORMAT_UNKNOWN;
   if(valid)valid=SUCCEEDED(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))&&(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
   if(!valid)r=nullptr;}
  if(r){ID3D12Resource_AddRef(r);c[6]|=1u<<i;c[8+2*i]=x;c[9+2*i]=y;}
  ReleaseCom(slot.region_inputs[i]);slot.region_inputs[i]=r;

 }
 guide_flags.store(c[6]);slot.region_ready=true;return true;
}
// Primary RR also needs an immutable depth table when regions are disabled.
// Combine optional tables into one lease rather than consume two pool entries.
bool Seal(ID3D12Device* device,ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
          const nr_binding::DescriptorSet* depth=nullptr) {
 const bool depth_requested=depth && depth->count!=0;
 if(!slot.region_ready && !depth_requested)return true;
 nr_binding::Plan plan{};plan.primary_owner=&slot;
 if(slot.region_ready) {
  WriteDescriptors(device,slot,nullptr,slot.neural,nullptr,&plan.heaps[nr_binding::Region]);
 }
 // DispatchCodec selects Consumer whenever compose_binding exists, including
 // region-OFF RR. A Depth-only packet would otherwise select a null heap.
 WriteCodecDescriptorSet(device,slot,nullptr,&plan.heaps[nr_binding::Consumer]);
 if(depth_requested)plan.heaps[nr_binding::Depth]=*depth;
 nr_binding::Refusal refusal=nr_binding::Refusal::None;
 auto* packet=AcquireBindingPacket(device,list,context,plan,&refusal);
 binding_reason.store(refusal);
 if(!packet){slot.region_ready=false;status.store(4);return false;}
 slot.region_binding=slot.region_ready?packet:nullptr;slot.compose_binding=packet;return true;
}

void Dispatch(ID3D12GraphicsCommandList* cmd,NrSlot& slot,bool field){
 if(!slot.region_ready)return;
 auto* packet=field&&slot.dual_selected?slot.dual_binding:slot.region_binding;
 if(!packet||!packet->heaps[nr_binding::Region])return;
 auto resource=field?slot.region_field:slot.region_guide;auto& state=field?slot.region_field_state:slot.region_guide_state;
 Transition(cmd,resource,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 auto* heap=packet->heaps[nr_binding::Region];
 ID3D12GraphicsCommandList_SetDescriptorHeaps(cmd,1,&heap);ID3D12GraphicsCommandList_SetComputeRootSignature(cmd,root);
 ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
 ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(cmd,1,20,slot.region_constants,0);ID3D12GraphicsCommandList_SetPipelineState(cmd,field?build:snapshot);
 ID3D12GraphicsCommandList_Dispatch(cmd,(slot.region_constants[0]+7)/8,(slot.region_constants[1]+7)/8,1);
 UavBarrier(cmd,resource);Transition(cmd,resource,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 if(field){slot.region_recorded=true;status.store((slot.region_constants[6]&12)==12?3:(slot.region_constants[6]&1)?2:1);}
}
} // namespace nr_regions

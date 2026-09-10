// Included after guide binding, codec and slot lifetime helpers.
#include "nr_region_shader.h"
#include "nr_optional_layer_probe.h"
namespace nr_regions {
ID3D12RootSignature* root=nullptr;
ID3D12PipelineState *snapshot=nullptr,*build=nullptr;
ID3D12Device* owner=nullptr;
std::atomic<unsigned> status{0}; // 0 off/unavailable, 1 image, 2 depth, 3 reflection.
bool Ensure(ID3D12Device* device) {
 if(root&&snapshot&&build)return owner==device;
 if(owner&&owner!=device)return false;
 if(!owner){owner=device;ID3D12Device_AddRef(owner);}
 if(!root){
  D3D12_DESCRIPTOR_RANGE ranges[2]={};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,7,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,7};
  D3D12_ROOT_PARAMETER p[2]={};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};
  p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,16};
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
  case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:return DXGI_FORMAT_R16_UNORM;default:break;}}
 if(i==0||i==3){if(f==DXGI_FORMAT_R32_FLOAT||f==DXGI_FORMAT_R16_FLOAT|| (i==0&&f==DXGI_FORMAT_R16_UNORM)|| (i==0&&f==DXGI_FORMAT_R24_UNORM_X8_TYPELESS))return f;}
 else if(f==DXGI_FORMAT_R16G16B16A16_FLOAT||f==DXGI_FORMAT_R32G32B32A32_FLOAT||f==DXGI_FORMAT_R32G32B32_FLOAT)return f;
 return DXGI_FORMAT_UNKNOWN;
}
// Called only while the existing NrSlot mutation gate still permits descriptor
// changes. Retained inputs are released by the same exact-fence slot retirement.
bool Prepare(ID3D12Device* device,NrSlot& slot,const NgxParams* params,
             const NrGuideBinding& guides,unsigned mw,unsigned mh,bool rr,bool encoded) {
 slot.region_recorded=false;slot.region_ready=false;status.store(0);
 if(g_config.generic_residual || g_config.region_version!=1 || !g_config.region_enabled)return false;
 status.store(4); // Requested, but resources/input contract are not available yet.
 if(!CanMutateSlotLocked(slot) || !Ensure(device))return false;
 const unsigned rw=guides.signature.depth_width,rh=guides.signature.depth_height;
 if(!rw||!rh||!mw||!mh)return false;
 const unsigned longest=std::max(mw,mh),limit=std::min(longest,640u);
 const unsigned w=std::max(1u,static_cast<unsigned>(static_cast<UINT64>(mw)*limit/longest));
 const unsigned h=std::max(1u,static_cast<unsigned>(static_cast<UINT64>(mh)*limit/longest));
 bool resize=!slot.region_field||ID3D12Resource_GetDesc(slot.region_field).Width!=w||ID3D12Resource_GetDesc(slot.region_field).Height!=h;
 if(resize){
  D3D12_RESOURCE_DESC d={};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  const UINT64 one=ID3D12Device_GetResourceAllocationInfo(device,0,1,&d).SizeInBytes;
  if(one>kExtraSlotBudget/2||!FitsExtraResourcePeak(slot,one*2))return false;
  ID3D12Resource *a=nullptr,*b=nullptr;
  if(!CreateTexture(device,w,h,L"NRChain.RegionGuide",&a,d.Format)||!CreateTexture(device,w,h,L"NRChain.RegionField",&b,d.Format)){ReleaseCom(a);ReleaseCom(b);return false;}
  ReleaseCom(slot.region_guide);ReleaseCom(slot.region_field);g_extra_allocated_bytes-=slot.region_bytes;
  slot.region_guide=a;slot.region_field=b;slot.region_bytes=2*one;g_extra_allocated_bytes+=slot.region_bytes;
  slot.region_guide_state=slot.region_field_state=D3D12_RESOURCE_STATE_COMMON;
 }
 if(!slot.region_heap){D3D12_DESCRIPTOR_HEAP_DESC d={};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;d.NumDescriptors=9;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if(FAILED(ID3D12Device_CreateDescriptorHeap(device,&d,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&slot.region_heap))))return false;}
 auto at=[&](unsigned i){auto h=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.region_heap);h.ptr+=SIZE_T(i)*g_codec.descriptor_size;return h;};
 UINT* c=slot.region_constants;std::fill(c,c+16,0u);c[0]=w;c[1]=h;c[2]=mw;c[3]=mh;c[4]=rw;c[5]=rh;c[7]=encoded?1:0;
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
  D3D12_SHADER_RESOURCE_VIEW_DESC d={};d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
  d.Format=r?format:DXGI_FORMAT_R32G32B32A32_FLOAT;ID3D12Device_CreateShaderResourceView(device,r,&d,at(i));
 }
 for(unsigned i=4;i<7;i++){D3D12_SHADER_RESOURCE_VIEW_DESC d={};d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
  d.Format=i==6?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;ID3D12Device_CreateShaderResourceView(device,i==4?slot.proxy:i==5?slot.neural:slot.region_guide,&d,at(i));}
 for(unsigned i=0;i<2;i++){D3D12_UNORDERED_ACCESS_VIEW_DESC d={};d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;ID3D12Device_CreateUnorderedAccessView(device,i?slot.region_field:slot.region_guide,nullptr,&d,at(7+i));}
 // Only the newly introduced descriptor is changed. Existing recorded heaps
 // (including post snapshot/F8/face) are separately reconstructed when needed.
 auto field=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);field.ptr+=SIZE_T(kCodecRegionSrvIndex)*g_codec.descriptor_size;
 D3D12_SHADER_RESOURCE_VIEW_DESC d={};d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
 ID3D12Device_CreateShaderResourceView(device,slot.region_field,&d,field);slot.region_ready=true;return true;
}
void Dispatch(ID3D12GraphicsCommandList* cmd,NrSlot& slot,bool field){
 if(!slot.region_ready)return;
 auto resource=field?slot.region_field:slot.region_guide;auto& state=field?slot.region_field_state:slot.region_guide_state;
 Transition(cmd,resource,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 ID3D12GraphicsCommandList_SetDescriptorHeaps(cmd,1,&slot.region_heap);ID3D12GraphicsCommandList_SetComputeRootSignature(cmd,root);
 ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.region_heap));
 ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(cmd,1,16,slot.region_constants,0);ID3D12GraphicsCommandList_SetPipelineState(cmd,field?build:snapshot);
 ID3D12GraphicsCommandList_Dispatch(cmd,(slot.region_constants[0]+7)/8,(slot.region_constants[1]+7)/8,1);
 UavBarrier(cmd,resource);Transition(cmd,resource,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 if(field){slot.region_recorded=true;status.store((slot.region_constants[6]&12)==12?3:(slot.region_constants[6]&1)?2:1);}
}
} // namespace nr_regions

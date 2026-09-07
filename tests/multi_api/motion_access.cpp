#include <cstdint>
#include <cstdio>
#include <cstdlib>
using DXGI_FORMAT=unsigned;using HRESULT=int;
constexpr DXGI_FORMAT DXGI_FORMAT_UNKNOWN=0;
constexpr unsigned D3D12_RESOURCE_DIMENSION_TEXTURE2D=3,D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE=8;
constexpr unsigned D3D12_FEATURE_FORMAT_SUPPORT=3,D3D12_FORMAT_SUPPORT1_TEXTURE2D=32,D3D12_FORMAT_SUPPORT1_SHADER_LOAD=256;
#define SUCCEEDED(x) ((x)>=0)
struct D3D12_RESOURCE_DESC {unsigned Dimension=3,DepthOrArraySize=1,MipLevels=1;struct {unsigned Count=1;}SampleDesc;unsigned Flags=0;DXGI_FORMAT Format=34;};
struct D3D12_FEATURE_DATA_FORMAT_SUPPORT {DXGI_FORMAT Format=0;unsigned Support1=0;};
struct ID3D12Device {bool fail=false,load=true;unsigned query=0;};
HRESULT ID3D12Device_CheckFeatureSupport(ID3D12Device* d,unsigned,D3D12_FEATURE_DATA_FORMAT_SUPPORT* s,unsigned){
 d->query=s->Format;s->Support1=32u|(d->load?256u:0u);return d->fail?-1:0;
}
#include "src/interop/nr_motion_access_d3d12.inl"
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d\n",__LINE__);std::exit(1);}}while(0)
int main(){
 ID3D12Device device;D3D12_RESOURCE_DESC desc;
 for(unsigned v=0;v<128;++v){
  desc.Format=v;unsigned expected=v==1||v==2?2:v==9||v==10?10:v==15||v==16?16:v==33||v==34?34:0;
  CHECK(NrMotionReadFormat(v)==expected);
  CHECK(NrCanReadMotion(&device,desc)==(expected!=0));
  if(expected)CHECK(device.query==expected);
 }
 desc.Format=33;device.fail=true;CHECK(!NrCanReadMotion(&device,desc));device.fail=false;
 CHECK(NrCanReadMotion(&device,desc));device.load=false;CHECK(!NrCanReadMotion(&device,desc));device.load=true;
 desc.Flags=8;CHECK(!NrCanReadMotion(&device,desc));desc.Flags=0;
 desc.DepthOrArraySize=2;CHECK(!NrCanReadMotion(&device,desc));desc.DepthOrArraySize=1;
 desc.SampleDesc.Count=4;CHECK(!NrCanReadMotion(&device,desc));desc.SampleDesc.Count=1;
 desc.MipLevels=0;CHECK(!NrCanReadMotion(&device,desc));desc.MipLevels=1;
 CHECK(!NrCanReadMotion(nullptr,desc));
 std::printf("%u actual motion-view capability seam checks passed (not GPU)\n",checks);
}

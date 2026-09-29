#ifndef NR_AMD_BUDGET_HEADER
#define NR_AMD_BUDGET_HEADER "../experiments/final_residual/amd_of_budget.hpp"
#endif
#include NR_AMD_BUDGET_HEADER
#ifdef NR_BUDGET_WARP
#include <d3d12.h>
#include <dxgi1_4.h>
#include <initializer_list>
#endif
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 unsigned calls=0;auto allocation=[&](unsigned w,unsigned h,unsigned f)->std::uint64_t{
  ++calls;const unsigned bytes=f==62?1:4;
  return (std::uint64_t(w)*h*bytes+65535)&~std::uint64_t(65535);
 };
 const auto estimate=nr::final_residual::AmdBorrowedCreationAllowance(3840,2160,allocation);
 assert(calls==34);assert(estimate>0&&estimate<100ull*1024*1024);
 const auto own=5*((3840ull*2160*8+65535)&~65535ull);
 assert(own+estimate<768ull*1024*1024);
 assert(own+3840ull*2160*64+16*1024*1024>768ull*1024*1024);
 assert(own+estimate>300ull*1024*1024); // Truly insufficient budget must remain insufficient.
 assert(!nr::final_residual::AmdBorrowedCreationAllowance(0,2160,allocation));
 assert(!nr::final_residual::AmdBorrowedCreationAllowance(4097,2160,allocation));
 assert(!nr::final_residual::AmdBorrowedCreationAllowance(3840,2160,[](unsigned,unsigned,unsigned){return UINT64_MAX;}));
 assert(!nr::final_residual::AmdBorrowedCreationAllowance(3840,2160,[](unsigned,unsigned,unsigned){return 0ull;}));
 assert(!nr::final_residual::AmdBorrowedCreationAllowance(3840,2160,[](unsigned,unsigned,unsigned){return UINT64_MAX-1;}));
 unsigned invalid_calls=0;auto invalid=[&](unsigned,unsigned,unsigned){++invalid_calls;return 65536ull;};
 for(unsigned extent:{0u,127u,4097u,~0u})assert(!nr::final_residual::AmdBorrowedCreationAllowance(extent,2160,invalid));
 assert(invalid_calls==0);
 const auto odd=nr::final_residual::AmdBorrowedCreationAllowance(3839,2159,allocation);assert(odd&&odd<=estimate);
 std::printf("PASS pinned FFX layout budget CPU; 4K allowance=%llu own=%llu total=%llu; no GPU allocation\n",(unsigned long long)estimate,(unsigned long long)own,(unsigned long long)(own+estimate));
#ifdef NR_BUDGET_WARP
 IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
 assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
 assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
 assert(SUCCEEDED(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
 const auto actual=[&](unsigned w,unsigned h,unsigned format){D3D12_RESOURCE_DESC d{};
  d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
  d.Format=DXGI_FORMAT(format);d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  return device->GetResourceAllocationInfo(0,1,&d).SizeInBytes;};
 for(unsigned w:{3840u,3839u}){unsigned h=w==3840?2160:2159;
  const auto bytes=nr::final_residual::AmdBorrowedCreationAllowance(w,h,actual),base=5*actual(w,h,10);
  assert(bytes&&base+bytes<768ull*1024*1024);
  std::printf("PASS WARP real allocation query %ux%u own=%llu allowance=%llu total=%llu\n",w,h,
   (unsigned long long)base,(unsigned long long)bytes,(unsigned long long)(base+bytes));
 }
 device->Release();adapter->Release();factory->Release();
#endif
}

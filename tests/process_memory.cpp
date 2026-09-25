#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <cassert>
#include <cstdio>
#include "../src/nr_process_memory.h"
int main(){
 const auto unknown=nr_process_memory::Read({},false);
 assert(!unknown.attempted&&unknown.local_result==E_PENDING);
 IDXGIFactory4* factory=nullptr;
 assert(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory))));
 IDXGIAdapter1* adapter=nullptr;
 assert(SUCCEEDED(IDXGIFactory4_EnumAdapters1(factory,0,&adapter)));
 DXGI_ADAPTER_DESC1 desc{};assert(SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter,&desc)));
 for(unsigned i=0;i<16;++i){
   const auto sample=nr_process_memory::Read(desc.AdapterLuid,true);
   assert(sample.attempted&&SUCCEEDED(sample.adapter_result));
   assert(SUCCEEDED(sample.local_result)&&SUCCEEDED(sample.nonlocal_result));
   assert(sample.local.Budget>0);
 }
 const auto wrong=nr_process_memory::Read({0xffffffff,0x7fffffff},true);
 assert(wrong.attempted&&FAILED(wrong.adapter_result));
 assert(wrong.local_result==E_PENDING);
 IDXGIAdapter1_Release(adapter);IDXGIFactory4_Release(factory);
 std::puts("PASS real process DXGI budget queries, unknown identity and missing-adapter refusal; no NGX or GPU workload");
}

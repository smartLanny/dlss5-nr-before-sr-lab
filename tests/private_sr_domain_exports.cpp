// Controlled exported factory for whole-Core domain/TLS tests; NOT NVIDIA NGX.
#include <windows.h>
#include <cstdint>
struct Handle {unsigned id;};
static Handle handles[32]{};static unsigned used=0,stats[8]{};
static void (*shutdown_callback)()=nullptr;
extern "C" __declspec(dllexport) void FixtureShutdownCallback(void (*callback)()){shutdown_callback=callback;}
extern "C" __declspec(dllexport) unsigned FixtureCount(unsigned i){return i<8?stats[i]:0;}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_D3D12_AllocateParameters(void** p){++stats[0];*p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,64);return *p?1:0xBAD00005u;}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_D3D12_DestroyParameters(void* p){++stats[1];HeapFree(GetProcessHeap(),0,p);return 1;}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_D3D12_CreateFeature(void*,int feature,void*,void** p){
    if(!p||used==32)return 0xBAD00005u;++stats[2];if(feature==1)++stats[3];if(feature==13)++stats[4];
    handles[used].id=used+1;*p=&handles[used++];return 1;}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_D3D12_EvaluateFeature(void*,void*,void*,void*){++stats[5];return 1;}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_D3D12_ReleaseFeature(void*){++stats[6];return 1;}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_D3D12_Shutdown(){++stats[7];if(shutdown_callback)shutdown_callback();return 1;}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_D3D12_Shutdown1(void*){++stats[7];if(shutdown_callback)shutdown_callback();return 1;}

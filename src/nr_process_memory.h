#pragma once
#include <dxgi1_4.h>

namespace nr_process_memory {
// Cold Create/Release, low-frequency trend and F8 only. Exact adapter/node, process-wide DXGI
// accounting; neither an NR texture sum nor the board's total VRAM allocation.
struct Snapshot {
    bool attempted=false;
    ULONGLONG tick=0;
    HRESULT adapter_result=E_PENDING,local_result=E_PENDING,nonlocal_result=E_PENDING;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
    LUID luid{};UINT node=0;
};
inline Snapshot Read(LUID luid,bool known,UINT node=0,bool include_nonlocal=true) {
    Snapshot out;
    out.tick=GetTickCount64();
    out.luid=luid;out.node=node;
    if(!known)return out;
    out.attempted=true;
    HMODULE module=LoadLibraryExW(L"dxgi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module){out.adapter_result=HRESULT_FROM_WIN32(GetLastError());return out;}
    using Create=HRESULT(WINAPI*)(REFIID,void**);
    auto create=reinterpret_cast<Create>(GetProcAddress(module,"CreateDXGIFactory1"));
    IDXGIFactory4* factory=nullptr;IDXGIAdapter3* adapter=nullptr;
    out.adapter_result=create?create(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)):E_NOINTERFACE;
    if(SUCCEEDED(out.adapter_result)&&factory)
        out.adapter_result=IDXGIFactory4_EnumAdapterByLuid(factory,luid,IID_IDXGIAdapter3,reinterpret_cast<void**>(&adapter));
    if(SUCCEEDED(out.adapter_result)&&adapter) {
        out.local_result=IDXGIAdapter3_QueryVideoMemoryInfo(adapter,node,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&out.local);
        if(include_nonlocal)out.nonlocal_result=IDXGIAdapter3_QueryVideoMemoryInfo(adapter,node,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&out.nonlocal);
    }
    if(adapter)IDXGIAdapter3_Release(adapter);
    if(factory)IDXGIFactory4_Release(factory);
    FreeLibrary(module);
    return out;
}
} // namespace nr_process_memory

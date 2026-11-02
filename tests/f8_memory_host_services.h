#pragma once
// Linux CPU fixture services only. No Windows SDK, GPU, DXGI adapter or NGX
// runtime is loaded. Production headers and function bodies remain unchanged.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <mutex>
#include <shared_mutex>
#include <strings.h>
#include <unistd.h>

#define WINAPI
using UINT = unsigned;
using ULONG = std::uint32_t;
using DWORD = std::uint32_t;
using HRESULT = std::int32_t;
using ULONGLONG = unsigned long long;
using HMODULE = void*;
using HANDLE = void*;
using REFIID = const void*;
inline constexpr HRESULT S_OK=0,E_FAIL=static_cast<HRESULT>(0x80004005u),
    E_PENDING=static_cast<HRESULT>(0x8000000au),E_NOINTERFACE=static_cast<HRESULT>(0x80004002u);
inline constexpr DWORD ERROR_SUCCESS=0,ERROR_FILE_TOO_LARGE=223,ERROR_WRITE_FAULT=29;
#define FAILED(value) ((value)<0)
#define SUCCEEDED(value) ((value)>=0)
#define HRESULT_FROM_WIN32(value) static_cast<HRESULT>(0x80070000u|(value))
#define INVALID_HANDLE_VALUE reinterpret_cast<HANDLE>(std::intptr_t(-1))
struct LUID {DWORD LowPart=0;std::int32_t HighPart=0;};
struct SYSTEMTIME {unsigned short wYear=2026,wMonth=11,wDayOfWeek=0,wDay=3,
    wHour=1,wMinute=2,wSecond=3,wMilliseconds=4;};
inline ULONGLONG GetTickCount64() {
    return static_cast<ULONGLONG>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline DWORD GetCurrentProcessId(){return static_cast<DWORD>(getpid());}
inline int _wcsicmp(const wchar_t* a,const wchar_t* b){return wcscasecmp(a,b);}
inline void SetLastError(DWORD){}
inline bool WriteFile(HANDLE file,const void* bytes,DWORD count,DWORD* written,void*) {
    *written=static_cast<DWORD>(std::fwrite(bytes,1,count,static_cast<std::FILE*>(file)));
    return !std::ferror(static_cast<std::FILE*>(file));
}
struct SRWLOCK {std::shared_mutex lock;};
#define SRWLOCK_INIT {}
inline void AcquireSRWLockExclusive(SRWLOCK* lock){lock->lock.lock();}
inline void ReleaseSRWLockExclusive(SRWLOCK* lock){lock->lock.unlock();}
inline void AcquireSRWLockShared(SRWLOCK* lock){lock->lock.lock_shared();}
inline void ReleaseSRWLockShared(SRWLOCK* lock){lock->lock.unlock_shared();}
inline bool TryAcquireSRWLockExclusive(SRWLOCK* lock){return lock->lock.try_lock();}
struct CRITICAL_SECTION {std::recursive_mutex lock;unsigned RecursionCount=0;};
inline void InitializeCriticalSection(CRITICAL_SECTION*){}
inline void DeleteCriticalSection(CRITICAL_SECTION*){}
inline void EnterCriticalSection(CRITICAL_SECTION* lock){lock->lock.lock();++lock->RecursionCount;}
inline void LeaveCriticalSection(CRITICAL_SECTION* lock){--lock->RecursionCount;lock->lock.unlock();}
inline bool TryEnterCriticalSection(CRITICAL_SECTION* lock) {
    if(!lock->lock.try_lock())return false;
    ++lock->RecursionCount;return true;
}

struct ID3D12Device;
struct ID3D12Resource;
struct D3D12_RESOURCE_DESC {std::uint64_t Width=0;};
enum D3D12_HEAP_TYPE {D3D12_HEAP_TYPE_DEFAULT,D3D12_HEAP_TYPE_READBACK,D3D12_HEAP_TYPE_UPLOAD};
enum D3D12_HEAP_FLAGS {D3D12_HEAP_FLAG_NONE};
struct D3D12_HEAP_PROPERTIES {D3D12_HEAP_TYPE Type{};UINT CreationNodeMask=0;};
struct D3D12_RESOURCE_ALLOCATION_INFO {std::uint64_t SizeInBytes=0,Alignment=0;};
struct ID3D12DeviceVtbl {
    ULONG (*Release)(ID3D12Device*)=nullptr;
    D3D12_RESOURCE_ALLOCATION_INFO* (*GetResourceAllocationInfo)(ID3D12Device*,
        D3D12_RESOURCE_ALLOCATION_INFO*,UINT,UINT,const D3D12_RESOURCE_DESC*)=nullptr;
};
struct ID3D12Device {ID3D12DeviceVtbl* lpVtbl=nullptr;};
struct ID3D12ResourceVtbl {
    HRESULT (*GetDevice)(ID3D12Resource*,REFIID,void**)=nullptr;
    HRESULT (*GetHeapProperties)(ID3D12Resource*,D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS*)=nullptr;
    D3D12_RESOURCE_DESC* (*GetDesc)(ID3D12Resource*,D3D12_RESOURCE_DESC*)=nullptr;
};
struct ID3D12Resource {ID3D12ResourceVtbl* lpVtbl=nullptr;};
inline constexpr REFIID IID_ID3D12Device=nullptr;
inline HRESULT ID3D12Resource_GetDevice(ID3D12Resource* r,REFIID id,void** out){return r->lpVtbl->GetDevice(r,id,out);}
inline HRESULT ID3D12Resource_GetHeapProperties(ID3D12Resource* r,D3D12_HEAP_PROPERTIES* p,D3D12_HEAP_FLAGS* f){return r->lpVtbl->GetHeapProperties(r,p,f);}
inline D3D12_RESOURCE_DESC ID3D12Resource_GetDesc(ID3D12Resource* r){D3D12_RESOURCE_DESC d;return *r->lpVtbl->GetDesc(r,&d);}
inline D3D12_RESOURCE_ALLOCATION_INFO ID3D12Device_GetResourceAllocationInfo(ID3D12Device* d,UINT node,UINT count,const D3D12_RESOURCE_DESC* desc){
    D3D12_RESOURCE_ALLOCATION_INFO out;return *d->lpVtbl->GetResourceAllocationInfo(d,&out,node,count,desc);
}
inline ULONG ID3D12Device_Release(ID3D12Device* d){return d->lpVtbl->Release(d);}
namespace nr_dual::dx12 {
struct Surfaces {ID3D12Resource *input=nullptr,*primary=nullptr,*output=nullptr,
    *low_valid=nullptr,*low_delta=nullptr;bool owns_low_valid=false;};
struct SpatialSlot {Surfaces surfaces;};
inline Surfaces GetSurfaces(SpatialSlot* slot){return slot->surfaces;}
}

// Declarations make the real inline process reader type-check. The fixtures
// provide measured Snapshot inputs and cannot link if they call these APIs.
struct DXGI_QUERY_VIDEO_MEMORY_INFO {std::uint64_t Budget=0,CurrentUsage=0,
    AvailableForReservation=0,CurrentReservation=0;};
struct IDXGIFactory4;
struct IDXGIAdapter3;
inline constexpr REFIID IID_IDXGIFactory4=nullptr,IID_IDXGIAdapter3=nullptr;
inline constexpr unsigned LOAD_LIBRARY_SEARCH_SYSTEM32=0x800;
enum DXGI_MEMORY_SEGMENT_GROUP {DXGI_MEMORY_SEGMENT_GROUP_LOCAL,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL};
HMODULE LoadLibraryExW(const wchar_t*,void*,unsigned);
DWORD GetLastError();
void* GetProcAddress(HMODULE,const char*);
bool FreeLibrary(HMODULE);
HRESULT IDXGIFactory4_EnumAdapterByLuid(IDXGIFactory4*,LUID,REFIID,void**);
HRESULT IDXGIAdapter3_QueryVideoMemoryInfo(IDXGIAdapter3*,UINT,DXGI_MEMORY_SEGMENT_GROUP,DXGI_QUERY_VIDEO_MEMORY_INFO*);
ULONG IDXGIAdapter3_Release(IDXGIAdapter3*);
ULONG IDXGIFactory4_Release(IDXGIFactory4*);

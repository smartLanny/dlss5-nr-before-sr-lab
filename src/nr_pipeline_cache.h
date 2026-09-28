#pragma once
// Only caches our D3D12 compute PSOs. No NGX model/temporal state, game
// resources, command lists, allocators, fences or live GPU handles are stored.
// Hardware/driver/root/shader identity and a bounded checksummed file precede
// CachedPSO. A rejected blob is retried once through the original empty-cache
// Create path. Cache I/O failure must never disable an otherwise valid codec.
#include <windows.h>
#include <bcrypt.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace nr_pipeline_cache {
using Digest=std::array<unsigned char,32>;
constexpr std::uint32_t Version=1;
constexpr std::uint64_t Magic=0x314F5350524E5A5Aull;
constexpr DWORD MaxBlob=16u*1024u*1024u;
constexpr std::uint64_t MaxDirectoryBytes=128ull*1024ull*1024ull;
constexpr unsigned MaxFiles=128;
struct Header {
    std::uint64_t magic=Magic;
    std::uint32_t version=Version,bytes=0;
    Digest key{},payload{};
};
static_assert(sizeof(Header)==80);
struct File {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit File(HANDLE value):h(value){}
    ~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    File(const File&)=delete;File& operator=(const File&)=delete;
    explicit operator bool()const{return h!=INVALID_HANDLE_VALUE;}
};
class Hash {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=false;
public:
    Hash(){ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 &&
        BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;}
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    Hash(const Hash&)=delete;Hash& operator=(const Hash&)=delete;
    bool Add(const void* p,std::size_t n){
        if(!ok||!p||n>std::numeric_limits<ULONG>::max()){ok=false;return false;}
        ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<void*>(p)),static_cast<ULONG>(n),0)>=0;return ok;
    }
    template<class T>bool Add(const T& value){return Add(&value,sizeof(value));}
    bool Finish(Digest& out){return ok&&BCryptFinishHash(hash,out.data(),static_cast<ULONG>(out.size()),0)>=0;}
};
inline bool DigestOf(const void* data,std::size_t size,Digest& out){Hash hash;return hash.Add(data,size)&&hash.Finish(out);}
inline std::wstring Hex(const Digest& digest){
    constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring result;result.reserve(64);
    for(auto b:digest){result.push_back(digits[b>>4]);result.push_back(digits[b&15]);}return result;
}
inline bool OwnedName(const wchar_t* name){
    if(!name||std::wcslen(name)!=68||std::wcscmp(name+64,L".pso")!=0)return false;
    for(unsigned i=0;i<64;++i)if(!((name[i]>=L'0'&&name[i]<=L'9')||(name[i]>=L'a'&&name[i]<=L'f')))return false;
    return true;
}
inline bool Directory(const std::wstring& path){
    if(!CreateDirectoryW(path.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    const DWORD attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
inline std::wstring DefaultDirectory(){
    wchar_t local[MAX_PATH]{};const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
    // Do not put synchronous first-use cache traffic on a UNC/network path,
    // relative directory, game install or a truncated environment value.
    if(!n||n>=MAX_PATH||local[1]!=L':'||local[2]!=L'\\'||n+40>=MAX_PATH)return {};
    std::wstring path=local;
    for(const auto leaf:{L"\\DLSS5Core",L"\\PipelineCache",L"\\v1"}){
        path+=leaf;if(!Directory(path))return {};
    }
    return path;
}
inline bool Read(const std::wstring& path,const Digest& key,std::vector<unsigned char>& data){
    data.clear();
    File file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(!file)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};Header header{};DWORD got=0;
    if(!GetFileInformationByHandle(file.h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
       !GetFileSizeEx(file.h,&size)||size.QuadPart<static_cast<LONGLONG>(sizeof(Header))||
       size.QuadPart>static_cast<LONGLONG>(sizeof(Header)+MaxBlob)||
       !ReadFile(file.h,&header,sizeof(header),&got,nullptr)||got!=sizeof(header)||
       header.magic!=Magic||header.version!=Version||header.key!=key||!header.bytes||header.bytes>MaxBlob||
       size.QuadPart!=static_cast<LONGLONG>(sizeof(Header)+header.bytes))return false;
    data.resize(header.bytes);Digest hash{};
    if(!ReadFile(file.h,data.data(),header.bytes,&got,nullptr)||got!=header.bytes||
       !DigestOf(data.data(),data.size(),hash)||hash!=header.payload){data.clear();return false;}
    return true;
}
inline bool FitsDirectory(const std::wstring& directory,std::uint64_t bytes){
    // Count orphaned .tmp and unexpected regular files as well: an interrupted
    // write must not evade the bound. Include the old destination so the temp
    // plus old file also fits during atomic replacement. No automatic deletion.
    WIN32_FIND_DATAW item{};const HANDLE find=FindFirstFileW((directory+L"/*").c_str(),&item);
    if(find==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
    std::uint64_t total=0;unsigned files=0,scanned=0;bool ok=true;
    do {
        if(++scanned>MaxFiles*2){ok=false;break;}
        if(std::wcscmp(item.cFileName,L".")==0||std::wcscmp(item.cFileName,L"..")==0||
           std::wcscmp(item.cFileName,L"write.lock")==0)continue;
        if(item.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){ok=false;break;}
        const auto size=(static_cast<std::uint64_t>(item.nFileSizeHigh)<<32)|item.nFileSizeLow;
        if(++files>=MaxFiles||size>MaxDirectoryBytes-total){ok=false;break;}total+=size;
    }while(FindNextFileW(find,&item));
    const auto error=GetLastError();FindClose(find);
    return ok&&error==ERROR_NO_MORE_FILES&&bytes<=MaxDirectoryBytes-total;
}
inline bool Write(const std::wstring& directory,const Digest& key,const void* data,std::size_t bytes){
    if(!data||!bytes||bytes>MaxBlob)return false;
    const auto destination=directory+L"\\"+Hex(key)+L".pso";
    // Nonblocking inter-process writer ownership bounds both total storage and
    // publication. Another game preparing PSOs cannot make this one wait.
    File lock(CreateFileW((directory+L"\\write.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!lock)return false;
    BY_HANDLE_FILE_INFORMATION lock_info{};
    if(!GetFileInformationByHandle(lock.h,&lock_info)||(lock_info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||
       !FitsDirectory(directory,bytes+sizeof(Header)))return false;
    const auto attributes=GetFileAttributesW(destination.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    Header header{};header.key=key;header.bytes=static_cast<std::uint32_t>(bytes);
    if(!DigestOf(data,bytes,header.payload))return false;
    static std::atomic<unsigned long long> sequence{0};
    const auto temporary=destination+L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++sequence)+L".tmp";
    bool ok=false;
    {
        File file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(!file)return false;
        DWORD wrote=0;
        ok=WriteFile(file.h,&header,sizeof(header),&wrote,nullptr)&&wrote==sizeof(header)&&
            WriteFile(file.h,data,header.bytes,&wrote,nullptr)&&wrote==header.bytes;
    }
    // Close then atomically publish. Readers see an old complete blob or a new
    // complete blob, never an interrupted write. Only our own temp is deleted.
    if(ok)ok=MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE;
    if(!ok)DeleteFileW(temporary.c_str());
    return ok;
}

struct Identity {
    std::uint32_t vendor=0,device=0,subsystem=0,revision=0,nodes=0;
    std::uint64_t driver=0;
    Digest root{};
};
inline bool PipelineKey(const Identity& id,const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc,Digest& key){
    Hash hash;
    return hash.Add(Version)&&hash.Add(id.vendor)&&hash.Add(id.device)&&hash.Add(id.subsystem)&&
        hash.Add(id.revision)&&hash.Add(id.nodes)&&hash.Add(id.driver)&&hash.Add(id.root)&&
        hash.Add(desc.NodeMask)&&hash.Add(desc.Flags)&&hash.Add(desc.CS.pShaderBytecode,desc.CS.BytecodeLength)&&hash.Finish(key);
}
inline bool RetryWithoutCache(HRESULT hr) noexcept {
    return hr!=E_OUTOFMEMORY && hr!=DXGI_ERROR_DEVICE_REMOVED && hr!=DXGI_ERROR_DEVICE_RESET;
}
struct Result {HRESULT hr=E_FAIL;bool enabled=false,hit=false,rejected=false,written=false;std::size_t bytes=0;};
class Session {
    Identity identity{};
    std::wstring directory;
    bool ready=false;
public:
    void Initialize(ID3D12Device* device,const void* root,std::size_t bytes,bool enabled=true,const std::wstring& explicit_directory={}) noexcept {
        ready=false;
        if(!enabled||!device||!root||!bytes)return;
        try {
            if(!DigestOf(root,bytes,identity.root))return;
            IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;
            if(FAILED(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory))))return;
            const auto luid=ID3D12Device_GetAdapterLuid(device);
            const HRESULT found=IDXGIFactory4_EnumAdapterByLuid(factory,luid,IID_IDXGIAdapter1,reinterpret_cast<void**>(&adapter));
            IDXGIFactory4_Release(factory);if(FAILED(found)||!adapter)return;
            DXGI_ADAPTER_DESC1 desc{};LARGE_INTEGER driver{};
            const bool known=SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter,&desc))&&
                SUCCEEDED(IDXGIAdapter1_CheckInterfaceSupport(adapter,IID_IDXGIDevice,&driver))&&driver.QuadPart>0;
            IDXGIAdapter1_Release(adapter);if(!known)return;
            identity.vendor=desc.VendorId;identity.device=desc.DeviceId;identity.subsystem=desc.SubSysId;
            identity.revision=desc.Revision;identity.nodes=ID3D12Device_GetNodeCount(device);identity.driver=static_cast<std::uint64_t>(driver.QuadPart);
            directory=explicit_directory.empty()?DefaultDirectory():explicit_directory;
            ready=!directory.empty()&&Directory(directory);
        }catch(...){ready=false;}
    }
    Result Create(ID3D12Device* device,const D3D12_COMPUTE_PIPELINE_STATE_DESC& original,ID3D12PipelineState** out) noexcept {
        Result result{};result.enabled=ready;
        if(!device||!out)return result;
        *out=nullptr;
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc=original;
        // This wrapper owns CachedPSO for the codec only. Never change another
        // caller's explicit cached descriptor or a pipeline's other fields.
        if(original.CachedPSO.pCachedBlob||original.CachedPSO.CachedBlobSizeInBytes){
            result.hr=ID3D12Device_CreateComputePipelineState(device,&original,IID_ID3D12PipelineState,reinterpret_cast<void**>(out));return result;
        }
        Digest key{};std::vector<unsigned char> blob;bool loaded=false,keyed=false;
        try {keyed=ready&&PipelineKey(identity,desc,key);loaded=keyed&&Read(directory+L"\\"+Hex(key)+L".pso",key,blob);}catch(...){blob.clear();}
        if(loaded){
            desc.CachedPSO={blob.data(),blob.size()};result.bytes=blob.size();
            result.hr=ID3D12Device_CreateComputePipelineState(device,&desc,IID_ID3D12PipelineState,reinterpret_cast<void**>(out));
            if(SUCCEEDED(result.hr)&&*out){result.hit=true;return result;}
            result.rejected=true;
            if(*out){ID3D12PipelineState_Release(*out);*out=nullptr;}
            // Out-of-memory or a removed device is not a corrupt cache. Avoid
            // a second heavy creation attempt under the same failing device.
            if(!RetryWithoutCache(result.hr))return result;
        }
        desc.CachedPSO={};
        result.hr=ID3D12Device_CreateComputePipelineState(device,&desc,IID_ID3D12PipelineState,reinterpret_cast<void**>(out));
        if(SUCCEEDED(result.hr)&&!*out)result.hr=E_FAIL;
        if(FAILED(result.hr)||!*out||!keyed)return result;
        ID3DBlob* cached=nullptr;
        if(SUCCEEDED(ID3D12PipelineState_GetCachedBlob(*out,&cached))&&cached){
            result.bytes=ID3D10Blob_GetBufferSize(cached);
            try {result.written=Write(directory,key,ID3D10Blob_GetBufferPointer(cached),result.bytes);}catch(...){result.written=false;}
            ID3D10Blob_Release(cached);
        }
        return result;
    }
};
} // namespace nr_pipeline_cache

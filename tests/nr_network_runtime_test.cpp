// Production metadata publication and identity worker with explicit Win32 IO
// observation seams. No real DLL loading, signing check, NGX or GPU execution.
#include "nr_network_policy.h"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <new>
#include <string>
#include <vector>

unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct Module { std::vector<unsigned char> version; std::wstring path; };
struct File { std::vector<unsigned char> bytes{'a','b','c'}; std::size_t cursor=0; long long size_override=0; } disk_file;
using HMODULE=Module*;
using HANDLE=File*;
using DWORD=unsigned long;
struct LARGE_INTEGER { long long QuadPart=0; };
constexpr unsigned GENERIC_READ=1,FILE_SHARE_READ=2,OPEN_EXISTING=3,FILE_ATTRIBUTE_NORMAL=4,FILE_FLAG_SEQUENTIAL_SCAN=8;
const HANDLE INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(~std::uintptr_t(0));
unsigned resource_calls=0,path_calls=0,open_calls=0,read_calls=0,close_calls=0;
bool fail_path=false,fail_open=false,fail_read=false;
std::wstring expected_path;
const wchar_t* MAKEINTRESOURCEW(unsigned value){return reinterpret_cast<const wchar_t*>(std::uintptr_t(value));}
void* FindResourceW(HMODULE module,const wchar_t*,const wchar_t*) {
    ++resource_calls;return module->version.empty()?nullptr:module->version.data();
}
DWORD SizeofResource(HMODULE module,void*){return static_cast<DWORD>(module->version.size());}
void* LoadResource(HMODULE,void* resource){return resource;}
void* LockResource(void* resource){return resource;}
DWORD GetModuleFileNameW(HMODULE module,wchar_t* path,DWORD capacity) {
    ++path_calls;
    if(fail_path)return 0;
    const auto count=module->path.size();
    if(count>=capacity)return capacity;
    std::copy(module->path.begin(),module->path.end(),path);path[count]=0;
    return static_cast<DWORD>(count);
}
void WideToUtf8(const wchar_t* path,char* out,std::size_t capacity) {
    std::size_t i=0;for(;path[i]&&i+1<capacity;++i)out[i]=static_cast<char>(path[i]);out[i]=0;
}
HANDLE CreateFileW(const wchar_t* path,unsigned access,unsigned share,void*,unsigned disposition,unsigned flags,void*) {
    ++open_calls;
    Check(expected_path==path,"worker measures the actual loaded module path");
    Check(access==GENERIC_READ&&share==FILE_SHARE_READ&&disposition==OPEN_EXISTING&&
        flags==(FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN),"identity measurement is bounded read-only access");
    if(fail_open)return INVALID_HANDLE_VALUE;
    disk_file.cursor=0;return &disk_file;
}
bool GetFileSizeEx(HANDLE file,LARGE_INTEGER* size){size->QuadPart=file->size_override?file->size_override:static_cast<long long>(file->bytes.size());return true;}
bool ReadFile(HANDLE file,unsigned char* output,DWORD requested,DWORD* received,void*) {
    ++read_calls;if(fail_read)return false;
    const auto count=std::min<std::size_t>(requested,file->bytes.size()-file->cursor);
    std::copy_n(file->bytes.data()+file->cursor,count,output);file->cursor+=count;*received=static_cast<DWORD>(count);return true;
}
void CloseHandle(HANDLE){++close_calls;}
void Log(const char*,...) {}
enum class BypassReason { None,NetworkInvalidRequest,NetworkUnavailable };
BypassReason bypass=BypassReason::None;
void SetBypassReason(BypassReason reason){bypass=reason;}
struct Config { unsigned nr_layers=1; int global=0; int overrides[5]{-1,-1,-1,-1,-1}; } g_config;
std::atomic<unsigned> g_config_generation{1};
namespace nr_layers {
unsigned Active(unsigned count){return count;}
int RequestedNetwork(const Config& c,unsigned layer){return nr_network_policy::Resolve(c.global,c.overrides[layer]);}
}
namespace nr_bundle {
#include "network-sha.inl"
}
#include "nr_network_runtime.inl"
#include "nr_network_identity_worker.inl"

std::vector<unsigned char> VersionBytes(unsigned major,unsigned minor,unsigned build,unsigned revision) {
    std::vector<unsigned char> bytes(92);
    const auto u16=[&](std::size_t offset,unsigned value){bytes[offset]=static_cast<unsigned char>(value);bytes[offset+1]=static_cast<unsigned char>(value>>8);};
    const auto u32=[&](std::size_t offset,unsigned value){u16(offset,value);u16(offset+2,value>>16);};
    u16(0,92);u16(2,52);u16(4,0);
    const char key[]="VS_VERSION_INFO";
    for(std::size_t i=0;i<sizeof(key);++i)u16(6+i*2,key[i]);
    u32(40,0xfeef04bd);u32(44,0x00010000);u32(48,(major<<16)|minor);u32(52,(build<<16)|revision);
    return bytes;
}
void KnownReceipt(const char* hash) {
    const auto identity=CurrentNrNetworkCreationIdentity();
    auto* receipt=new NrNetworkFileIdentity;
    receipt->generation=identity.generation;receipt->module_address=identity.module_address;
    std::strcpy(receipt->sha256,hash);receipt->status="test-source-receipt";
    g_nr_network_file_identity.store(receipt,std::memory_order_release);
}
int main(){
    using namespace nr_network_policy;
    Check(!CurrentNrNetworkCreationIdentity().generation,"no creation identity before runtime load");
    Check(std::strcmp(EnsureNrNetworkIdentityOnWorker().status,"pending")==0&&!open_calls,
        "early worker does not consume a once flag or open a candidate DLL");
    Module modern{VersionBytes(310,9,16,0),L"C:/actual/game/nvngx_dlssnr.dll"};
    expected_path=modern.path;
    Check(RecordNrNetworkRuntimeLoaded(&modern),"loaded module metadata published");
    const auto first=CurrentNrNetworkCreationIdentity();
    Check(first.generation==1&&first.module_address==reinterpret_cast<std::uintptr_t>(&modern),"creation identity describes loaded owner");
    Check(!open_calls&&!path_calls,"first load metadata never hashes or reads a DLL file");
    auto snapshot=NrNetworkRuntimeSnapshot();
    Check(snapshot.version.known&&snapshot.family==Family::OriginalAndOptimized&&snapshot.evidence==Evidence::LoadedVersionHint,
        "mapped version is reported only as a hint");
    const auto resources=resource_calls;
    Check(RecordNrNetworkRuntimeLoaded(&modern)&&resource_calls==resources,"same owner fast path does not scan resources");
    const auto& file=EnsureNrNetworkIdentityOnWorker();
    Check(std::strcmp(file.sha256,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")==0,
        "existing production SHA256 hashes observed file bytes");
    Check(file.file_bytes==3&&open_calls==1&&read_calls==1&&close_calls==1,"one bounded worker read closes its handle");
    Check(!std::strcmp(file.path_utf8,"C:/actual/game/nvngx_dlssnr.dll"),"actual loaded path is preserved for F8");
    Check(&EnsureNrNetworkIdentityOnWorker()==&file&&open_calls==1,"worker cache is immutable for the loaded generation");
    Check(CurrentNrNetworkCreationIdentity().generation==first.generation,"learning a file hash does not spuriously recreate a model");
    Check(NrNetworkRuntimeSnapshot().family==Family::Unknown,"unrecognized file does not inherit a reassuring version label");
    KnownReceipt("2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f");
    Check(NrNetworkRuntimeSnapshot().evidence==Evidence::LoadedPathFileHash,"known matching file promotes evidence level");
    Check(NrNetworkRequestedStatus(0).expected==2&&!NrNetworkRequestedStatus(2).unavailable,"new default inference remains separate from execution");

    Module legacy{VersionBytes(310,8,0,0),L"C:/other/nvngx_dlssnr.dll"};
    expected_path=legacy.path;Check(RecordNrNetworkRuntimeLoaded(&legacy),"different owner metadata published");
    Check(CurrentNrNetworkCreationIdentity().generation==2&&!NrNetworkRuntimeSnapshot().sha256[0],"new owner cannot reuse previous path hash");
    Check(NrNetworkRequestedStatus(2).possible_fallback&&!NrNetworkRequestedStatus(2).unavailable,"version hint alone never rejects a compatible runtime");
    KnownReceipt("2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f");
    Check(NrNetworkRuntimeSnapshot().family==Family::Unknown&&
        !std::strcmp(NrNetworkRuntimeSnapshot().identity_status,"loaded-version-file-hash-conflict"),
        "replacement-file hash conflicting with loaded metadata cannot claim availability");
    KnownReceipt("8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206");
    g_config.global=2;
    Check(!ValidateNrActiveNetworkRequests()&&bypass==BypassReason::NetworkUnavailable,"known old runtime refuses silent optimized fallback");
    Check(g_config.global==2,"unavailable saved request is retained");
    g_config.global=71;++g_config_generation;
    Check(!ValidateNrActiveNetworkRequests()&&bypass==BypassReason::NetworkInvalidRequest,"unknown saved integer is refused");
    g_config.overrides[0]=1;
    Check(ValidateNrActiveNetworkRequests(),"explicit valid layer is independent of unused global request");
    g_config.nr_layers=2;g_config.overrides[1]=2;
    Check(!ValidateNrActiveNetworkRequests()&&bypass==BypassReason::NetworkUnavailable,"active auxiliary network is validated independently");
    g_config.nr_layers=1;
    Check(ValidateNrActiveNetworkRequests(),"inactive saved auxiliary is retained without blocking primary");

    Module unknown{{},L"C:/custom/nvngx_dlssnr.dll"};expected_path=unknown.path;
    RecordNrNetworkRuntimeLoaded(&unknown);fail_read=true;
    const auto& failed=EnsureNrNetworkIdentityOnWorker();
    Check(!failed.sha256[0]&&!std::strcmp(failed.status,"hash-unavailable"),"IO failure remains unknown without a zero hash");
    const auto attempts=open_calls;fail_read=false;
    Check(&EnsureNrNetworkIdentityOnWorker()==&failed&&open_calls==attempts,"failed measurement is cached instead of re-reading each frame");
    Check(!g_nr_network_identity_measuring.load(),"worker releases its measurement guard on failure");
    Check(NrNetworkRequestedStatus(2).expected==Unknown&&!NrNetworkRequestedStatus(2).unavailable,"unknown compatible runtime remains admissible");
    std::printf("PASS %u production loaded-metadata/worker/cache/admission checks; Win32 IO observation stubs only\n",checks);
}

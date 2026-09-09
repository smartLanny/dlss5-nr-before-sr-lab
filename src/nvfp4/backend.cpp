#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#include "backend.h"
#include "nvapi_abi.h"
#include "recording_lifetime.h"
#include "asset_hashes.h"
#include "policy_registry.h"
#include "../../kernels/nvfp4/nvfp4_kernel_abi.h"
#include "../../kernels/nvfp4/nvfp4_fused_abi.h"
#include "../../kernels/nvfp4_expansion/expansion_abi.h"

namespace nr::nvfp4 {
namespace {
using Microsoft::WRL::ComPtr;
using Blob = std::vector<uint8_t>;
namespace fs = std::filesystem;
using Key = std::pair<uint64_t, uint64_t>;
constexpr uint64_t MaximumBudget = 256ull << 20;
constexpr uint64_t MaximumM = 3840;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 operation failed: " + std::to_string(uint32_t(hr))); }
void NvCheck(api::Status rc) { if (rc != api::Ok) throw std::runtime_error("NVAPI operation failed: " + std::to_string(rc)); }
template<class T> T Symbol(HMODULE m, const char* name) {
    auto p = GetProcAddress(m, name);
    if (!p) throw std::runtime_error(std::string("Missing export: ") + name);
    return reinterpret_cast<T>(p);
}
void Reason(Snapshot& s, State state, const char* text) {
    s.state = state;
    std::snprintf(s.reason, sizeof(s.reason), "%s", text ? text : "");
}
bool Hash(const void* data, uint32_t bytes, uint8_t out[32]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto rc = BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    if(rc>=0) rc=BCryptHashData(hash,static_cast<PUCHAR>(const_cast<void*>(data)),bytes,0);
    if(rc>=0) rc=BCryptFinishHash(hash,out,32,0);
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return rc >= 0;
}
bool OriginalModule(const void* data, uint32_t bytes, uint8_t hash[32]) {
    static constexpr uint8_t expected[32] = {0x3f,0xa6,0xf0,0x76,0xee,0xcf,0xbb,0x6e,0x19,0xc3,0x78,0xf8,0x4b,0xbb,0x70,0x25,0xcd,0x80,0x5e,0xd4,0xde,0x9a,0x2f,0x9c,0x9b,0x99,0x5e,0xb3,0x4a,0x7f,0x3d,0x76};
    return data && bytes == 3202680 && Hash(data, bytes, hash) && !std::memcmp(hash, expected, 32);
}
Blob Read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Missing locked NVFP4 asset");
    auto size = f.tellg();
    if (size <= 0 || size > 128 * 1024 * 1024) throw std::runtime_error("Invalid asset length");
    Blob b(static_cast<size_t>(size)); f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(b.data()), b.size())) throw std::runtime_error("Asset read failed");
    return b;
}
struct Budget {
    uint64_t used = 0, limit = MaximumBudget;
    std::mutex mutex;
    bool Reserve(uint64_t bytes) { std::lock_guard lock(mutex); if (bytes > limit - used) return false; used += bytes; return true; }
    void Release(uint64_t bytes) { std::lock_guard lock(mutex); used -= bytes; }
    uint64_t Used() { std::lock_guard lock(mutex); return used; }
};
struct Buffer {
    ComPtr<ID3D12Resource> gpu;
    std::shared_ptr<Budget> budget;
    uint64_t bytes = 0, charge = 0;
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& x) noexcept : gpu(std::move(x.gpu)), budget(std::move(x.budget)), bytes(x.bytes), charge(x.charge) { x.charge = 0; }
    Buffer& operator=(Buffer&& x) noexcept { if (this != &x) { Reset(); gpu = std::move(x.gpu); budget = std::move(x.budget); bytes = x.bytes; charge = x.charge; x.charge = 0; } return *this; }
    ~Buffer() { Reset(); }
    void Reset() { gpu.Reset(); if (charge && budget) budget->Release(charge); charge = 0; }
    uint64_t Address() const { return gpu ? gpu->GetGPUVirtualAddress() : 0; }
};
Buffer Allocate(ID3D12Device* device, std::shared_ptr<Budget> budget, uint64_t bytes,
                D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_DEFAULT) {
    bytes = std::max<uint64_t>(bytes, 256);
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (type == D3D12_HEAP_TYPE_DEFAULT) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto allocation = device->GetResourceAllocationInfo(0, 1, &desc);
    if (!allocation.SizeInBytes || allocation.SizeInBytes == UINT64_MAX || !budget->Reserve(allocation.SizeInBytes))
        throw std::runtime_error("256 MiB device pool budget exhausted");
    Buffer result; result.budget = std::move(budget); result.charge = allocation.SizeInBytes; result.bytes = bytes;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        type == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&result.gpu)));
    return result;
}
void Transition(ID3D12GraphicsCommandList* c, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to}; c->ResourceBarrier(1, &b);
}
struct Patch { size_t offset; uint32_t binding; uint64_t add; };
struct Contract {
    Blob params, cubin; std::string name;
    api::Dim3 grid{}, block{}; uint32_t shared = 0;
    uint64_t workspace_bytes = 0, reset_bytes = UINT64_MAX;
    std::vector<Patch> patches;
    api::Handle function = nullptr;
};
struct Weight { uint64_t ep, es, cp, cs, cosine; };
struct FusedInput { int m,n,k,sm_count; uint64_t a,b,sfa,sfb,c,d,sfd,norm,workspace; };
struct FusedInfo { uint64_t params_bytes,workspace_bytes,shared_bytes,sfa_bytes,sfb_bytes,sfd_bytes; uint32_t grid[3],block[3]; };
static_assert(sizeof(FusedInput) == 88 && sizeof(FusedInfo) == 72);
struct Cuda {
    HMODULE driver = nullptr, helper = nullptr;
    void* context = nullptr; int ordinal = -1, sm = 0, major = 0, minor = 0, max_shared = 0;
    int (__stdcall* push)(void*) = nullptr;
    int (__stdcall* pop)(void**) = nullptr;
    int (__stdcall* release)(int) = nullptr;
    int (__cdecl* build)(const FusedInput*, void*, uint64_t, FusedInfo*) = nullptr;
    ~Cuda() { if (context && release) release(ordinal); if (helper) FreeLibrary(helper); if (driver) FreeLibrary(driver); }
    void Init(ID3D12Device* device, const fs::path& assets) {
        driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!driver) throw std::runtime_error("CUDA driver unavailable");
        auto init = Symbol<int (__stdcall*)(unsigned)>(driver, "cuInit");
        auto count = Symbol<int (__stdcall*)(int*)>(driver, "cuDeviceGetCount");
        auto get = Symbol<int (__stdcall*)(int*, int)>(driver, "cuDeviceGet");
        auto luid = Symbol<int (__stdcall*)(char*, unsigned*, int)>(driver, "cuDeviceGetLuid");
        auto attr = Symbol<int (__stdcall*)(int*, int, int)>(driver, "cuDeviceGetAttribute");
        auto retain = Symbol<int (__stdcall*)(void**, int)>(driver, "cuDevicePrimaryCtxRetain");
        release = Symbol<decltype(release)>(driver, "cuDevicePrimaryCtxRelease_v2");
        push = Symbol<decltype(push)>(driver, "cuCtxPushCurrent_v2"); pop = Symbol<decltype(pop)>(driver, "cuCtxPopCurrent_v2");
        int n = 0; if (init(0) || count(&n)) throw std::runtime_error("CUDA device enumeration failed");
        LUID wanted = device->GetAdapterLuid();
        for (int i = 0; i < n; ++i) { int d = -1; char got[8]{}; unsigned nodes = 0;
            if (!get(&d, i) && !luid(got, &nodes, d) && !std::memcmp(got, &wanted, 8) && nodes) { ordinal = d; break; }
        }
        if (ordinal < 0) throw std::runtime_error("DXGI LUID has no matching CUDA device");
        if (attr(&sm, 16, ordinal) || attr(&major, 75, ordinal) || attr(&minor, 76, ordinal) ||
            attr(&max_shared, 97, ordinal) || sm <= 0 || max_shared <= 0)
            throw std::runtime_error("CUDA capability query failed");
        if (major != 12 || minor != 0) throw std::runtime_error("Locked reference requires actual SM120 capability");
        if (retain(&context, ordinal)) throw std::runtime_error("Matched CUDA primary context unavailable");
        helper = LoadLibraryExW((assets / L"expansion" / L"params_builder.dll").c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!helper) throw std::runtime_error("Locked expansion builder unavailable");
        build = Symbol<decltype(build)>(helper, "fused_params");
    }
    void Encode(const FusedInput& input, Blob& params, FusedInfo& info) {
        if (build(&input, nullptr, 0, &info) || info.params_bytes != 2176 || info.workspace_bytes)
            throw std::runtime_error("Expansion Params/workspace ABI mismatch");
        params.resize(info.params_bytes);
        if (push(context)) throw std::runtime_error("Matched CUDA context push failed");
        int rc = build(&input, params.data(), params.size(), &info); void* previous = nullptr; int popped = pop(&previous);
        if (rc || popped || !info.grid[0] || !info.grid[1] || !info.grid[2] || !info.block[0] || !info.block[1] || !info.block[2])
            throw std::runtime_error("Expansion descriptor encoding failed");
    }
};
struct Upload {
    ComPtr<ID3D12CommandQueue> queue; ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list; ComPtr<ID3D12Fence> fence;
    std::vector<Buffer> sources;
    std::atomic<bool> submitted{false};
    explicit Upload(ID3D12Device* d) {
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(d->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
        Check(d->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocator)));
        Check(d->CreateCommandList(0, q.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
        Check(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    }
    void Copy(Buffer& destination, Buffer& source, uint64_t bytes) {
        Transition(list.Get(), destination.gpu.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        for (uint64_t offset = 0; offset < bytes;) { uint64_t size = std::min(source.bytes, bytes - offset);
            list->CopyBufferRegion(destination.gpu.Get(), offset, source.gpu.Get(), 0, size); offset += size;
        }
        Transition(list.Get(), destination.gpu.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    void Submit() { Check(list->Close()); ID3D12CommandList* lists[] = {list.Get()}; queue->ExecuteCommandLists(1, lists); submitted = true; Check(queue->Signal(fence.Get(), 1)); }
    bool Complete() const { auto value = fence->GetCompletedValue(); return value != UINT64_MAX && value >= 1; }
};
struct Arena {
    uint64_t m = 0;
    Buffer xp,xs,yp,ys,c,workspace;
    std::array<Blob,8> expand_params, contract_params;
    std::array<FusedInfo,8> expand_info{};
    struct AlignedParams {
        Blob bytes; size_t offset=0;
        void Resize(size_t size,size_t alignment) { bytes.resize(size+alignment-1); offset=(alignment-(reinterpret_cast<uintptr_t>(bytes.data())%alignment))%alignment; }
        void* Data() { return bytes.data()+offset; }
    };
    std::array<AlignedParams,8> optimized_params;
    std::array<AlignedParams,8> packed_expansion_params;
    NrExpansionInfo packed_expansion_info{};
    NrNvfp4ContractInfo optimized_info{};
    NrNvfp4FusedInfo fused_info{};
    bool optimized=false, fused_publish=false, packed_expansion=false;
    std::shared_ptr<Upload> upload;
    detail::RecordingLifetime lifetime;
    std::vector<ComPtr<ID3D12CommandQueue>> queues;
    std::vector<ComPtr<ID3D12Fence>> fences;
    uint64_t context = 0, generation = 0;
    bool ready = false, leased = false;
    std::atomic<bool> poisoned{false}, has_recording{false};
};
struct ExpansionSettings {
    fs::path directory;
    std::string cubin_sha,builder_sha,build_id,kernel_id;
    uint32_t variant=0;
};
struct OptimizedSettings {
    fs::path directory;
    std::string cubin_sha,builder_sha,build_id,kernel_id;
    uint32_t variant=0;
    std::vector<uint32_t> tested_m;
    bool allow_auto=false, fused_publish=false, qualified_by_registry=false;
    ExpansionSettings expansion;
    bool Contains(uint64_t m) const { return std::find(tested_m.begin(),tested_m.end(),m)!=tested_m.end(); }
};
struct LockedFile {
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit LockedFile(const fs::path& path) {
        handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot hold an immutable read lock on an NVFP4 asset");
    }
    ~LockedFile() { if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
struct Device {
    ComPtr<ID3D12Device> owner; std::shared_ptr<Budget> budget = std::make_shared<Budget>();
    Cuda cuda; Buffer weights,norm;
    std::array<Weight,8> weights_offsets{};
    std::map<uint64_t,Contract> contracts;
    std::vector<api::Handle> modules, functions;
    api::Handle pack = nullptr, epilogue = nullptr, expand = nullptr;
    std::shared_ptr<Upload> upload;
    std::vector<std::shared_ptr<Arena>> arenas;
    bool preparing = false, ready = false;
    std::string failure;
    OptimizedSettings optimized_settings;
    HMODULE optimized_helper=nullptr;
    NrNvfp4BuildContractFn build_contract=nullptr;
    NrNvfp4BuildFusedFn build_fused=nullptr;
    api::Handle optimized_contract=nullptr;
    HMODULE packed_expansion_helper=nullptr;
    NrBuildPackExpansionFn build_packed_expansion=nullptr;
    api::Handle packed_expansion_function=nullptr;
    std::atomic<bool> optimized_ready{false};
    bool runtime_approved=false;
    fs::path runtime_path;
    std::vector<std::unique_ptr<LockedFile>> asset_locks;
    uint32_t pending_jobs=0;
    bool ever_owned=false;
};
enum class Kind { Publish, Expand, Contract };
using IdentityLive = std::shared_ptr<std::atomic<bool>>;
struct ModuleIdentity {
    ID3D12Device* device; bool supported; IdentityLive live;
    std::array<uint8_t,32> hash{}; bool hash_valid = false;
};
struct Target { ID3D12Device* device; api::Handle module; Kind kind; bool supported; const char* name; IdentityLive live, module_live; };
struct OriginalIdentity {
    ID3D12Device* device; api::Handle module; std::string name;
    IdentityLive live, module_live;
};
struct Metadata {
    std::map<api::Handle,ModuleIdentity> modules;
    std::map<api::Handle,Target> functions;
    // This optional diagnostic map never participates in replacement admission.
    std::map<api::Handle,OriginalIdentity> original_functions;
};
struct Context {
    std::mutex mutex; std::atomic<bool> busy{false}; bool retired = false;
    uint64_t m = 0; Snapshot status; std::shared_ptr<Device> device;
};
struct ScopeData {
    OwnedCall call; std::shared_ptr<Context> context; std::shared_ptr<Device> device;
    std::shared_ptr<const Metadata> metadata; std::shared_ptr<Arena> arena;
    Snapshot status;
    bool eligible = false, active = false, pending = false, failure = false, owns_busy = false;
    bool recorded = false, observed_publish = false;
    uint64_t m = 0, x = 0, y = 0, done = 0;
    uint32_t rows = 0, columns = 0, pair = 0;
    uint32_t original_pair = 0;
    void (*observe_stage)(Stage,bool,uint32_t,ID3D12GraphicsCommandList*,void*) = nullptr;
    void (*observe_launch)(const char*,const void*,uint32_t,const uint32_t*,const uint32_t*,uint32_t,void*) = nullptr;
    void* observer_user = nullptr;
    void (*observe_module)(const void*,uint32_t,const uint8_t*,void*) = nullptr;
    OriginalLaunchObserver observe_original_launch = nullptr;
    bool observe_original_input_calls = false;
    uint64_t original_sequence = 0;
};
static_assert(sizeof(ScopeData)<=768 && alignof(ScopeData)<=alignof(std::max_align_t));
thread_local ScopeData* current_scope = nullptr;
struct Job { std::shared_ptr<Device> device; uint64_t m; bool optimized=false; };
struct Global {
    std::mutex mutex, metadata_mutex, hook_mutex;
    std::condition_variable work;
    Config config; fs::path assets;
    fs::path runtime_path;
    OptimizedSettings optimized;
    std::atomic<Mode> mode{Mode::Disabled};
    std::atomic<bool> observer_enabled{false};
    std::atomic<bool> original_observer_enabled{false};
    std::map<ID3D12Device*,std::shared_ptr<Device>> devices;
    std::map<Key,std::shared_ptr<Context>> contexts;
    std::deque<Job> jobs;
    std::deque<std::shared_ptr<Device>> retire_queue;
    std::vector<std::shared_ptr<Device>> failed_destroy_devices;
    std::set<Key> retired_contexts;
    std::deque<Key> retirement_order;
    std::shared_ptr<const Metadata> metadata = std::make_shared<Metadata>();
    std::atomic<api::CreateModule> create_module{nullptr};
    std::atomic<api::CreateFunction> create_function{nullptr};
    std::atomic<api::Launch> launch{nullptr};
    std::atomic<api::Destroy> destroy_module{nullptr}, destroy_function{nullptr};
    api::Query query = nullptr; HMODULE nvapi = nullptr;
    void* query_target = nullptr; bool worker_started = false, hook_installed = false;
    Snapshot status;
};
// Process-lifetime owner is intentional only for unresolved GPU dependencies.
// Individual arenas are recycled after both actual completion and Reset.
Global& G() { static Global* instance = new Global; return *instance; }
bool TemplateM(uint64_t m) { return m == 960 || m == 2160 || m == 3840; }
void RequestLocked(const std::shared_ptr<Device>& d, uint64_t m, bool optimized=false) {
    auto& g = G();
    if (!d->failure.empty()) return;
    for (const auto& j : g.jobs) if (j.device == d && j.m == m && j.optimized==optimized) return;
    if (!m && (d->preparing || d->ready)) return;
    if (!m) d->preparing = true;
    g.jobs.push_back({d,m,optimized}); ++d->pending_jobs; g.work.notify_one();
}
bool SelectOptimized(const Device& d,Mode mode,uint64_t m) {
    return d.optimized_ready && d.optimized_settings.Contains(m) &&
        (mode==Mode::Optimized || (mode==Mode::Auto && d.optimized_settings.allow_auto && d.optimized_settings.qualified_by_registry));
}
bool AllowedM(const Device& d,Mode mode,uint64_t m) {
    return SelectOptimized(d,mode,m) || (mode==Mode::UpstreamReference && TemplateM(m));
}

#include "prepare.inl"

void LostRecordingLocked(const std::shared_ptr<Arena>& arena,const char* reason) {
    arena->lifetime.quarantined=true; arena->poisoned.store(true);
    auto found=G().contexts.find({arena->context,arena->generation});
    if(found!=G().contexts.end()) {
        std::lock_guard status_lock(found->second->mutex);
        found->second->status.may_have_recorded_failure=true;
        Reason(found->second->status,State::Quarantined,reason);
    }
}
void CollectRetiredLocked() {
    auto& g=G();
    for(auto it=g.contexts.begin();it!=g.contexts.end();) {
        if(it->second->retired && !it->second->busy.load()) it=g.contexts.erase(it); else ++it;
    }
    for(auto it=g.devices.begin();it!=g.devices.end();) {
        const auto& d=it->second;
        bool active=!d->ever_owned || d->pending_jobs || bool(d->upload);
        for(const auto& [key,c]:g.contexts) { (void)key; active|=c->device==d; }
        for(const auto& arena:d->arenas) active|=arena->leased || bool(arena->upload);
        if(active) { ++it; continue; }
        g.retire_queue.push_back(d); it=g.devices.erase(it); g.work.notify_one();
    }
}
bool DestroyPreparedDevice(const std::shared_ptr<Device>& d) {
    // Only the preparation worker performs driver destruction, after every
    // context is retired and every arena has completion plus successful Reset.
    auto destroy_function=G().destroy_function.load(),destroy_module=G().destroy_module.load();
    for(auto it=d->functions.rbegin();it!=d->functions.rend();++it) if(!destroy_function || destroy_function(d->owner.Get(),*it)!=api::Ok) {
        d->failure="CUDA function destruction failed; retaining device dependencies without retry"; return false;
    }
    for(auto it=d->modules.rbegin();it!=d->modules.rend();++it) if(!destroy_module || destroy_module(d->owner.Get(),*it)!=api::Ok) {
        d->failure="CUDA module destruction failed; retaining device dependencies without retry"; return false;
    }
    d->functions.clear(); d->modules.clear(); d->arenas.clear(); d->weights.Reset(); d->norm.Reset();
    if(d->optimized_helper) { FreeLibrary(d->optimized_helper); d->optimized_helper=nullptr; }
    if(d->packed_expansion_helper) { FreeLibrary(d->packed_expansion_helper); d->packed_expansion_helper=nullptr; }
    d->asset_locks.clear();
    return true;
}
void ReapLocked() {
    for (auto& [identity,d] : G().devices) {
        (void)identity;
        if (d->upload && d->upload->submitted && d->upload->Complete()) { d->upload.reset(); d->preparing=false; if (d->failure.empty()) d->ready = true; }
        if(d->upload && d->upload->submitted && d->upload->fence->GetCompletedValue()==UINT64_MAX)
            d->failure="Device removed during upload; unresolved resources remain quarantined";
        for (auto& a : d->arenas) {
            if (a->upload && a->upload->submitted && a->upload->Complete()) { a->upload.reset(); if (!a->poisoned) a->ready = true; }
            if (!a->leased) continue;
            if(a->lifetime.quarantined) continue;
            for (auto& submission : a->lifetime.submissions) {
                auto* f = reinterpret_cast<ID3D12Fence*>(submission.fence);
                const auto value = f->GetCompletedValue();
                submission.complete = value != UINT64_MAX && value >= submission.value;
            }
            if (!a->lifetime.Reclaimable()) continue;
            auto it = G().contexts.find({a->context,a->generation});
            if (it != G().contexts.end()) { std::lock_guard status_lock(it->second->mutex); ++it->second->status.completed_recordings; }
            a->leased = false; a->lifetime = {}; a->queues.clear(); a->fences.clear(); a->has_recording.store(false);
        }
        d->arenas.erase(std::remove_if(d->arenas.begin(), d->arenas.end(), [](const auto& a) {
            return a->poisoned && !a->leased && !a->upload;
        }), d->arenas.end());
    }
    CollectRetiredLocked();
}
void Worker() {
    auto& g = G();
    for (;;) {
        Job job;
        std::shared_ptr<Device> retired;
        {
            std::unique_lock lock(g.mutex);
            g.work.wait_for(lock, std::chrono::milliseconds(10), [&] { return !g.jobs.empty() || !g.retire_queue.empty(); });
            ReapLocked();
            if(!g.retire_queue.empty()) { retired=g.retire_queue.front(); g.retire_queue.pop_front(); }
            else { if (g.jobs.empty()) continue; job = g.jobs.front(); g.jobs.pop_front(); }
        }
        if(retired) {
            if(!DestroyPreparedDevice(retired)) {
                std::lock_guard lock(g.mutex); Reason(g.status,State::Quarantined,retired->failure.c_str()); g.failed_destroy_devices.push_back(retired);
            }
            continue;
        }
        try {
            if (!job.m) PrepareDevice(job.device);
            else PrepareArena(job.device, job.m, job.optimized);
        } catch (const std::exception& error) {
            std::lock_guard lock(g.mutex);
            // Pool pressure affects only this preparation request, not a device
            // that already has valid ready resources.
            if (!job.m) { job.device->failure = error.what(); job.device->preparing = false; }
            for (auto& [key,c] : g.contexts) if (c->device == job.device) {
                std::lock_guard status_lock(c->mutex); Reason(c->status, State::Fallback, error.what());
            }
        } catch (...) { std::lock_guard lock(g.mutex); if (!job.m) job.device->failure = "Unexpected NVFP4 preparation failure"; }
        { std::lock_guard lock(g.mutex); --job.device->pending_jobs; CollectRetiredLocked(); }
    }
}
void Fail(ScopeData& s, const char* reason) {
    s.failure = s.recorded || s.pending || (s.arena && s.arena->poisoned.load()); s.active = false;
    s.status.may_have_recorded_failure = s.failure;
    Reason(s.status, s.failure ? State::Quarantined : State::Fallback, reason);
    if (s.failure && s.arena) s.arena->poisoned = true;
}
void Uav(ScopeData& s) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    s.call.command_list->ResourceBarrier(1,&b); ++s.status.uav_barriers; s.recorded = true; s.arena->has_recording.store(true);
}
void StageBoundary(ScopeData& s, Stage stage, bool begin, uint32_t pair) {
    if(s.observe_stage) s.observe_stage(stage,begin,31+pair,s.call.command_list,s.observer_user);
}
api::Status Record(ScopeData& s, api::Handle function, const void* params, uint32_t bytes,
                   api::Dim3 grid, api::Dim3 block, uint32_t shared = 0) {
    api::LaunchParams k{function,grid,block,shared,params,bytes};
    s.recorded = true; // A failed vendor call may already have emitted commands.
    s.arena->has_recording.store(true);
    ++s.status.replacement_launches;
    return G().launch.load()(s.call.command_list,&k,1);
}
OriginalLaunchInfo DescribeOriginal(const ScopeData& scope,const api::LaunchParams* k,uint32_t count,uint64_t sequence) {
        const auto* s=&scope;OriginalLaunchInfo info;
        info.sequence=sequence; info.launch_count=count;
        if(k && count==1) {
            info.function=reinterpret_cast<uintptr_t>(k->function);
            info.params_bytes=k->params_bytes; info.shared_bytes=k->shared_bytes;
            info.grid[0]=k->grid.x; info.grid[1]=k->grid.y; info.grid[2]=k->grid.z;
            info.block[0]=k->block.x; info.block[1]=k->block.y; info.block[2]=k->block.z;
            if(s->metadata) {
                auto found=s->metadata->original_functions.find(k->function);
                if(found!=s->metadata->original_functions.end()) {
                    const auto& identity=found->second;
                    info.module=reinterpret_cast<uintptr_t>(identity.module);
                    info.function_name=identity.name.empty() ? nullptr : identity.name.c_str();
                    info.identity_live=identity.device==s->call.device && identity.live && identity.module_live &&
                        identity.live->load(std::memory_order_acquire) && identity.module_live->load(std::memory_order_acquire);
                    auto module=s->metadata->modules.find(identity.module);
                    info.identity_live=info.identity_live && module!=s->metadata->modules.end() && module->second.device==s->call.device;
                    if(module!=s->metadata->modules.end() && module->second.hash_valid)
                        info.module_sha256=module->second.hash.data();
                    auto ffn=s->metadata->functions.find(k->function);
                    info.ffn_target=info.identity_live && ffn!=s->metadata->functions.end() && ffn->second.supported;
                }
            }
        }
        return info;
}
api::Status Forward(ScopeData* s, ID3D12GraphicsCommandList* list, const api::LaunchParams* k, uint32_t count) {
    auto original = G().launch.load(); if (!original) return api::Error;
    if (s) s->status.original_launches += count;
    const Target* target=nullptr;
    if(s && s->observe_stage && k && count==1) {
        auto found=s->metadata->functions.find(k->function);
        if(found!=s->metadata->functions.end() && found->second.supported) target=&found->second;
        if(target && target->kind!=Kind::Contract) StageBoundary(*s,Stage::OriginalPair,true,s->original_pair);
    }
    api::Status rc;
    if(s && s->observe_original_launch && !s->observe_original_input_calls) {
        const auto info=DescribeOriginal(*s,k,count,s->original_sequence++);
        s->observe_original_launch(info,true,api::Ok,list,s->observer_user);
        rc=original(list,k,count);
        s->observe_original_launch(info,false,rc,list,s->observer_user);
    } else rc=original(list,k,count);
    if(target && target->kind==Kind::Contract) { StageBoundary(*s,Stage::OriginalPair,false,s->original_pair); ++s->original_pair; }
    return rc;
}
api::Status ProcessLaunch(ID3D12GraphicsCommandList* list, const api::LaunchParams* k, uint32_t count) {
    auto* s = current_scope;
    // The common process-wide forward path does not take any lock, copy a map,
    // inspect a parameter blob, allocate, encode a descriptor or wait for GPU.
    if (!s || s->call.phase != Phase::Evaluate || s->call.command_list != list)
        return Forward(nullptr,list,k,count);
    if (!s->eligible) return Forward(s->observe_original_launch ? s : nullptr,list,k,count);
    if (s->failure) return api::Error;
    if(s->arena && s->arena->poisoned.load()) { Fail(*s,"Recording ownership was quarantined; no retry"); return api::Error; }
    if (!k || count != 1) {
        if (s->pending || s->active) { Fail(*s,"Unexpected chain after replacement; context must reset"); return api::Error; }
        return Forward(s,list,k,count);
    }
    auto found = s->metadata->functions.find(k->function);
    if (found == s->metadata->functions.end()) {
        if(s->pending) { Fail(*s,"Unknown operator inside replaced FFN pair; no retry"); return api::Error; }
        return Forward(s,list,k,count);
    }
    const auto& target = found->second;
    if (target.device != s->call.device || !target.supported || !target.live || !target.module_live ||
        !target.live->load(std::memory_order_acquire) || !target.module_live->load(std::memory_order_acquire)) {
        if (s->active || s->pending) { Fail(*s,"Module/device identity changed after replacement"); return api::Error; }
        return Forward(s,list,k,count);
    }
    if (s->observe_launch) {
        const uint32_t grid[] = {k->grid.x,k->grid.y,k->grid.z}, block[] = {k->block.x,k->block.y,k->block.z};
        s->observe_launch(target.name,k->params,k->params_bytes,grid,block,k->shared_bytes,s->observer_user);
    }
    if (k->params_bytes != 72 || !k->params) {
        Fail(*s,"Original FFN parameter ABI mismatch");
        return s->failure ? api::Error : Forward(s,list,k,count);
    }
    uint64_t p[8]; uint32_t dims[2]; std::memcpy(p,k->params,64); std::memcpy(dims,static_cast<const uint8_t*>(k->params)+64,8);
    const uint64_t m = uint64_t(dims[0]) * dims[1];
    s->status.observed_m=m;
    if (!m || m > MaximumM) {
        if(s->status.mode==Mode::Passthrough) { Reason(s->status,State::Observing,"Diagnostic passthrough observed an original shape outside the bounded replacement pool"); return Forward(s,list,k,count); }
        Fail(*s,"Original shape has no validated contraction template"); return s->failure ? api::Error : Forward(s,list,k,count);
    }
    if (target.kind == Kind::Publish) {
        if (s->observed_publish || s->pending || s->pair) {
            Fail(*s,"More than one FFN sequence in owned Evaluate"); return s->failure ? api::Error : Forward(s,list,k,count);
        }
        s->observed_publish = true; s->m = m; s->rows = dims[0]; s->columns = dims[1]; s->status.observed_m = m;
        s->active = s->arena && s->arena->m == m && s->status.mode != Mode::Passthrough;
        if (!s->active) Reason(s->status,s->status.mode==Mode::Passthrough ? State::Observing : State::Fallback,
            s->status.mode==Mode::Passthrough ? "Diagnostic passthrough observed owned original FP8 FFN sequence" :
            (!s->call.allow_replacement ? "Replacement is disabled for this context; observing original FP8" :
             ((s->device && AllowedM(*s->device,s->status.mode,m)) ? "Whole Evaluate uses FP8 until complete resources are ready" : "Shape has no accepted contraction implementation for this mode")));
    }
    if (!s->active) return Forward(s,list,k,count);
    if (m != s->m || dims[0] != s->rows || dims[1] != s->columns || s->pair >= 8 || !p[0] || !p[2]) {
        Fail(*s,"FFN block/shape/address sequence mismatch"); return s->failure ? api::Error : Forward(s,list,k,count);
    }
    auto& arena = *s->arena; auto& d = *s->device;
    if (target.kind != Kind::Contract) {
        if (s->pending || (s->pair == 0) != (target.kind == Kind::Publish) ||
            k->grid.x != 32*((m+127)/128) || k->grid.y != 1 || k->grid.z != 1 ||
            k->block.x != 32 || k->block.y != 4 || k->block.z != 1 || k->shared_bytes) {
            Fail(*s,"Original expansion geometry/order mismatch"); return s->failure ? api::Error : Forward(s,list,k,count);
        }
        s->pending = true; s->x = p[0]; s->y = p[2]; s->done = p[7];
        StageBoundary(*s,Stage::ReplacementPair,true,s->pair);
        Uav(*s);
        if(arena.packed_expansion) {
            const auto& info=arena.packed_expansion_info;
            if(p[0]%16 || p[0]>UINT64_MAX-info.original_bytes) {
                Fail(*s,"Packed expansion input violates the accepted address footprint"); return api::Error;
            }
            auto* bytes=static_cast<uint8_t*>(arena.packed_expansion_params[s->pair].Data());
            // This is the builder-declared public linear pointer region, never
            // a TMA descriptor. All immutable operands stay cached per block.
            std::memcpy(bytes+info.linear_input_offset,&p[0],sizeof(uint64_t));
            StageBoundary(*s,Stage::Expansion,true,s->pair);
            if(Record(*s,d.packed_expansion_function,bytes,uint32_t(info.params_bytes),
                {info.grid[0],info.grid[1],info.grid[2]},{info.block[0],info.block[1],info.block[2]},info.shared_bytes)!=api::Ok) {
                Fail(*s,"Packed expansion recording failed; no retry"); return api::Error;
            }
            Uav(*s);StageBoundary(*s,Stage::Expansion,false,s->pair);return api::Ok;
        }
        struct Pack { uint64_t input, output, scale; uint32_t rows, cols; } pack{p[0],arena.xp.Address(),arena.xs.Address(),uint32_t(m),1024};
        StageBoundary(*s,Stage::Pack,true,s->pair);
        if (Record(*s,d.pack,&pack,sizeof(pack),{uint32_t((m*512+255)/256),1,1},{256,1,1}) != api::Ok) { Fail(*s,"Pack recording failed; no retry"); return api::Error; }
        Uav(*s);
        StageBoundary(*s,Stage::Pack,false,s->pair);
        auto& info = arena.expand_info[s->pair]; auto& params = arena.expand_params[s->pair];
        StageBoundary(*s,Stage::Expansion,true,s->pair);
        if (Record(*s,d.expand,params.data(),uint32_t(params.size()),{info.grid[0],info.grid[1],info.grid[2]},
            {info.block[0],info.block[1],info.block[2]},uint32_t(info.shared_bytes)) != api::Ok) { Fail(*s,"Expansion recording failed; no retry"); return api::Error; }
        Uav(*s); StageBoundary(*s,Stage::Expansion,false,s->pair); return api::Ok;
    }
    if (!s->pending || p[0] != s->y || p[1] != s->x || p[6] != s->done ||
        k->grid.x != 8*((m+127)/128) || k->grid.y != 1 || k->grid.z != 4 ||
        k->block.x != 32 || k->block.y != 4 || k->block.z != 1 || k->shared_bytes) {
        Fail(*s,"Contraction alias/geometry/order mismatch; no retry"); return api::Error;
    }
    const auto& params = arena.contract_params[s->pair];
    // The locked three templates explicitly declare workspace_reset_bytes=0.
    StageBoundary(*s,Stage::Contraction,true,s->pair);
    api::Status contract_status=api::Error;
    if(arena.optimized) {
        const auto& info=arena.optimized_info;
        if(arena.fused_publish) {
            if((p[1]%16) || (p[2]%16) || (p[7]%4) || (p[4]%4) || (s->done%4)) {
                Fail(*s,"Fused publication bindings violate the accepted alignment ABI"); return api::Error;
            }
            const NrNvfp4RuntimeBindings bindings{p[1],d.weights.Address()+d.weights_offsets[s->pair].cosine,p[2],p[7],p[4],s->done};
            // Only this build-ID-verified, explicit 48-byte linear region is
            // mutable. All TMA descriptors and every other Params byte stay cached.
            auto* bytes=static_cast<uint8_t*>(arena.optimized_params[s->pair].Data());
            std::memcpy(bytes+arena.fused_info.runtime_bindings_offset,&bindings,sizeof(bindings));
        }
        contract_status=Record(*s,d.optimized_contract,arena.optimized_params[s->pair].Data(),uint32_t(info.params_bytes),
            {info.grid[0],info.grid[1],info.grid[2]},{info.block[0],info.block[1],info.block[2]},info.shared_bytes);
    } else {
        const auto& contract=d.contracts.at(m);
        contract_status=Record(*s,contract.function,params.data(),uint32_t(params.size()),contract.grid,contract.block,contract.shared);
    }
    if (contract_status != api::Ok) {
        Fail(*s,"Contraction recording failed; no retry"); return api::Error;
    }
    Uav(*s);
    StageBoundary(*s,Stage::Contraction,false,s->pair);
    if(arena.fused_publish) {
        StageBoundary(*s,Stage::ReplacementPair,false,s->pair);
        s->pending=false; ++s->pair; ++s->status.replaced_pairs;
        Reason(s->status,State::Ready,"Fused contraction/publication pair recorded; completion remains fence-qualified");
        return api::Ok;
    }
    struct Epilogue { uint64_t gemm,skip,cosine,output; uint32_t rows,pad; uint64_t done,counter,expand_done; } ep{
        arena.c.Address(),p[1],d.weights.Address()+d.weights_offsets[s->pair].cosine,p[2],uint32_t(m),0,p[7],p[4],s->done};
    static_assert(sizeof(Epilogue)==64);
    StageBoundary(*s,Stage::Epilogue,true,s->pair);
    if (Record(*s,d.epilogue,&ep,sizeof(ep),{uint32_t((m*512+255)/256),1,1},{256,1,1}) != api::Ok) {
        Fail(*s,"Epilogue recording failed; no retry"); return api::Error;
    }
    Uav(*s); StageBoundary(*s,Stage::Epilogue,false,s->pair); StageBoundary(*s,Stage::ReplacementPair,false,s->pair);
    s->pending = false; ++s->pair; ++s->status.replaced_pairs;
    Reason(s->status, State::Ready, arena.optimized ? "Optimized FFN pair recorded; GPU completion is reported separately" : "Reference FFN pair recorded; GPU completion is reported separately"); return api::Ok;
}
api::Status __cdecl Launch(ID3D12GraphicsCommandList* list, const api::LaunchParams* k, uint32_t count) {
    auto* s=current_scope;
    if(!s || !s->observe_original_launch || !s->observe_original_input_calls ||
       s->call.phase!=Phase::Evaluate || s->call.command_list!=list)
        return ProcessLaunch(list,k,count);
    // The input sequence includes replaced calls. A contiguous range therefore
    // names the same original graph segment in FP8 and replacement modes.
    const auto info=DescribeOriginal(*s,k,count,s->original_sequence++);
    s->observe_original_launch(info,true,api::Ok,list,s->observer_user);
    const auto rc=ProcessLaunch(list,k,count);
    s->observe_original_launch(info,false,rc,list,s->observer_user);
    return rc;
}
api::Status __cdecl CreateModule(ID3D12Device* d, const void* bytes, uint32_t size, api::Handle* output) {
    auto original = G().create_module.load(); if (!original) return api::Error;
    auto rc = original(d,bytes,size,output);
    if (rc == api::Ok && output && *output) try {
        auto& g = G(); std::lock_guard lock(g.metadata_mutex);
        const auto previous=std::atomic_load(&g.metadata);
        // Invalidate shared generation tokens before any allocation/hash work.
        // Even a scope holding an older immutable map sees handle reuse, and an
        // observer allocation failure cannot preserve a stale supported tag.
        if(auto old=previous->modules.find(*output);old!=previous->modules.end() && old->second.live)
            old->second.live->store(false,std::memory_order_release);
        for(const auto& [handle,target]:previous->functions) {
            (void)handle; if(target.module==*output && target.live)target.live->store(false,std::memory_order_release);
        }
        for(const auto& [handle,identity]:previous->original_functions) {
            (void)handle; if(identity.module==*output && identity.live)identity.live->store(false,std::memory_order_release);
        }
        uint8_t hash[32]{}; bool supported = OriginalModule(bytes,size,hash);
        const bool hash_valid=supported || (g.original_observer_enabled.load() && bytes && size && Hash(bytes,size,hash));
        if (supported && current_scope && current_scope->eligible && g.observer_enabled.load() && current_scope->observe_module)
            current_scope->observe_module(bytes,size,hash,current_scope->observer_user);
        auto next = std::make_shared<Metadata>(*previous);
        for(auto it=next->functions.begin();it!=next->functions.end();) {
            if(it->second.module==*output)it=next->functions.erase(it);else ++it;
        }
        for(auto it=next->original_functions.begin();it!=next->original_functions.end();) {
            if(it->second.module==*output)it=next->original_functions.erase(it);else ++it;
        }
        ModuleIdentity identity{d,supported,std::make_shared<std::atomic<bool>>(true)};
        std::copy(std::begin(hash),std::end(hash),identity.hash.begin()); identity.hash_valid=hash_valid;
        next->modules[*output] = std::move(identity);
        std::atomic_store(&g.metadata,std::shared_ptr<const Metadata>(next));
    } catch (...) { /* Observer failure cannot change original module creation. */ }
    return rc;
}
api::Status __cdecl CreateFunction(ID3D12Device* d, api::Handle module, const char* name, api::Handle* output) {
    auto original = G().create_function.load(); if (!original) return api::Error;
    auto rc = original(d,module,name,output);
    if (rc == api::Ok && output && *output) try {
        auto& g=G(); std::lock_guard lock(g.metadata_mutex);
        const auto previous=std::atomic_load(&g.metadata);
        if(auto old=previous->functions.find(*output);old!=previous->functions.end() && old->second.live)
            old->second.live->store(false,std::memory_order_release);
        if(auto old=previous->original_functions.find(*output);old!=previous->original_functions.end() && old->second.live)
            old->second.live->store(false,std::memory_order_release);
        auto next=std::make_shared<Metadata>(*previous);next->functions.erase(*output);next->original_functions.erase(*output);
        Kind kind=Kind::Publish;const char* stable=nullptr;
        if(name && !std::strcmp(name,"cc_vit_1d_ffn_expand_publish_fp8")){kind=Kind::Publish;stable="cc_vit_1d_ffn_expand_publish_fp8";}
        else if(name && !std::strcmp(name,"cc_vit_1d_ffn_expand_chained_fp8")){kind=Kind::Expand;stable="cc_vit_1d_ffn_expand_chained_fp8";}
        else if(name && !std::strcmp(name,"cc_vit_1d_ffn_contract_chained_fp8")){kind=Kind::Contract;stable="cc_vit_1d_ffn_contract_chained_fp8";}
        if(stable){
            auto m=next->modules.find(module);
            const bool supported=m!=next->modules.end() && m->second.device==d && m->second.supported && m->second.live && m->second.live->load(std::memory_order_acquire);
            next->functions[*output]={d,module,kind,supported,stable,std::make_shared<std::atomic<bool>>(true),m==next->modules.end()?IdentityLive{}:m->second.live};
        }
        if(g.original_observer_enabled.load()) {
            auto m=next->modules.find(module);
            auto f=next->functions.find(*output);
            auto live=f==next->functions.end() ? std::make_shared<std::atomic<bool>>(true) : f->second.live;
            next->original_functions[*output]={d,module,name ? name : "",std::move(live),m==next->modules.end()?IdentityLive{}:m->second.live};
        }
        std::atomic_store(&g.metadata,std::shared_ptr<const Metadata>(next));
    } catch (...) {}
    return rc;
}
api::Status __cdecl DestroyFunction(ID3D12Device* d, api::Handle f) {
    auto original=G().destroy_function.load(); if (!original) return api::Error;
    auto rc=original(d,f); try { auto& g=G(); std::lock_guard lock(g.metadata_mutex);
        const auto previous=std::atomic_load(&g.metadata);
        auto old=previous->functions.find(f);
        if(old!=previous->functions.end() && old->second.device==d && old->second.live)old->second.live->store(false,std::memory_order_release);
        auto diagnostic=previous->original_functions.find(f);
        if(diagnostic!=previous->original_functions.end() && diagnostic->second.device==d && diagnostic->second.live)diagnostic->second.live->store(false,std::memory_order_release);
        auto next=std::make_shared<Metadata>(*previous); auto it=next->functions.find(f);
        if(it!=next->functions.end() && it->second.device==d) next->functions.erase(it);
        auto raw=next->original_functions.find(f);
        if(raw!=next->original_functions.end() && raw->second.device==d) next->original_functions.erase(raw);
        std::atomic_store(&g.metadata,std::shared_ptr<const Metadata>(next));
    } catch (...) {}
    return rc;
}
api::Status __cdecl DestroyModule(ID3D12Device* d, api::Handle m) {
    auto original=G().destroy_module.load(); if (!original) return api::Error;
    auto rc=original(d,m); try { auto& g=G(); std::lock_guard lock(g.metadata_mutex);
        const auto previous=std::atomic_load(&g.metadata);auto old=previous->modules.find(m);
        if(old!=previous->modules.end() && old->second.device==d && old->second.live)old->second.live->store(false,std::memory_order_release);
        for(const auto& [handle,target]:previous->functions){(void)handle;if(target.device==d && target.module==m && target.live)target.live->store(false,std::memory_order_release);}
        for(const auto& [handle,identity]:previous->original_functions){(void)handle;if(identity.device==d && identity.module==m && identity.live)identity.live->store(false,std::memory_order_release);}
        auto next=std::make_shared<Metadata>(*previous); auto it=next->modules.find(m);
        if(it!=next->modules.end() && it->second.device==d) next->modules.erase(it);
        for(auto f=next->functions.begin();f!=next->functions.end();) if(f->second.device==d && f->second.module==m) f=next->functions.erase(f); else ++f;
        for(auto f=next->original_functions.begin();f!=next->original_functions.end();) if(f->second.device==d && f->second.module==m) f=next->original_functions.erase(f); else ++f;
        std::atomic_store(&g.metadata,std::shared_ptr<const Metadata>(next));
    } catch (...) {}
    return rc;
}
void* __cdecl Query(uint32_t id) { auto original=G().query; return original ? WrapNvapi(id,original(id)) : nullptr; }
} // namespace

#include "public_api.inl"
} // namespace nr::nvfp4

#if defined(CINTERFACE)
#error Compile this file separately without CINTERFACE; include only its public header in the Core.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <new>
#include <cstring>
#include <limits>
#include "nr_dual_spatial_d3d12.hpp"
#include "nr_dual_embedded.hpp"

namespace nr_dual::dx12 {
namespace {
template<class T> void Drop(T*& p) noexcept { if (p) { p->Release(); p = nullptr; } }
constexpr auto ReadState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto WriteState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
             std::uint32_t& state, D3D12_RESOURCE_STATES next) {
    if (state == std::uint32_t(next)) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(state);
    b.Transition.StateAfter = next;
    list->ResourceBarrier(1, &b);
    state = std::uint32_t(next);
}
D3D12_RESOURCE_DESC Description(Extent size, DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = size.width; d.Height = size.height;
    d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = format; d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return d;
}
bool SameDevice(ID3D12Device* expected, ID3D12Resource* resource) {
    ID3D12Device* actual = nullptr; IUnknown* a = nullptr; IUnknown* b = nullptr;
    bool ok = SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&actual))) && actual &&
              SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(&a))) && a &&
              SUCCEEDED(actual->QueryInterface(IID_PPV_ARGS(&b))) && b && a == b;
    Drop(a); Drop(b); Drop(actual); return ok;
}
bool Supports(ID3D12Device* device, DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT s{}; s.Format = format;
    return SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &s, sizeof(s))) &&
        (s.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) &&
        (s.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) &&
        (s.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
        (s.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
}
bool MakeTexture(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc,
                 ID3D12Resource** out, const wchar_t* name) {
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_DEFAULT;
    // First integration is single-node D3D12 only; do not select CUDA/device 0.
    h.CreationNodeMask = 1; h.VisibleNodeMask = 1;
    const auto hr = device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(out));
    if (FAILED(hr) || !*out) return false;
    (*out)->SetName(name); return true;
}
std::uint64_t AllocBytes(ID3D12Device* device, const D3D12_RESOURCE_DESC& d) {
    const auto a = device->GetResourceAllocationInfo(0, 1, &d);
    return a.SizeInBytes != std::numeric_limits<UINT64>::max() ? a.SizeInBytes : 0;
}
} // namespace

struct Pipelines {
    ID3D12Device* device = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* down = nullptr;
    ID3D12PipelineState* residual = nullptr;
    bool recorded = false, poisoned = false;
    std::uint32_t failure_result = 0;
    std::uint32_t live_slots = 0;
};
struct SpatialSlot {
    Pipelines* owner = nullptr; // Keeps owner logically pinned via live_slots.
    ID3D12Resource* primary = nullptr;
    ID3D12Resource* low_input = nullptr;
    ID3D12Resource* second = nullptr;
    ID3D12Resource* valid = nullptr;
    ID3D12Resource* delta = nullptr;
    ID3D12DescriptorHeap* delta_heap = nullptr;
    std::uint32_t delta_state = D3D12_RESOURCE_STATE_COMMON;
    ID3D12DescriptorHeap* down_heap = nullptr;
    Plan plan{};
    ModelDomain domain = ModelDomain::DirectValue;
    std::uint32_t input_state = D3D12_RESOURCE_STATE_COMMON;
    std::uint32_t second_state = D3D12_RESOURCE_STATE_COMMON;
    std::uint32_t valid_state = D3D12_RESOURCE_STATE_COMMON;
    std::uint64_t bytes = 0;
    bool recorded = false, poisoned = false;
    std::uint32_t failure_result = 0;
};
namespace {
void DropSlot(SpatialSlot* s) noexcept {
    if (!s) return;
    Drop(s->delta_heap);Drop(s->delta);
    Drop(s->down_heap); Drop(s->valid); Drop(s->second); Drop(s->low_input); Drop(s->primary);
    if (s->owner && s->owner->live_slots) --s->owner->live_slots;
    delete s;
}
void DropPipelines(Pipelines* p) noexcept {
    if (!p) return;
    Drop(p->residual);
    Drop(p->down); Drop(p->root); Drop(p->device); delete p;
}
}
Pipelines* CreatePipelines(ID3D12Device* device, std::string& error) {
    error.clear();
    if (!device || device->GetNodeCount() != 1) { error = "A proven single-node D3D12 device is required"; return nullptr; }
    if (!Supports(device, DXGI_FORMAT_R16G16B16A16_FLOAT) || !Supports(device, DXGI_FORMAT_R32_UINT) || !Supports(device, DXGI_FORMAT_R32G32B32A32_FLOAT)) {
        error = "Required FP16/R32_UINT load and UAV-store support is absent"; return nullptr;
    }
    auto* p = new(std::nothrow) Pipelines;
    if (!p) { error = "Pipeline allocation failed"; return nullptr; }
    p->device = device; device->AddRef();
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 3; ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 2; ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 3;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0; params[1].Constants.Num32BitValues = 8;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 2; root.pParameters = params;
    ID3DBlob* binary = nullptr; ID3DBlob* errors = nullptr;
    auto hr = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &binary, &errors);
    if (FAILED(hr) || !binary) {
        error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "Root serialization failed";
        Drop(binary); Drop(errors); DropPipelines(p); return nullptr;
    }
    Drop(errors);
    hr = device->CreateRootSignature(0, binary->GetBufferPointer(), binary->GetBufferSize(), IID_PPV_ARGS(&p->root));
    Drop(binary);
    if (FAILED(hr) || !p->root) { error = "CreateRootSignature failed"; DropPipelines(p); return nullptr; }
    hr = D3DCompile(embedded::downsample, sizeof(embedded::downsample) - 1,
        "nr_dual_downsample", nullptr, nullptr, "DownsampleCS", "cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &binary, &errors);
    if (FAILED(hr) || !binary) {
        error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "DownsampleCS compilation failed";
        Drop(binary); Drop(errors); DropPipelines(p); return nullptr;
    }
    Drop(errors);
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature = p->root;
    d.CS = {binary->GetBufferPointer(), binary->GetBufferSize()};
    hr = device->CreateComputePipelineState(&d, IID_PPV_ARGS(&p->down));
    Drop(binary);
    if (FAILED(hr) || !p->down) { error = "Downsample pipeline creation failed"; DropPipelines(p); return nullptr; }
    hr=D3DCompile(embedded::residual,sizeof(embedded::residual)-1,"nr_dual_residual",nullptr,nullptr,"ResidualCS","cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
    if(FAILED(hr)||!binary){error=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"ResidualCS compilation failed";Drop(binary);Drop(errors);DropPipelines(p);return nullptr;}
    Drop(errors);d.CS={binary->GetBufferPointer(),binary->GetBufferSize()};
    hr=device->CreateComputePipelineState(&d,IID_PPV_ARGS(&p->residual));Drop(binary);
    if(FAILED(hr)||!p->residual){error="Residual pipeline creation failed";DropPipelines(p);return nullptr;}
    return p;
}
SpatialSlot* CreateSpatialSlot(Pipelines* p, ID3D12Resource* first,
                              const Plan& plan, ModelDomain domain,
                              std::uint64_t available, std::string& error) {
    error.clear();
    if (!p || !p->device || p->poisoned || !first || !plan || plan.path == Path::Single ||
        (domain != ModelDomain::DirectValue && domain != ModelDomain::SrgbProxy) ||
        !SameDevice(p->device, first)) {
        error = "Invalid pipeline/plan/domain/device"; return nullptr;
    }
    const auto d = first->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width != plan.primary.width ||
        d.Height != plan.primary.height || d.DepthOrArraySize != 1 || d.MipLevels != 1 ||
        d.SampleDesc.Count != 1 || d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
        (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0 ||
        (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0) {
        error = "Primary must be a compact, ordinary owned FP16 texture, not a padded/game/aliased view"; return nullptr;
    }
    const auto color = Description(plan.secondary, DXGI_FORMAT_R16G16B16A16_FLOAT);
    const auto mask = Description(plan.secondary, DXGI_FORMAT_R32_UINT);
    const auto delta = Description(plan.secondary, DXGI_FORMAT_R32G32B32A32_FLOAT);
    const auto color_bytes = AllocBytes(p->device, color);
    const auto mask_bytes = plan.path == Path::MatchedResidual ? AllocBytes(p->device, mask) : 0;
    const auto delta_bytes=plan.path==Path::MatchedResidual?AllocBytes(p->device,delta):0;
    const auto total = plan.path == Path::MatchedResidual ? 2 * color_bytes + mask_bytes + delta_bytes : color_bytes;
    if (!color_bytes || (plan.path == Path::MatchedResidual && (!mask_bytes||!delta_bytes)) || total > available) {
        error = "Secondary texture allocation exceeds the remaining Core budget"; return nullptr;
    }
    auto* s = new(std::nothrow) SpatialSlot;
    if (!s) { error = "Spatial slot allocation failed"; return nullptr; }
    s->owner = p; ++p->live_slots;
    s->plan = plan; s->domain = domain; s->bytes = total;
    s->primary = first; first->AddRef();
    if (!MakeTexture(p->device, color, &s->second, L"NRDual.SecondAnswer")) {
        error = "Secondary answer allocation failed"; DropSlot(s); return nullptr;
    }
    if (plan.path == Path::DirectSecond) return s; // No copy, downsample, delta, or second full-size intermediate.
    if (!MakeTexture(p->device, color, &s->low_input, L"NRDual.LowInput") ||
        !MakeTexture(p->device, mask, &s->valid, L"NRDual.LowValid") ||
        !MakeTexture(p->device, delta, &s->delta, L"NRDual.LowMatchedDelta")) {
        error = "Low input/validity allocation failed"; DropSlot(s); return nullptr;
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = 5;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(p->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&s->down_heap))) || !s->down_heap) {
        error = "Downsample descriptor allocation failed"; DropSlot(s); return nullptr;
    }
    if(FAILED(p->device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&s->delta_heap)))||!s->delta_heap){error="Residual descriptor allocation failed";DropSlot(s);return nullptr;}
    const auto increment=p->device->GetDescriptorHandleIncrementSize(heap.Type);
    auto srv=[&](ID3D12DescriptorHeap* h,unsigned i,ID3D12Resource* resource,DXGI_FORMAT format){
        auto cpu=h->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=std::size_t(i)*increment;
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
        p->device->CreateShaderResourceView(resource,&d,cpu);};
    auto uav=[&](ID3D12DescriptorHeap* h,unsigned i,ID3D12Resource* resource,DXGI_FORMAT format){
        auto cpu=h->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=std::size_t(i)*increment;
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        p->device->CreateUnorderedAccessView(resource,nullptr,&d,cpu);};
    srv(s->down_heap,0,first,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(s->down_heap,1,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);srv(s->down_heap,2,nullptr,DXGI_FORMAT_R32_UINT);
    uav(s->down_heap,3,s->low_input,DXGI_FORMAT_R16G16B16A16_FLOAT);uav(s->down_heap,4,s->valid,DXGI_FORMAT_R32_UINT);
    srv(s->delta_heap,0,s->low_input,DXGI_FORMAT_R16G16B16A16_FLOAT);srv(s->delta_heap,1,s->second,DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(s->delta_heap,2,s->valid,DXGI_FORMAT_R32_UINT);uav(s->delta_heap,3,s->delta,DXGI_FORMAT_R32G32B32A32_FLOAT);
    uav(s->delta_heap,4,nullptr,DXGI_FORMAT_R32_UINT);
    return s;
}
Surfaces GetSurfaces(const SpatialSlot* s) noexcept {
    if (!s) return {};
    return {s->primary, s->low_input ? s->low_input : s->primary, s->second, s->valid,
            s->plan.primary, s->plan.secondary, s->domain,s->delta};
}
std::uint64_t SlotBytes(const SpatialSlot* s) noexcept { return s ? s->bytes : 0; }

namespace {
void RecordInput(Pipelines* p,SpatialSlot* s,ID3D12GraphicsCommandList* list,std::uint32_t& primary_state) {
            s->recorded=true;p->recorded=true;
            Barrier(list, s->primary, primary_state, ReadState);
            Barrier(list, s->low_input, s->input_state, WriteState);
            Barrier(list, s->valid, s->valid_state, WriteState);
            ID3D12DescriptorHeap* heaps[] = {s->down_heap};
            list->SetDescriptorHeaps(1, heaps);
            list->SetComputeRootSignature(p->root);
            list->SetComputeRootDescriptorTable(0, s->down_heap->GetGPUDescriptorHandleForHeapStart());
            const std::uint32_t constants[8] = {s->plan.primary.width, s->plan.primary.height,
                s->plan.secondary.width, s->plan.secondary.height, std::uint32_t(s->domain), 0, 0, 0};
            list->SetComputeRoot32BitConstants(1, 8, constants, 0);
            list->SetPipelineState(p->down);
            list->Dispatch((s->plan.secondary.width + 7) / 8, (s->plan.secondary.height + 7) / 8, 1);
            // UAV -> SRV transitions order these writes before NGX/final shader reads.
            Barrier(list, s->low_input, s->input_state, ReadState);
            Barrier(list, s->valid, s->valid_state, ReadState);
}
}
bool RecordCreationInputs(Pipelines* p,SpatialSlot* s,ID3D12GraphicsCommandList* list,std::uint32_t& primary_state) {
    if(!p||!s||s->owner!=p||p->poisoned||s->poisoned||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    if(s->plan.path==Path::MatchedResidual)RecordInput(p,s,list,primary_state);
    else Barrier(list,s->primary,primary_state,ReadState);
    Barrier(list,s->second,s->second_state,WriteState);
    s->recorded=true;p->recorded=true;return true;
}

Attempt RecordSecond(Pipelines* p, SpatialSlot* s, ID3D12GraphicsCommandList* list,
                     std::uint32_t& primary_state, const Plan& plan, const Admission& a,
                     EvaluateFn evaluate, void* user) {
    struct Backend {
        Pipelines* p; SpatialSlot* s; ID3D12GraphicsCommandList* list;
        std::uint32_t& primary_state; const Admission& a; EvaluateFn evaluate; void* user;
        bool PreviouslyPoisoned() const {
            return (p && p->poisoned) || (s && s->poisoned);
        }
        std::uint32_t PriorFailureResult() const {
            return p && p->poisoned ? p->failure_result : (s ? s->failure_result : 0);
        }
        bool ValidateNoCommands(const Plan& plan) {
            return p && s && s->owner == p && !s->poisoned && list && evaluate &&
                list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT &&
                s->plan.path == plan.path && s->plan.primary == plan.primary &&
                s->plan.secondary == plan.secondary && s->primary && s->second &&
                (plan.path != Path::MatchedResidual || (s->low_input && s->valid && s->down_heap && s->delta && s->delta_heap && p->residual));
        }
        void MarkRecorded() { s->recorded = true; p->recorded = true; }
        void RecordDownsample() { RecordInput(p,s,list,primary_state); }
        void PrepareOutputForEvaluate() {
            MarkRecorded();
            if (s->plan.path == Path::DirectSecond) Barrier(list, s->primary, primary_state, ReadState);
            Barrier(list, s->second, s->second_state, WriteState);
        }
        std::uint32_t EvaluateOnce() { return evaluate(user, list, a.secondary_feature, a.secondary_params); }
        void PoisonWithoutFurtherGpuCommands(std::uint32_t r) {
            s->poisoned = true; s->failure_result = r;
            p->poisoned = true; p->failure_result = r;
        }
        void MakeOutputReadable() {
            Barrier(list, s->second, s->second_state, ReadState);
            if(s->plan.path==Path::MatchedResidual) {
                Barrier(list,s->delta,s->delta_state,WriteState);
                list->SetDescriptorHeaps(1,&s->delta_heap);list->SetComputeRootSignature(p->root);
                list->SetComputeRootDescriptorTable(0,s->delta_heap->GetGPUDescriptorHandleForHeapStart());
                const std::uint32_t c[8]={s->plan.secondary.width,s->plan.secondary.height,std::uint32_t(s->domain),0,0,0,0,0};
                list->SetComputeRoot32BitConstants(1,8,c,0);list->SetPipelineState(p->residual);
                list->Dispatch((c[0]+7)/8,(c[1]+7)/8,1);Barrier(list,s->delta,s->delta_state,ReadState);
            }
        }
    } backend{p, s, list, primary_state, a, evaluate, user};
    return RecordPreparedSecond(plan, a, backend);
}

bool DestroySpatialSlotAfterRetirement(SpatialSlot*& s, RetiredFn predicate, void* user) noexcept {
    if (!s) return true;
    // Even before our first Evaluate, external NGX Create may have recorded
    // references to these surfaces. Never infer "unused" from our own recorded bit.
    if (s->poisoned || !predicate || !predicate(user)) return false;
    DropSlot(s); s = nullptr; return true;
}
bool DestroyPipelinesAfterRetirement(Pipelines*& p, RetiredFn predicate, void* user) noexcept {
    if (!p) return true;
    if (p->live_slots || !predicate || !predicate(user)) return false;
    DropPipelines(p); p = nullptr; return true;
}
} // namespace nr_dual::dx12

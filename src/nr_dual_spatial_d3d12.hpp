#pragma once
// Compile src/nr_dual_spatial_d3d12.cpp as a separate, ordinary C++ translation unit.
// This header also works when the Core uses CINTERFACE/COBJMACROS; no COM calls here.
#include "nr_dual_pass.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;

namespace nr_dual::dx12 {
struct Pipelines;
struct SpatialSlot;

// These structs contain NO NGX implementation; use the Core's existing signed
// NRChain bridge and native parameter factory. Do not load another NR provider.
using EvaluateFn = std::uint32_t (*)(void* user, ID3D12GraphicsCommandList* list,
                                    const void* feature, const void* params);
using TimestampFn = void (*)(void* user,ID3D12GraphicsCommandList* list,unsigned query);
using RetiredFn = bool (*)(void* user); // Core's exact-fence AND recording-ref retirement predicate.
using DeviceIdentityFn = bool (*)(ID3D12Device*,ID3D12Device*);
enum class SpatialError : unsigned { None, Contract, DeviceIdentity, Budget, Allocation };

struct Surfaces {
    ID3D12Resource* primary = nullptr;   // P1, borrowed from the pinned primary slot
    ID3D12Resource* input = nullptr;     // P1 at 100%; otherwise the owned L1
    ID3D12Resource* output = nullptr;    // owned P2 or L2, always separate from input
    ID3D12Resource* low_valid = nullptr; // nullptr at 100%; R32_UINT at reduced size
    Extent primary_extent{}, secondary_extent{};
    ModelDomain domain = ModelDomain::DirectValue;
    ID3D12Resource* low_delta = nullptr; // FP32 matched residual + validity at N2 only
    bool owns_low_valid = false;          // true for this slot's valid; false for a retained residual base
};

// CPU allocation/compilation only, not a hot-path operation. Source is embedded
// reproducibly by tools/embed_shaders.py. No shader compiler DLL is distributed.
Pipelines* CreatePipelines(ID3D12Device* device, std::string& error);
// budget_available_bytes must already account for every active/retired generation
// under the Core lock. Reserve SlotBytes() before exposing the slot to other threads.
SpatialSlot* CreateSpatialSlot(Pipelines* pipelines, ID3D12Resource* first_answer,
                               const Plan& plan, ModelDomain domain,
                               std::uint64_t budget_available_bytes, std::string& error,
                               DeviceIdentityFn same_device=nullptr,SpatialError* failure=nullptr,
                               const Surfaces* residual_base=nullptr);
Surfaces GetSurfaces(const SpatialSlot* slot) noexcept;
// A cumulative low-resolution chain retains the first reduced input/validity
// while evaluating later outputs; descriptors remain immutable in flight.
bool MatchesResidualBase(const SpatialSlot* slot,const Surfaces* base) noexcept;
std::uint64_t SlotBytes(const SpatialSlot* slot) noexcept;

// After the Core pins both real slots, initialize the Create input from this
// frame's successful P1 and establish the exact Create texture states. No NR
// Evaluate is made; its completion must be observed before RecordSecond.
bool RecordCreationInputs(Pipelines* pipelines, SpatialSlot* slot,
                          ID3D12GraphicsCommandList* list,
                          std::uint32_t& primary_state);

// The caller must already bind this exact GetSurfaces().input/output pair into the
// independent secondary params and supply factual Admission checks. uint32_t state
// is D3D12_RESOURCE_STATES, to avoid COM header/macro pollution in the Core.
// This updates primary_state after a real transition, records NO SR, creates NO
// feature, performs NO SubmitAndWait/readback, and does not restore game bindings.
// Enclose the WHOLE chain in the Core's existing NativeCommandStateScope.
Attempt RecordSecond(Pipelines* pipelines, SpatialSlot* slot,
                     ID3D12GraphicsCommandList* list, std::uint32_t& primary_state,
                     const Plan& plan, const Admission& admission,
                     EvaluateFn evaluate, void* evaluate_user,
                     TimestampFn timestamp=nullptr,void* timestamp_user=nullptr);

// Retirement proof is mandatory even before RecordSecond: external NGX Create
// may already reference the exported surfaces. No destructor releases GPU objects.
// A returned NGX error
// poisons the slot; it is then retained until process exit, along with all Core
// dependencies. False means retained, not "already destroyed".
bool DestroySpatialSlotAfterRetirement(SpatialSlot*& slot, RetiredFn can_retire, void* user) noexcept;
bool DestroyPipelinesAfterRetirement(Pipelines*& pipelines, RetiredFn can_retire, void* user) noexcept;
} // namespace nr_dual::dx12

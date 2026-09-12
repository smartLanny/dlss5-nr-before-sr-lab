#pragma once
// Only the companion's PRIVATE D3D12 evaluation list may call Bind.
// This is NOT permission to modify a game's arbitrary Reset(nullptr) list.
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace nr_bridge_baseline {
// One bounded process-lifetime set, matching the pinned carrier session. Never
// release a PSO/root while a submitted list might still reference it. No image,
// descriptor, queue, allocator or resource-state ownership is acquired here.
inline SRWLOCK lock = SRWLOCK_INIT;
inline ID3D12Device* owner = nullptr;
inline ID3D12RootSignature* root = nullptr;
inline ID3D12PipelineState* pso = nullptr;

inline bool Bind(ID3D12GraphicsCommandList* list) {
    if (!list || !TryAcquireSRWLockExclusive(&lock)) return false;
    ID3D12Device* device = nullptr;
    bool ok = SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))) && device;
    if (ok && owner && owner != device) ok = false; // no cross-device reuse
    if (ok && !pso) {
        constexpr char shader[] = "[numthreads(1,1,1)] void main() {}";
        ID3DBlob *code = nullptr, *serialized = nullptr, *errors = nullptr;
        ID3D12RootSignature* next_root = nullptr;
        ID3D12PipelineState* next_pso = nullptr;
        D3D12_ROOT_SIGNATURE_DESC desc{};
        ok = SUCCEEDED(D3DCompile(shader, sizeof(shader)-1, "nr-private-baseline",
            nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &code, &errors));
        if (errors) { errors->Release(); errors = nullptr; }
        if (ok) ok = SUCCEEDED(D3D12SerializeRootSignature(&desc,
            D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
        if (errors) errors->Release();
        if (ok) ok = SUCCEEDED(device->CreateRootSignature(0,
            serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&next_root)));
        if (ok) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
            compute.pRootSignature = next_root;
            compute.CS = {code->GetBufferPointer(), code->GetBufferSize()};
            ok = SUCCEEDED(device->CreateComputePipelineState(&compute,
                IID_PPV_ARGS(&next_pso)));
        }
        if (code) code->Release();
        if (serialized) serialized->Release();
        if (ok) {
            owner = device; owner->AddRef();
            root = next_root; pso = next_pso;
        } else {
            if (next_pso) next_pso->Release();
            if (next_root) next_root->Release();
        }
    }
    if (ok) {
        // Go THROUGH the ReShade list wrapper so the normal r3 observer sees
        // both writes. Do not unwrap, fabricate shadow state, or dispatch.
        list->SetPipelineState(pso);
        list->SetComputeRootSignature(root);
    }
    if (device) device->Release();
    ReleaseSRWLockExclusive(&lock);
    return ok;
}
} // namespace nr_bridge_baseline

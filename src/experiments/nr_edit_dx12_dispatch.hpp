#pragma once
#include "nr_edit_dispatch_slots.hpp"
#include <cstddef>
#include <cstdint>
struct ID3D12Device; struct ID3D12Resource; struct ID3D12GraphicsCommandList;
struct ID3D12CommandQueue; struct ID3D12Fence;
namespace nr_edit::dx12 {
struct Dispatcher;
struct View { ID3D12Resource* resource=nullptr; std::uint32_t format=0,mip=0; };
struct Pass {
    ID3D12GraphicsCommandList* list=nullptr;
    // Eleven SRVs / five UAVs match the existing RR edit shader. Deferred
    // uses a subset; typed null descriptors cover slots that a shader ignores.
    View reads[11]{},writes[5]{};
    const void* constants=nullptr;std::size_t constant_bytes=0;
    unsigned width=0,height=0;
};
// The path is the checked-in HLSL; includes use D3D_COMPILE_STANDARD_FILE_INCLUDE.
// Only compilation/allocation occurs here. SDK/compiler/runtime DLLs are not bundled.
Dispatcher* Create(ID3D12Device*,const wchar_t* shader_path,const char* entry,char* error,std::size_t error_size);
// All referenced resources must be private/pinned and in their legal SRV/UAV
// states. Enclose this in the Core's complete NativeCommandStateScope. The helper
// neither guesses barriers nor saves a partial root-state snapshot.
dispatch::Ticket Record(Dispatcher*,const Pass&);
// Call AFTER the actual list has executed and its exact queue fence was signaled.
// Each recorded ticket must be submitted, or quarantined. No frame-count retirement.
bool Submitted(Dispatcher*,dispatch::Ticket,ID3D12CommandQueue*,ID3D12Fence*,std::uint64_t);
void Collect(Dispatcher*);
void Quarantine(Dispatcher*);
// False leaves the whole object alive. Never delete a failed/in-flight dispatcher.
bool DestroyAfterRetirement(Dispatcher*&);
}

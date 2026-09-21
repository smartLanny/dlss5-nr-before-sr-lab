#pragma once
// Ordinary C++ TU using the Core compiler/ABI, not inside its CINTERFACE namespace.
#include "nr_private_sr_runtime.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
struct ID3D12Device;struct ID3D12Resource;struct ID3D12GraphicsCommandList;struct ID3D12Fence;
namespace nr_edit_dx12 {
using Id=std::uint64_t;
struct Bytecode {const void* data=nullptr;std::size_t bytes=0;};
enum class Kernel:unsigned {RrGuides,RrAccumulate,RrCompose,DeferredEncode,DeferredCompose,SparseAdvance,NormalizeGuides,PostExtract,PostCompose,Count};
struct View {
    ID3D12Resource* resource=nullptr;
    unsigned format=0,mip=0,state=0;
    bool state_known=false;
};
struct Work {
    Kernel kernel=Kernel::Count;
    nr_private_sr::Recording recording{};
    unsigned width=0,height=0;
    const void* constants=nullptr;unsigned constant_bytes=0;
    // t0..t10/u0..u4. Unused descriptors must be null. Read/write aliasing is rejected.
    std::array<View,11> reads{};std::array<View,5> writes{};
};
struct Token {Id serial=0;unsigned slot=0;explicit operator bool()const noexcept{return serial!=0;}};
enum class Status {Ready,Invalid,Busy,DeviceMismatch,Format,Allocation,Poisoned};
struct Answer {Status status=Status::Invalid;Token token{};bool recorded=false;};
using SameDevice=bool(*)(ID3D12Device*,ID3D12Device*);
class Kernels;
Kernels* Create(ID3D12Device*,Id device_identity,Id queue_identity,
                const std::array<Bytecode,static_cast<unsigned>(Kernel::Count)>&,
                SameDevice,unsigned* error=nullptr);
Kernels* Create(ID3D12Device*,Id,Id,const std::array<Bytecode,5>&,SameDevice,unsigned* error=nullptr);
// Bounded immutable dispatch packets, never wrap over an in-flight packet.
// Caller pins resources and owns a NativeCommandStateScope. States restored on return.
Answer Record(Kernels*,ID3D12GraphicsCommandList*,const Work&);
bool Submitted(Kernels*,nr_private_sr::Recording,ID3D12Fence*,nr_private_sr::Fence,bool success);
bool Discarded(Kernels*,nr_private_sr::Recording);
bool Collect(Kernels*);
bool Poisoned(const Kernels*);
void SetRetirementGuard(Kernels*,nr_private_sr::RecordingReleased,void*);
void MarkUncertain(Kernels*);
bool ValidateRead(Kernels*,const View&,unsigned width,unsigned height);
bool ReadyToRetire(Kernels*);
bool DestroyAfterRetirement(Kernels*&);
// Allocations count against the existing shared budget, not an unaccounted pool.
ID3D12Resource* CreatePlane(ID3D12Device*,unsigned w,unsigned h,unsigned format,
                           std::uint64_t available_bytes,std::uint64_t* actual_bytes);
}

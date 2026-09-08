#pragma once
#include <cstddef>
#include <cstdint>
#include "backend.h"

// Narrow x64 ABI, checked against NVIDIA/nvapi nvapi.h. Keep this independent
// of the SDK's C++ macros: Core is built with CINTERFACE in a separate TU.
namespace nr::nvfp4::api {
using Handle = void*;
using Status = int32_t;
constexpr Status Ok = 0, Error = -1;
struct Dim3 { uint32_t x, y, z; };
struct LaunchParams {
    Handle function;
    Dim3 grid;
    Dim3 block;
    uint32_t shared_bytes;
    const void* params;
    uint32_t params_bytes;
};
static_assert(sizeof(LaunchParams) == 56 && offsetof(LaunchParams, params) == 40);
using Query = void* (__cdecl*)(uint32_t);
using CreateModule = Status (__cdecl*)(ID3D12Device*, const void*, uint32_t, Handle*);
using CreateFunction = Status (__cdecl*)(ID3D12Device*, Handle, const char*, Handle*);
using Launch = Status (__cdecl*)(ID3D12GraphicsCommandList*, const LaunchParams*, uint32_t);
using Destroy = Status (__cdecl*)(ID3D12Device*, Handle);
constexpr uint32_t CreateModuleId = 0xad1a677d, CreateFunctionId = 0xe2436e22;
constexpr uint32_t LaunchId = 0x24973538, DestroyModuleId = 0x41c65285;
constexpr uint32_t DestroyFunctionId = 0xdf295ea6;
} // namespace nr::nvfp4::api

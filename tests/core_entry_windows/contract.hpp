#pragma once
#include <windows.h>
#include <cstdint>
struct ID3D12GraphicsCommandList {};
struct NgxHandle { unsigned id=41; };
struct NgxParams {
    unsigned native_calls=0,core_entries=0,model_entries=0;
    bool covered_at_native_entry=false;
    void (*model_preflight)()=nullptr;
    NgxHandle handle{};
};
using NgxResult=std::uint32_t;
using CreateFn=NgxResult (*)(ID3D12GraphicsCommandList*,int,NgxParams*,NgxHandle**);

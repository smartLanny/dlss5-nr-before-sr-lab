#include "contract.hpp"
extern "C" __declspec(dllexport) NgxResult RunCreate(HMODULE core,NgxParams* p,NgxHandle** out) {
    auto create=reinterpret_cast<CreateFn>(GetProcAddress(core,"NVSDK_NGX_D3D12_CreateFeature"));
    if(!create)return 0xBAD00007u;
    return create(nullptr,1,p,out);
}

#include "contract.hpp"
extern "C" __declspec(dllexport) NgxResult NVSDK_NGX_D3D12_CreateFeature(
    ID3D12GraphicsCommandList*,int feature,NgxParams* p,NgxHandle** output) {
    ++p->native_calls;
    p->covered_at_native_entry=p->core_entries!=0;
    if(p->model_preflight)p->model_preflight();
    ++p->model_entries;
    if(output)*output=&p->handle;
    return feature==1?1u:0xBAD00002u;
}

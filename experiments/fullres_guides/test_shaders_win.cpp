#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include "shader_source.hpp"

int main() {
    using Microsoft::WRL::ComPtr;
    const D3D_SHADER_MACRO gray[]={{"PREP","1"},{nullptr,nullptr}};
    const D3D_SHADER_MACRO rgba[]={{"PREP","1"},{"PREP_RGBA","1"},{nullptr,nullptr}};
    const D3D_SHADER_MACRO* variants[]={nullptr,gray,rgba};
    const char* names[]={"motion-conversion","gray-preparation","rgba-preparation"};
    for(unsigned i=0;i<3;++i) {
        ComPtr<ID3DBlob> code,errors;
        const HRESULT hr=D3DCompile(nr::fullres::guide_shader,sizeof(nr::fullres::guide_shader)-1,
            names[i],variants[i],nullptr,"CS","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(errors && errors->GetBufferSize())
            std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);
        if(FAILED(hr) || !code || !code->GetBufferSize()) {
            std::fprintf(stderr,"FAIL %s HRESULT=0x%08lX\n",names[i],static_cast<unsigned long>(hr));
            return 1;
        }
        std::printf("PASS D3DCompile cs_5_0 %s (%zu bytes)\n",names[i],code->GetBufferSize());
    }
    std::puts("3 production shader variants compiled. No GPU, SDK optical flow, FX or NR was executed.");
    return 0;
}

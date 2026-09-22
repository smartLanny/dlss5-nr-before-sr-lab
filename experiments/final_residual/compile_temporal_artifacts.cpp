// Build-time only: use the same embedded source, macros and flags as the
// standalone runtime oracle. This helper never loads NGX or executes a GPU.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cwchar>
#include "residual_accumulator_shader.hpp"

int wmain(int argc,wchar_t** argv) {
    if(argc!=3||std::wcslen(argv[1])!=1||argv[1][0]<L'0'||argv[1][0]>L'2')return 2;
    const unsigned variant=static_cast<unsigned>(argv[1][0]-L'0');
    const D3D_SHADER_MACRO macros[]={{"RECORDING","1"},
        {variant==2?"INPLACE_FP16":nullptr,variant==2?"1":nullptr},{nullptr,nullptr}};
    using nr::final_residual::residual_accumulator_shader;
    ID3DBlob *code=nullptr,*error=nullptr;
    const auto hr=D3DCompile(residual_accumulator_shader,sizeof(residual_accumulator_shader)-1,
        "recorded-temporal",macros,nullptr,variant==0?"CopyCandidate":"CS","cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
    if(error){std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);error->Release();}
    if(FAILED(hr)||!code){if(code)code->Release();return 3;}
    FILE* output=_wfopen(argv[2],L"wb");
    if(!output){code->Release();return 4;}
    const auto size=code->GetBufferSize();
    const bool written=std::fwrite(code->GetBufferPointer(),1,size,output)==size;
    const bool closed=std::fclose(output)==0;code->Release();
    return written&&closed?0:5;
}

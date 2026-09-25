#include "aux_shader_compile_observer.h"
#include <atomic>
#undef D3DCompile
namespace {std::atomic<unsigned long long> calls{0};}
extern "C" unsigned long long NrAuxObservedCompileCalls(){return calls.load();}
extern "C" HRESULT WINAPI NrAuxObservedCompile(LPCVOID data,SIZE_T size,LPCSTR name,const D3D_SHADER_MACRO* macros,
    ID3DInclude* include,LPCSTR entry,LPCSTR target,UINT flags,UINT flags2,ID3DBlob** code,ID3DBlob** errors){
    calls.fetch_add(1);return D3DCompile(data,size,name,macros,include,entry,target,flags,flags2,code,errors);
}

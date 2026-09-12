// Windows-only compile check for the five new utility shaders. Not executed here.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include "../src/nr_compat_present_shader.h"
int main(){
    const char* entries[]={"ConvertCS","InitCS","InitColorCS","FullscreenVS","BlitPS"};
    const char* profiles[]={"cs_5_0","cs_5_0","cs_5_0","vs_5_0","ps_5_0"};
    for(unsigned i=0;i<5;++i){ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
        HRESULT result=D3DCompile(kCompatPresentShader,sizeof(kCompatPresentShader)-1,"compat-present",
            nullptr,nullptr,entries[i],profiles[i],D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors){std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
        if(FAILED(result)||!code){std::fprintf(stderr,"%s failed 0x%08lx\n",entries[i],static_cast<unsigned long>(result));if(code)code->Release();return 1;}
        std::printf("%s %s: %zu bytes\n",entries[i],profiles[i],code->GetBufferSize());code->Release();
    }
    return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
int wmain(int argc,wchar_t** argv) {
    if(argc!=6)return 2;
    char entry[128]{};
    if(!WideCharToMultiByte(CP_UTF8,0,argv[2],-1,entry,sizeof(entry),nullptr,nullptr))return 3;
    const bool face=std::wcstol(argv[3],nullptr,10)!=0;
    const long domain=std::wcstol(argv[4],nullptr,10);
    if(domain<-1||domain>1)return 4;
    const D3D_SHADER_MACRO macros[]={{"NR_FACE_SHADER_ENABLE",face?"1":"0"},
        {"NR_DUAL_REDUCED",domain>=0?"1":"0"},{"NR_DUAL_DOMAIN",domain==1?"1":"0"},{nullptr,nullptr}};
    ID3DBlob *binary=nullptr,*errors=nullptr;
    const HRESULT hr=D3DCompileFromFile(argv[1],macros,nullptr,entry,"cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
    if(errors){std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(FAILED(hr)||!binary){std::fprintf(stderr,"Shader compilation failed: 0x%08lx\n",static_cast<unsigned long>(hr));return 5;}
    FILE* out=_wfopen(argv[5],L"wb");if(!out){binary->Release();return 6;}
    const auto size=binary->GetBufferSize();const bool ok=std::fwrite(binary->GetBufferPointer(),1,size,out)==size;
    std::fclose(out);binary->Release();
    if(!ok)return 7;
    std::printf("%s face=%d domain=%ld bytes=%zu\n",entry,face?1:0,domain,size);return 0;
}

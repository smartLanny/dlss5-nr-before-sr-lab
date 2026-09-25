#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
// Same command contract as the existing codec compiler, but finite/NaN
// validation is semantically significant for experimental residual planes.
int wmain(int argc,wchar_t** argv) {
    if(argc!=6)return 2;
    char entry[128]{};
    if(!WideCharToMultiByte(CP_UTF8,0,argv[2],-1,entry,sizeof(entry),nullptr,nullptr))return 3;
    if(std::wcstol(argv[3],nullptr,10)!=0||std::wcstol(argv[4],nullptr,10)!=-1)return 4;
    ID3DBlob *binary=nullptr,*errors=nullptr;
    const HRESULT hr=D3DCompileFromFile(argv[1],nullptr,nullptr,entry,"cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,
        0,&binary,&errors);
    if(errors){std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(FAILED(hr)||!binary){std::fprintf(stderr,"Edit shader compilation failed: 0x%08lx\n",static_cast<unsigned long>(hr));return 5;}
    FILE* out=_wfopen(argv[5],L"wb");if(!out){binary->Release();return 6;}
    const auto size=binary->GetBufferSize();const bool ok=std::fwrite(binary->GetBufferPointer(),1,size,out)==size;
    const bool closed=std::fclose(out)==0;binary->Release();
    if(!ok||!closed)return 7;
    std::printf("%s IEEE-strict SM5 bytes=%zu\n",entry,size);return 0;
}

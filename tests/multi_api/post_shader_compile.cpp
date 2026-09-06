// Compile actual staged production HLSL. No GPU execution / NGX claim.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#pragma comment(lib,"d3dcompiler.lib")
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    char entry[128]{};
    if(!WideCharToMultiByte(CP_UTF8,0,argv[2],-1,entry,sizeof(entry),nullptr,nullptr)) return 3;
    ID3DBlob* shader=nullptr;ID3DBlob* errors=nullptr;
    const HRESULT hr=D3DCompileFromFile(argv[1],nullptr,nullptr,entry,"cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors);
    if(errors) {std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(shader) shader->Release();
    if(FAILED(hr)) {std::fprintf(stderr,"%s failed: 0x%08lx\n",entry,static_cast<unsigned long>(hr));return 1;}
    std::printf("%s SM5 compile passed\n",entry);
    return 0;
}

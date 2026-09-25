#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <vector>
int wmain(int argc,wchar_t** argv) {
    if(argc!=7)return 2;
    char label[128]{},entry[128]{},target[32]{};
    if(!WideCharToMultiByte(CP_UTF8,0,argv[2],-1,label,sizeof(label),nullptr,nullptr)||
       !WideCharToMultiByte(CP_UTF8,0,argv[3],-1,entry,sizeof(entry),nullptr,nullptr)||
       !WideCharToMultiByte(CP_UTF8,0,argv[4],-1,target,sizeof(target),nullptr,nullptr))return 3;
    const unsigned flags=static_cast<unsigned>(std::wcstoul(argv[5],nullptr,10));
    FILE* input=_wfopen(argv[1],L"rb");if(!input)return 4;
    std::fseek(input,0,SEEK_END);const auto size=std::ftell(input);std::rewind(input);
    if(size<=0){std::fclose(input);return 4;}
    std::vector<char> source(static_cast<std::size_t>(size));
    const bool read=std::fread(source.data(),1,source.size(),input)==source.size();std::fclose(input);if(!read)return 4;
    ID3DBlob *binary=nullptr,*errors=nullptr;
    const auto hr=D3DCompile(source.data(),source.size(),label,nullptr,nullptr,entry,target,flags,0,&binary,&errors);
    if(errors){std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(FAILED(hr)||!binary){std::fprintf(stderr,"Shader compilation failed: 0x%08lx\n",static_cast<unsigned long>(hr));return 5;}
    FILE* output=_wfopen(argv[6],L"wb");if(!output){binary->Release();return 6;}
    const auto bytes=binary->GetBufferSize();const bool written=std::fwrite(binary->GetBufferPointer(),1,bytes,output)==bytes;
    std::fclose(output);binary->Release();if(!written)return 7;
    std::printf("%s %s flags=%u bytes=%zu\n",entry,target,flags,bytes);return 0;
}

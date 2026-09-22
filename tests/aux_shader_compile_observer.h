#pragma once
#include <windows.h>
#include <d3dcompiler.h>
extern "C" HRESULT WINAPI NrAuxObservedCompile(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
extern "C" unsigned long long NrAuxObservedCompileCalls();
#define D3DCompile NrAuxObservedCompile

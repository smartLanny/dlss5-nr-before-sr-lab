// C API spelling adapters only; all objects and methods are real D3D12.
template<class T> void ReleaseCom(T *&p){if(p)p->Release();p=nullptr;}
#define ID3D12Resource_Map(p,...) (p)->Map(__VA_ARGS__)
#define ID3D12Resource_Unmap(p,...) (p)->Unmap(__VA_ARGS__)
#define ID3D12Resource_GetDesc(p) (p)->GetDesc()
#define ID3D12DescriptorHeap_GetDesc(p) (p)->GetDesc()
#define ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(p) (p)->GetCPUDescriptorHandleForHeapStart()
#define ID3D12Device_CreateCommittedResource(p,...) (p)->CreateCommittedResource(__VA_ARGS__)
#define ID3D12Device_CreateDescriptorHeap(p,...) (p)->CreateDescriptorHeap(__VA_ARGS__)
#define ID3D12Device_CopyDescriptorsSimple(p,...) (p)->CopyDescriptorsSimple(__VA_ARGS__)
#define ID3D12Device_CreateUnorderedAccessView(p,...) (p)->CreateUnorderedAccessView(__VA_ARGS__)
#define ID3D12GraphicsCommandList_ResourceBarrier(p,...) (p)->ResourceBarrier(__VA_ARGS__)
#define ID3D12GraphicsCommandList_CopyBufferRegion(p,...) (p)->CopyBufferRegion(__VA_ARGS__)
#include "nr_f8_gpu.inl"

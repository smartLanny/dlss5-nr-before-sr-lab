// Actual Windows SDK/WARP typed SRV -> canonical texture test, not NGX/game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include "src/interop/nr_format_access.h"
#include "../ci_warp_runtime.h"
#define ID3D12Device_CheckFeatureSupport(device, feature, data, size) \
    (device)->CheckFeatureSupport(feature, data, size)
#include "src/interop/nr_format_access_d3d12.inl"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static void Check(bool value, const char* what) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", what); ExitProcess(1); }
}
static void Hr(HRESULT value, const char* what) {
    if (FAILED(value)) {
        std::fprintf(stderr, "FAIL %s HRESULT=%08lx\n", what, static_cast<unsigned long>(value));
        // Never destroy possibly queued test resources after an unproven fence.
        ExitProcess(2);
    }
}
static void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
    list->ResourceBarrier(1,&b);
}
static ComPtr<ID3D12Resource> Resource(ID3D12Device* d, D3D12_RESOURCE_DESC desc,
                                    D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES props{}; props.Type=heap;
    props.CreationNodeMask=props.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> r;
    Hr(d->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)),"CreateResource");
    return r;
}
static D3D12_RESOURCE_DESC Buffer(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes; d.Height=1;
    d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1;
    d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
static void Run(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12RootSignature* root,
                ID3D12PipelineState* pso, DXGI_FORMAT storage) {
    constexpr UINT width=7, height=5;
    D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width; d.Height=height;
    d.DepthOrArraySize=d.MipLevels=1; d.Format=storage; d.SampleDesc.Count=1;
    const auto view=NrResolveSceneSrv(device,d);
    Check(view==NrSceneReadFormat(storage),"production view selection");
    Check(nr_format_access::LegalView(storage,view),"legal typed pairing");
    auto source=Resource(device,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    // Source has NO UAV flag. The processing target is an owned canonical FP16.
    D3D12_RESOURCE_DESC target_desc=d;
    target_desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    target_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto target=Resource(device,target_desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT input_fp{}, output_fp{};
    UINT64 input_bytes=0, output_bytes=0;
    device->GetCopyableFootprints(&d,0,1,0,&input_fp,nullptr,nullptr,&input_bytes);
    device->GetCopyableFootprints(&target_desc,0,1,0,&output_fp,nullptr,nullptr,&output_bytes);
    // A typeless upload footprint uses the explicitly compatible typed layout.
    input_fp.Footprint.Format=view;
    auto upload=Resource(device,Buffer(input_bytes),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback=Resource(device,Buffer(output_bytes),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    unsigned char* bytes=nullptr; D3D12_RANGE empty{};
    Hr(upload->Map(0,&empty,reinterpret_cast<void**>(&bytes)),"Map upload");
    std::memset(bytes,0,static_cast<size_t>(input_bytes));
    for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
        const float pixel[4]={x/16.0f,y/8.0f,(x+y)/32.0f,1.0f};
        auto row=bytes+input_fp.Offset+y*input_fp.Footprint.RowPitch;
        if(view==DXGI_FORMAT_R32G32B32A32_FLOAT) std::memcpy(row+x*16,pixel,16);
        else for(UINT c=0;c<4;++c) {
            const auto half=DirectX::PackedVector::XMConvertFloatToHalf(pixel[c]);
            std::memcpy(row+x*8+c*2,&half,2);
        }
    }
    upload->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;
    hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Hr(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"descriptor heap");
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format=view;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(source.Get(),&srv,cpu);
    cpu.ptr+=device->GetDescriptorHandleIncrementSize(hd.Type);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format=target_desc.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(target.Get(),nullptr,&uav,cpu);
    ComPtr<ID3D12CommandAllocator> allocator;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso,IID_PPV_ARGS(&list)),"list");
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=input_fp;
    dst.pResource=source.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Barrier(list.Get(),source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]={heap.Get()};
    list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root);
    list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch(1,1,1);
    Barrier(list.Get(),target.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    src={};src.pResource=target.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst={};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=output_fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Hr(list->Close(),"Close");
    ComPtr<ID3D12Fence> fence;
    Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event");
    ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    Hr(queue->Signal(fence.Get(),1),"Signal");Hr(fence->SetEventOnCompletion(1,event),"Fence completion");
    Check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"real queue completion");CloseHandle(event);
    Check(fence->GetCompletedValue()!=UINT64_MAX,"device not removed");
    D3D12_RANGE range{0,static_cast<SIZE_T>(output_bytes)};
    Hr(readback->Map(0,&range,reinterpret_cast<void**>(&bytes)),"Map readback");
    for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
        const float expected[4]={x/16.0f,y/8.0f,(x+y)/32.0f,1.0f};
        for(UINT c=0;c<4;++c) {
            DirectX::PackedVector::HALF half{};
            std::memcpy(&half,bytes+output_fp.Offset+y*output_fp.Footprint.RowPitch+x*8+c*2,2);
            Check(DirectX::PackedVector::XMConvertHalfToFloat(half)==expected[c],"typed SRV canonical pixels");
        }
    }
    readback->Unmap(0,&empty);
    std::printf("storage=%u srv=%u: 140 canonical pixel checks; original source UAV not required\n",storage,view);
}
int main() {
    Hr(nr_test_warp::Prepare(),"Prepare pinned CI WARP");
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Factory");
    ComPtr<IDXGIAdapter> adapter;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP");
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Device");
    Hr(nr_test_warp::Verify("format-view"),"Verify actual CI WARP");
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Queue");
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1};
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable={2,ranges};parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&parameter;
    ComPtr<ID3DBlob> serialized,errors;
    Hr(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"Serialize root");
    ComPtr<ID3D12RootSignature> root;Hr(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)),"Root");
    constexpr char shader[]="Texture2D<float4> S:register(t0); RWTexture2D<float4> D:register(u0); [numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){uint w,h;S.GetDimensions(w,h);if(p.x<w&&p.y<h)D[p.xy]=S.Load(int3(p.xy,0));}";
    ComPtr<ID3DBlob> code;
    Hr(D3DCompile(shader,sizeof(shader)-1,"format-view",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors),"Shader");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pso;Hr(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)),"PSO");
    for(auto f:{DXGI_FORMAT_R32G32B32A32_TYPELESS,DXGI_FORMAT_R32G32B32A32_FLOAT,
                DXGI_FORMAT_R16G16B16A16_TYPELESS,DXGI_FORMAT_R16G16B16A16_FLOAT})
        Run(device.Get(),queue.Get(),root.Get(),pso.Get(),f);
    std::puts("Four WARP typed-read paths passed; no NGX/game/transport claim");
}

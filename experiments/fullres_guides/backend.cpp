// Project-owned integration of official AMD/NVIDIA APIs. No NR/SR/FG calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "backend.hpp"
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <nvOpticalFlowD3D12.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace nr::fullres {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto write = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void require(bool value, const char *what) { if (!value) throw std::runtime_error(what); }
void hr(HRESULT value, const char *what) { require(SUCCEEDED(value), what); }
void barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
             D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    list->ResourceBarrier(1, &b);
}
ComPtr<ID3D12Resource> texture(ID3D12Device *device, Extent size, DXGI_FORMAT format,
                            D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size.width; desc.Height = size.height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Format = format; desc.Flags = flags;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> result;
    hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
        nullptr, IID_PPV_ARGS(&result)), "texture allocation failed (full extent retained)");
    return result;
}
// Deliberately identical pixel coordinates: NO resampling/downsampling here.
// Motion is a block estimate expanded at its native units, not extra motion detail.
constexpr char shader[] = R"hlsl(
cbuffer Params : register(b0) { uint Width; uint Height; uint Grid; uint Reset; float Unit; };
#ifdef PREP
Texture2D<float4> Source : register(t0);
RWTexture2D<float> Luma : register(u0);
[numthreads(8,8,1)] void CS(uint3 p : SV_DispatchThreadID) {
    if (p.x >= Width || p.y >= Height) return;
    float3 c = saturate(Source.Load(int3(p.xy,0)).rgb);
    Luma[p.xy] = dot(c, float3(0.2126, 0.7152, 0.0722));
}
#else
Texture2D<int2> Raw : register(t0);
RWTexture2D<float2> Flow : register(u0);
[numthreads(8,8,1)] void CS(uint3 p : SV_DispatchThreadID) {
    if (p.x >= Width || p.y >= Height) return;
    float2 v = 0;
    if (Reset == 0) v = float2(Raw.Load(int3(p.xy / Grid,0))) * Unit;
    Flow[p.xy] = v;
}
#endif
)hlsl";
struct Pass {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT stride = 0;
    void create(ID3D12Device *device, bool prep) {
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {2, ranges};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = {0, 0, 5};
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 2; desc.pParameters = params;
        ComPtr<ID3DBlob> blob, errors;
        hr(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root signature");
        hr(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "root creation");
        D3D_SHADER_MACRO macros[] = {{"PREP", "1"}, {nullptr, nullptr}};
        hr(D3DCompile(shader, sizeof(shader)-1, "nr-fullres-guide-conversion", prep ? macros : nullptr,
            nullptr, "CS", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &errors), "conversion shader compile");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = root.Get(); pd.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
        hr(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), "conversion pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 2;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "conversion descriptors");
        stride = device->GetDescriptorHandleIncrementSize(hd.Type);
    }
    void bind(ID3D12Device *device, ID3D12Resource *src, DXGI_FORMAT src_format,
              ID3D12Resource *dst, DXGI_FORMAT dst_format) {
        auto h = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = src_format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(src, &srv, h); h.ptr += stride;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = dst_format; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(dst, nullptr, &uav, h);
    }
    void run(ID3D12GraphicsCommandList *list, Extent size, UINT grid, bool reset, float unit) {
        struct Constants { UINT w, h, grid, reset; float unit; } c{size.width,size.height,grid,reset?1u:0u,unit};
        static_assert(sizeof(c) == 20);
        ID3D12DescriptorHeap *heaps[]{heap.Get()};
        list->SetDescriptorHeaps(1, heaps); list->SetComputeRootSignature(root.Get());
        list->SetPipelineState(pso.Get());
        list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        list->SetComputeRoot32BitConstants(1,5,&c,0);
        list->Dispatch(grid_extent(size.width,8),grid_extent(size.height,8),1);
    }
};
}

struct Session::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> prep_alloc, finish_alloc;
    ComPtr<ID3D12GraphicsCommandList> prep_list, finish_list;
    ComPtr<ID3D12Fence> done, prep_done, of_done, registered, producer;
    ComPtr<ID3D12Resource> snapshot, raw, dense, scd;
    std::array<ComPtr<ID3D12Resource>,2> luma;
    Pass convert, prepare;
    Backend selected; Extent size;
    History history; Result last{};
    std::unique_ptr<FfxOpticalflowContext> amd;
    bool amd_created = false;
    std::vector<uint8_t> scratch;
    HMODULE nv_module = nullptr;
    NV_OF_D3D12_API_FUNCTION_LIST nv{};
    NvOFHandle nv_session = nullptr;
    std::array<NvOFGPUBufferHandle,2> luma_handle{};
    NvOFGPUBufferHandle raw_handle = nullptr;
    uint64_t done_value=0, producer_value=0, reg_value=0, of_value=0, prep_value=0;
    UINT grid=8, current_luma=0;
    bool ready=false, failed=false, capture_unpublished=false, execution_unpublished=false;
    bool registration_unresolved=false;
    std::string message="not initialized";
    explicit Impl(Backend b, Extent e):selected(b),size(e) {}
    bool idle() const noexcept {
        return !failed && !execution_unpublished && (!done_value || completed(done->GetCompletedValue(),done_value));
    }
    bool safe() const noexcept {
        return idle() && !capture_unpublished && !registration_unresolved &&
            (!producer_value || (producer && completed(producer->GetCompletedValue(),producer_value)));
    }
    void fault(const char *reason) noexcept {
        failed=true; ready=false;
        try { message=reason; } catch (...) {}
        OutputDebugStringA("NR fullres guide experiment bypassed: collect ReShade.log and report the selected backend.\n");
    }
    void commands(ComPtr<ID3D12CommandAllocator>& a, ComPtr<ID3D12GraphicsCommandList>& l) {
        hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)),"allocator");
        hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&l)),"command list");
        hr(l->Close(),"initial list close");
    }
    void fence(ComPtr<ID3D12Fence>& f) {
        hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)),"fence creation");
    }
    bool nv_format(NV_OF_BUFFER_USAGE usage, DXGI_FORMAT format) {
        uint32_t count=0;
        require(nv.nvOFGetSurfaceFormatCountD3D12(nv_session,usage,NV_OF_MODE_OPTICALFLOW,&count)==NV_OF_SUCCESS && count && count<=256,"NVOF format query");
        std::vector<DXGI_FORMAT> formats(count);
        require(nv.nvOFGetSurfaceFormatD3D12(nv_session,usage,NV_OF_MODE_OPTICALFLOW,formats.data())==NV_OF_SUCCESS,"NVOF format list");
        return std::find(formats.begin(),formats.end(),format)!=formats.end();
    }
    void register_nv(ID3D12Resource *r, NvOFGPUBufferHandle& handle) {
        NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 p{};
        p.resource=r; p.hOFGpuBuffer=&handle;
        p.inputFencePoint={registered.Get(),reg_value};
        p.outputFencePoint={registered.Get(),++reg_value};
        registration_unresolved=true;
        require(nv.nvOFRegisterResourceD3D12(nv_session,&p)==NV_OF_SUCCESS,"NVOF registration failed; session retained");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        require(event!=nullptr,"NVOF registration event");
        const HRESULT result=registered->SetEventOnCompletion(reg_value,event);
        const DWORD wait=SUCCEEDED(result)?WaitForSingleObject(event,2000):WAIT_FAILED;
        const bool finished=wait==WAIT_OBJECT_0 && completed(registered->GetCompletedValue(),reg_value);
        // A still-registered event is also a live GPU dependency. Never close it on timeout.
        if (finished || FAILED(result)) CloseHandle(event);
        require(finished && handle,"NVOF registration incomplete; do not release dependent resources");
        registration_unresolved=false;
    }
    void init(ID3D12Device *d) {
        require(d && size.valid() && valid_backend(selected),"invalid device/backend/extent");
        require(size.width<=16384 && size.height<=16384,"D3D12 extent limit; no automatic resize");
        // This experiment puts an explicit budget on its own planes, not total SDK VRAM.
        require(uint64_t(size.width)*size.height<=16777216ull,"full-resolution plane budget exceeded; no downsample fallback");
        device=d;
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"private same-device queue");
        commands(prep_alloc,prep_list); commands(finish_alloc,finish_list);
        fence(done); fence(prep_done); fence(of_done); fence(registered);
        snapshot=texture(d,size,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        dense=texture(d,size,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        convert.create(d,false);
        if (selected==Backend::amd) {
            D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_2};
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 caps{};
            hr(d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&sm,sizeof(sm)),"AMD OF shader model query");
            hr(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,&caps,sizeof(caps)),"AMD OF wave query");
            require(sm.HighestShaderModel>=D3D_SHADER_MODEL_6_2 && caps.WaveOps,"AMD OF requires SM6.2 and wave operations");
            require(size.width>=128 && size.height>=128,"AMD OF pyramid requires a larger source; input is not resized");
            scratch.resize(ffxGetScratchMemorySizeDX12(FFX_OPTICALFLOW_CONTEXT_COUNT));
            FfxOpticalflowContextDescription description{};
            description.resolution={size.width,size.height}; // Exact source extent, never a quality cap.
            require(ffxGetInterfaceDX12(&description.backendInterface,ffxGetDeviceDX12(d),scratch.data(),scratch.size(),FFX_OPTICALFLOW_CONTEXT_COUNT)==FFX_OK,"FFX DX12 interface");
            amd=std::make_unique<FfxOpticalflowContext>();
            require(ffxOpticalflowContextCreate(amd.get(),&description)==FFX_OK,"FFX optical flow context");
            amd_created=true;
            scd=texture(d,{3,1},DXGI_FORMAT_R32_UINT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,write);
        } else {
            nv_module=LoadLibraryExW(L"nvofapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            require(nv_module!=nullptr,"NVOF driver unavailable; selected backend not silently replaced");
            using Create=NV_OF_STATUS (NVOFAPI *)(uint32_t,NV_OF_D3D12_API_FUNCTION_LIST*);
            auto create=reinterpret_cast<Create>(GetProcAddress(nv_module,"NvOFAPICreateInstanceD3D12"));
            require(create && create(NV_OF_API_VERSION,&nv)==NV_OF_SUCCESS,"NVOF API unavailable");
            require(nv.nvCreateOpticalFlowD3D12 && nv.nvOFGetCaps && nv.nvOFInit &&
                nv.nvOFExecuteD3D12 && nv.nvOFRegisterResourceD3D12 && nv.nvOFUnregisterResourceD3D12 &&
                nv.nvOFGetSurfaceFormatCountD3D12 && nv.nvOFGetSurfaceFormatD3D12 && nv.nvOFDestroy,"incomplete NVOF function table");
            require(nv.nvCreateOpticalFlowD3D12(d,&nv_session)==NV_OF_SUCCESS && nv_session,"NVOF device/session");
            uint32_t count=0;
            require(nv.nvOFGetCaps(nv_session,NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,nullptr,&count)==NV_OF_SUCCESS && count && count<=64,"NVOF grid query");
            std::vector<uint32_t> grids(count);
            require(nv.nvOFGetCaps(nv_session,NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,grids.data(),&count)==NV_OF_SUCCESS,"NVOF grid values");
            grid=0;
            for (UINT candidate : {1u,2u,4u}) if(std::find(grids.begin(),grids.end(),candidate)!=grids.end()){grid=candidate;break;}
            require(grid!=0,"NVOF has no supported output grid");
            NV_OF_INIT_PARAMS p{};
            p.width=size.width; p.height=size.height;
            p.outGridSize=static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(grid);
            p.hintGridSize=NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
            p.mode=NV_OF_MODE_OPTICALFLOW; p.perfLevel=NV_OF_PERF_LEVEL_SLOW;
            p.enableExternalHints=NV_OF_FALSE; p.enableOutputCost=NV_OF_FALSE;
            p.disparityRange=NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
            p.enableRoi=NV_OF_FALSE; p.predDirection=NV_OF_PRED_DIRECTION_FORWARD;
            p.enableGlobalFlow=NV_OF_FALSE; p.inputBufferFormat=NV_OF_BUFFER_FORMAT_GRAYSCALE8;
            require(nv.nvOFInit(nv_session,&p)==NV_OF_SUCCESS,"NVOF rejected full extent/quality; no downsample fallback");
            require(nv_format(NV_OF_BUFFER_USAGE_INPUT,DXGI_FORMAT_R8_UNORM) &&
                nv_format(NV_OF_BUFFER_USAGE_OUTPUT,DXGI_FORMAT_R16G16_SINT),"NVOF required formats unavailable");
            prepare.create(d,true);
            for(size_t i=0;i<2;++i) {
                luma[i]=texture(d,size,DXGI_FORMAT_R8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
                register_nv(luma[i].Get(),luma_handle[i]);
            }
        }
        raw=texture(d,{grid_extent(size.width,grid),grid_extent(size.height,grid)},DXGI_FORMAT_R16G16_SINT,
            selected==Backend::amd?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE,
            selected==Backend::amd?write:D3D12_RESOURCE_STATE_COMMON);
        if(selected==Backend::nvidia) register_nv(raw.Get(),raw_handle);
        convert.bind(d,raw.Get(),DXGI_FORMAT_R16G16_SINT,dense.Get(),DXGI_FORMAT_R32G32_FLOAT);
        ready=true; message="ready; exact full-size input; no NR/SR/FG connected";
    }
    bool cleanup() noexcept {
        if(amd_created) { if(ffxOpticalflowContextDestroy(amd.get())!=FFX_OK)return false; amd_created=false; }
        if(nv_session) {
            auto remove=[&](NvOFGPUBufferHandle& h){
                if(!h)return true;
                NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 p{};p.hOFGpuBuffer=h;
                if(nv.nvOFUnregisterResourceD3D12(&p)!=NV_OF_SUCCESS)return false;
                h=nullptr;return true;
            };
            if(!remove(raw_handle))return false;
            for(auto& h:luma_handle)if(!remove(h))return false;
            if(nv.nvOFDestroy(nv_session)!=NV_OF_SUCCESS)return false;
            nv_session=nullptr;
        }
        if(nv_module) {FreeLibrary(nv_module);nv_module=nullptr;}
        return true;
    }
};

Session::Session(ID3D12Device *device, Backend b, Extent size):p_(std::make_unique<Impl>(b,size)) {
    try {p_->init(device);} catch(const std::exception& e){p_->message=e.what();p_->ready=false;}
}
Session::~Session() {
    if(!p_) return;
    if(!p_->safe()) {p_->fault("retirement not proven; resources retained until process exit"); (void)p_.release(); return;}
    if(!p_->cleanup()){p_->fault("SDK retirement rejected; session retained");(void)p_.release();}
}
bool Session::initialized() const noexcept{return p_ && p_->ready;}
bool Session::idle() const noexcept{return p_ && p_->idle();}
bool Session::safe_to_destroy() const noexcept{return p_ && p_->safe();}
bool Session::faulted() const noexcept{return !p_ || p_->failed;}
const char* Session::status() const noexcept{return p_?p_->message.c_str():"no session";}
Backend Session::backend() const noexcept{return p_->selected;}
Extent Session::extent() const noexcept{return p_->size;}
ID3D12Resource* Session::snapshot() const noexcept{return p_->snapshot.Get();}
void Session::mark_capture_recorded() noexcept {p_->capture_unpublished=true;}
void Session::poison(const char *reason) noexcept {p_->fault(reason);}
bool Session::result(Result &out) const noexcept {
    if(!initialized() || !idle() || !p_->last.current) return false;
    out=p_->last;return true;
}
bool Session::submit(ID3D12Fence *producer,uint64_t value,const Frame& frame) noexcept {
    auto& p=*p_;
    if(!initialized() || !p.idle() || !producer || !value || value==UINT64_MAX || !p.capture_unpublished) return false;
    const auto pair=p.history.plan(frame,p.selected);
    if(!pair.accepted || !(frame.extent==p.size)) {p.fault("invalid source frame or pair");return false;}
    // The published game signal also retires any read of our previous output.
    p.producer=producer;p.producer_value=value;p.capture_unpublished=false;
    try {
        hr(p.queue->Wait(producer,value),"source dependency wait failed");
        hr(p.prep_alloc->Reset(),"prep allocator reset");
        hr(p.prep_list->Reset(p.prep_alloc.Get(),nullptr),"prep list reset");
        auto* list=p.prep_list.Get();
        barrier(list,p.snapshot.Get(),D3D12_RESOURCE_STATE_COPY_DEST,read);
        if(p.selected==Backend::amd) {
            FfxOpticalflowDispatchDescription job{};
            job.commandList=ffxGetCommandListDX12(list);
            job.color=ffxGetResourceDX12(p.snapshot.Get(),ffxGetResourceDescriptionDX12(p.snapshot.Get()),L"Full-size SDR source",FFX_RESOURCE_STATE_COMPUTE_READ);
            job.opticalFlowVector=ffxGetResourceDX12(p.raw.Get(),ffxGetResourceDescriptionDX12(p.raw.Get()),L"AMD block flow",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            job.opticalFlowSCD=ffxGetResourceDX12(p.scd.Get(),ffxGetResourceDescriptionDX12(p.scd.Get()),L"AMD scene cut",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            job.reset=pair.reset;job.backbufferTransferFunction=0;job.minMaxLuminance={0,1};
            require(ffxOpticalflowContextDispatch(p.amd.get(),&job)==FFX_OK,"AMD OF dispatch failed; session latched");
        } else {
            const auto i=p.current_luma;
            p.prepare.bind(p.device.Get(),p.snapshot.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,p.luma[i].Get(),DXGI_FORMAT_R8_UNORM);
            barrier(list,p.luma[i].Get(),D3D12_RESOURCE_STATE_COMMON,write);
            p.prepare.run(list,p.size,1,false,1);
            barrier(list,p.luma[i].Get(),write,D3D12_RESOURCE_STATE_COMMON);
        }
        barrier(list,p.snapshot.Get(),read,D3D12_RESOURCE_STATE_COPY_DEST);
        hr(list->Close(),"prep close");
        ID3D12CommandList* lists[]{list};
        p.execution_unpublished=true;
        p.queue->ExecuteCommandLists(1,lists);
        hr(p.queue->Signal(p.prep_done.Get(),++p.prep_value),"prep signal failed; resources retained");
        if(p.selected==Backend::nvidia && !pair.reset) {
            NV_OF_FENCE_POINT in_fence{p.prep_done.Get(),p.prep_value};
            NV_OF_FENCE_POINT out_fence{p.of_done.Get(),++p.of_value};
            NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
            in.inputFrame=p.luma_handle[p.current_luma];in.referenceFrame=p.luma_handle[1-p.current_luma];
            // Always disable vendor hints in the first A/B experiment; explicit reference is authoritative.
            in.disableTemporalHints=NV_OF_TRUE;in.numFencePoints=1;in.fencePoint=&in_fence;
            NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};out.outputBuffer=p.raw_handle;out.fencePoint=&out_fence;
            require(p.nv.nvOFExecuteD3D12(p.nv_session,&in,&out)==NV_OF_SUCCESS,"NVOF execution failed; resources retained");
            hr(p.queue->Wait(p.of_done.Get(),p.of_value),"NVOF completion dependency failed");
        }
        hr(p.finish_alloc->Reset(),"finish allocator reset");
        hr(p.finish_list->Reset(p.finish_alloc.Get(),nullptr),"finish list reset");
        list=p.finish_list.Get();
        const auto raw_state=p.selected==Backend::amd?write:D3D12_RESOURCE_STATE_COMMON;
        barrier(list,p.raw.Get(),raw_state,read);
        barrier(list,p.dense.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,write);
        p.convert.run(list,p.size,p.grid,pair.reset,p.selected==Backend::amd?1.0f:1.0f/32.0f);
        barrier(list,p.dense.Get(),write,D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(list,p.raw.Get(),read,raw_state);
        hr(list->Close(),"finish close");lists[0]=list;
        p.queue->ExecuteCommandLists(1,lists);
        const uint64_t completion=p.done_value+1;
        hr(p.queue->Signal(p.done.Get(),completion),"completion signal failed; resources retained");
        p.done_value=completion;p.execution_unpublished=false;
        require(p.history.commit(frame,p.selected,true),"history commit failed");
        p.last.flow=p.dense.Get();
        p.last.current=frame.sequence;p.last.reference=pair.reference;p.last.reset=pair.reset;p.last.grid=p.grid;
        if(p.selected==Backend::nvidia)p.current_luma=1-p.current_luma;
        p.message="submitted; wait for real GPU completion before preview (not NR)";
        return true;
    }catch(const std::exception& e){p.fault(e.what());return false;}
    catch(...){p.fault("unexpected provider failure");return false;}
}
} // namespace nr::fullres

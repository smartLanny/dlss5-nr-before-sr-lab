// Project-owned integration of official AMD/NVIDIA APIs and the stable Core V1
// producer seam. The addon never creates its own NGX SR/NR feature.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef NR_OF_ENABLE_NVOF
#define NR_OF_ENABLE_NVOF 1
#endif
#include "backend.hpp"
#include "shader_source.hpp"
#include "nr_external_provider_feeder_map.h"
#include "nr_external_provider_producer.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#if NR_OF_ENABLE_NVOF
#include <nvOpticalFlowD3D12.h>
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace nr::fullres {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto write = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void require(bool value, const char *what) { if (!value) throw std::runtime_error(what); }
void hr(HRESULT value, const char *what) {
    if (SUCCEEDED(value)) return;
    char message[256]{};
    std::snprintf(message, sizeof(message), "%s (HRESULT 0x%08lX)", what, static_cast<unsigned long>(value));
    throw std::runtime_error(message);
}
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
struct Pass {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT stride = 0;
    bool preparation = false;
    void create(ID3D12Device *device, bool prep, bool rgba = false) {
        preparation = prep;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {2, ranges};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = {0, 0, 8};
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 2; desc.pParameters = params;
        ComPtr<ID3DBlob> blob, errors;
        hr(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root signature");
        hr(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "root creation");
        D3D_SHADER_MACRO macros[] = {{"PREP", "1"}, {rgba ? "PREP_RGBA" : nullptr, "1"}, {nullptr, nullptr}};
        hr(D3DCompile(guide_shader, sizeof(guide_shader)-1, "nr-fullres-guide-conversion", prep ? macros : nullptr,
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
    void run(ID3D12GraphicsCommandList *list, Geometry geometry, Backend backend, UINT grid, bool reset) {
        const auto c = shader_constants(geometry, backend, grid, reset);
        const auto size = preparation ? geometry.flow : geometry.source;
        ID3D12DescriptorHeap *heaps[]{heap.Get()};
        list->SetDescriptorHeaps(1, heaps); list->SetComputeRootSignature(root.Get());
        list->SetPipelineState(pso.Get());
        list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        list->SetComputeRoot32BitConstants(1,8,&c,0);
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
    ComPtr<ID3D12Resource> snapshot, depth_snapshot, output, raw, dense, scd, amd_input;
    std::array<ComPtr<ID3D12Resource>,2> luma;
    Pass convert, prepare;
    Backend selected; Extent size; FlowScale requested_scale; Geometry dimensions;
    History history; Result last{};
    Frame pending_source{}; Pair pending_pair{};
    bool pending_writeback=false, force_reset=true;
    std::unique_ptr<FfxOpticalflowContext> amd;
    bool amd_created = false;
    std::vector<uint8_t> scratch;
    HMODULE nv_module = nullptr;
#if NR_OF_ENABLE_NVOF
    NV_OF_D3D12_API_FUNCTION_LIST nv{};
    NvOFHandle nv_session = nullptr;
    std::array<NvOFGPUBufferHandle,2> luma_handle{};
    NvOFGPUBufferHandle raw_handle = nullptr;
#endif
    uint64_t done_value=0, producer_value=0, reg_value=0, of_value=0, prep_value=0;
    UINT grid=8, current_luma=0;
    uint64_t submitted_at=0;
    static constexpr uint64_t timeout_ms=5000;
    bool ready=false, failed=false, capture_unpublished=false, execution_unpublished=false;
    bool registration_unresolved=false;
    std::string message="not initialized";
    explicit Impl(Backend b, Extent e, FlowScale scale):selected(b),size(e),requested_scale(scale),dimensions(geometry(e,scale)) {}
    bool idle() const noexcept {
        return !failed && !execution_unpublished && (!done_value || completed(done->GetCompletedValue(),done_value));
    }
    bool safe() const noexcept {
        return idle() && !pending_source.valid() && !capture_unpublished && !registration_unresolved &&
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
#if NR_OF_ENABLE_NVOF
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
#endif
    void init(ID3D12Device *d) {
        require(d && dimensions.valid() && valid_backend(selected) && valid_scale(requested_scale),"invalid device/backend/extent");
        require(size.width<=16384 && size.height<=16384,"D3D12 extent limit; no automatic resize");
        // This experiment puts an explicit budget on its own planes, not total SDK VRAM.
        require(uint64_t(size.width)*size.height<=16777216ull,"full-resolution plane budget exceeded; no downsample fallback");
        device=d;
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"private same-device queue");
        commands(prep_alloc,prep_list); commands(finish_alloc,finish_list);
        fence(done); fence(prep_done); fence(of_done); fence(registered);
        snapshot=texture(d,size,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        depth_snapshot=texture(d,size,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        output=texture(d,size,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        dense=texture(d,size,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        convert.create(d,false);
        if (selected==Backend::amd) {
            D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_2};
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 caps{};
            hr(d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&sm,sizeof(sm)),"AMD OF shader model query");
            hr(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,&caps,sizeof(caps)),"AMD OF wave query");
            require(sm.HighestShaderModel>=D3D_SHADER_MODEL_6_2 && caps.WaveOps,"AMD OF requires SM6.2 and wave operations");
            require(dimensions.flow.width>=128 && dimensions.flow.height>=128,"Selected AMD OF input is below the probe minimum; no automatic scale change");
            scratch.resize(ffxGetScratchMemorySizeDX12(FFX_OPTICALFLOW_CONTEXT_COUNT));
            FfxOpticalflowContextDescription description{};
            description.resolution={dimensions.flow.width,dimensions.flow.height}; // Only the explicit 100/50 selection.
            require(ffxGetInterfaceDX12(&description.backendInterface,ffxGetDeviceDX12(d),scratch.data(),scratch.size(),FFX_OPTICALFLOW_CONTEXT_COUNT)==FFX_OK,"FFX DX12 interface");
            amd=std::make_unique<FfxOpticalflowContext>();
            require(ffxOpticalflowContextCreate(amd.get(),&description)==FFX_OK,"FFX optical flow context");
            amd_created=true;
            if (requested_scale == FlowScale::half) {
                amd_input=texture(d,dimensions.flow,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,write);
                prepare.create(d,true,true);
                prepare.bind(d,snapshot.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,amd_input.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
            }
            scd=texture(d,{3,1},DXGI_FORMAT_R32_UINT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,write);
        } else {
#if NR_OF_ENABLE_NVOF
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
            p.width=dimensions.flow.width; p.height=dimensions.flow.height;
            p.outGridSize=static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(grid);
            p.hintGridSize=NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
            p.mode=NV_OF_MODE_OPTICALFLOW; p.perfLevel=NV_OF_PERF_LEVEL_SLOW;
            p.enableExternalHints=NV_OF_FALSE; p.enableOutputCost=NV_OF_FALSE;
            p.disparityRange=NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
            p.enableRoi=NV_OF_FALSE; p.predDirection=NV_OF_PRED_DIRECTION_FORWARD;
            p.enableGlobalFlow=NV_OF_FALSE; p.inputBufferFormat=NV_OF_BUFFER_FORMAT_GRAYSCALE8;
            require(nv.nvOFInit(nv_session,&p)==NV_OF_SUCCESS,"NVOF rejected selected extent/quality; no automatic scale change");
            require(nv_format(NV_OF_BUFFER_USAGE_INPUT,DXGI_FORMAT_R8_UNORM) &&
                nv_format(NV_OF_BUFFER_USAGE_OUTPUT,DXGI_FORMAT_R16G16_SINT),"NVOF required formats unavailable");
            prepare.create(d,true);
            for(size_t i=0;i<2;++i) {
                luma[i]=texture(d,dimensions.flow,DXGI_FORMAT_R8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
                register_nv(luma[i].Get(),luma_handle[i]);
            }
#else
            require(false,"NVOF backend was not compiled; install the official NVIDIA Optical Flow SDK headers and rebuild");
#endif
        }
        raw=texture(d,{grid_extent(dimensions.flow.width,grid),grid_extent(dimensions.flow.height,grid)},DXGI_FORMAT_R16G16_SINT,
            selected==Backend::amd?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE,
            selected==Backend::amd?write:D3D12_RESOURCE_STATE_COMMON);
        if(selected==Backend::nvidia) {
#if NR_OF_ENABLE_NVOF
            register_nv(raw.Get(),raw_handle);
#endif
        }
        convert.bind(d,raw.Get(),DXGI_FORMAT_R16G16_SINT,dense.Get(),DXGI_FORMAT_R32G32_FLOAT);
        ready=true; message="ready; explicit 100/50 OF input; Core V1 writeback connected";
    }
    bool cleanup() noexcept {
        if(amd_created) { if(ffxOpticalflowContextDestroy(amd.get())!=FFX_OK)return false; amd_created=false; }
#if NR_OF_ENABLE_NVOF
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
#endif
        if(nv_module) {FreeLibrary(nv_module);nv_module=nullptr;}
        return true;
    }
};

Session::Session(ID3D12Device *device, Backend b, Extent size, FlowScale scale):p_(std::make_unique<Impl>(b,size,scale)) {
    try {p_->init(device);} catch(const std::exception& e){p_->message=e.what();p_->ready=false;}
}
Session::~Session() {
    if(!p_) return;
    if(!p_->safe()) {p_->fault("retirement not proven; resources retained until process exit"); (void)p_.release(); return;}
    if(!p_->cleanup()){p_->fault("SDK retirement rejected; session retained");(void)p_.release();}
}
bool Session::initialized() const noexcept{return p_ && p_->ready;}
bool Session::idle() const noexcept{return p_ && p_->idle();}
Progress Session::poll() noexcept {
    auto& p=*p_;
    if(p.failed || !p.ready)return Progress::failed;
    const auto value=p.done?p.done->GetCompletedValue():0;
    const auto progress=fence_progress(p.done_value,value,p.submitted_at,GetTickCount64(),Impl::timeout_ms);
    if(progress==Progress::removed)p.fault("OF device removed; resources quarantined. Collect a BUG report.");
    if(progress==Progress::timed_out)p.fault("OF completion exceeded 5000 ms; session stopped, resources retained. Collect a BUG report.");
    if(progress==Progress::ready && p.pending_source.valid()) {
        if(p.pending_writeback && p.history.commit(p.pending_source,p.selected,true)) {
            p.last.flow=p.dense.Get();p.last.source=p.pending_source;
            p.last.current=p.pending_source.sequence;p.last.reference=p.pending_pair.reference;
            p.last.reset=p.pending_pair.reset;p.last.grid=p.grid;
            if(p.selected==Backend::nvidia)p.current_luma=1-p.current_luma;
            p.force_reset=false;
            p.message="Core writeback reached the declared GPU completion fence";
        } else {
            // A recorded OF pair which Core did not adopt is not temporal history.
            // AMD's opaque context is reset before its next accepted candidate;
            // NVOF keeps the last accepted luma in the untouched opposite slot.
            p.force_reset=true;
            p.message="Core retained the original frame; OF history not advanced";
        }
        p.pending_source={};p.pending_pair={};p.pending_writeback=false;
    }
    return progress;
}
bool Session::safe_to_destroy() const noexcept{return p_ && p_->safe();}
bool Session::faulted() const noexcept{return !p_ || p_->failed;}
const char* Session::status() const noexcept{return p_?p_->message.c_str():"no session";}
Backend Session::backend() const noexcept{return p_->selected;}
Extent Session::extent() const noexcept{return p_->size;}
Extent Session::flow_extent() const noexcept{return p_->dimensions.flow;}
FlowScale Session::scale() const noexcept{return p_->requested_scale;}
ID3D12Resource* Session::snapshot() const noexcept{return p_->snapshot.Get();}
ID3D12Resource* Session::depth_snapshot() const noexcept{return p_->depth_snapshot.Get();}
void Session::mark_capture_recorded() noexcept {p_->capture_unpublished=true;}
void Session::poison(const char *reason) noexcept {p_->fault(reason);}
bool Session::result(const Frame &view, Result &out) const noexcept {
    out={};
    if(!initialized() || !idle() || !preview_matches(p_->last.source,view)) return false;
    out=p_->last;return true;
}
bool Session::submit(ID3D12Fence *producer,uint64_t value,const Frame& frame,
                     bool depth_inverted,Delivery& delivery,ID3D12Resource* destination,
                     D3D12_RESOURCE_STATES destination_state) noexcept {
    delivery={};
    auto& p=*p_;
    if(!initialized() || !p.idle() || !producer || !value || value==UINT64_MAX || !p.capture_unpublished) return false;
    if(!destination)destination=p.output.Get();
    const auto target=destination?destination->GetDesc():D3D12_RESOURCE_DESC{};
    if(!destination||target.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||target.Width!=p.size.width||
       target.Height!=p.size.height||target.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||target.MipLevels!=1||
       target.DepthOrArraySize!=1||target.SampleDesc.Count!=1||
       (target.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)==0)return false;
    auto pair=p.history.plan(frame,p.selected);
    if(!pair.accepted || !(frame.extent==p.size)) {p.fault("invalid source frame or pair");return false;}
    if(p.done_value>=UINT64_MAX-1 || p.prep_value>=UINT64_MAX-1 || p.of_value>=UINT64_MAX-1) {
        p.fault("OF fence sequence exhausted; restart the session safely.");return false;
    }
    // The published game signal also retires any read of our previous output.
    p.producer=producer;p.producer_value=value;p.capture_unpublished=false;
    try {
        pair.reset=pair.reset||p.force_reset;
        if(pair.reset)pair.reference=0;
        hr(p.queue->Wait(producer,value),"source dependency wait failed");
        hr(p.prep_alloc->Reset(),"prep allocator reset");
        hr(p.prep_list->Reset(p.prep_alloc.Get(),nullptr),"prep list reset");
        auto* list=p.prep_list.Get();
        barrier(list,p.snapshot.Get(),D3D12_RESOURCE_STATE_COPY_DEST,read);
        if(p.selected==Backend::amd) {
            auto* input=p.snapshot.Get();
            if(p.amd_input) {
                p.prepare.run(list,p.dimensions,p.selected,1,false);
                barrier(list,p.amd_input.Get(),write,read);
                input=p.amd_input.Get();
            }
            FfxOpticalflowDispatchDescription job{};
            job.commandList=ffxGetCommandListDX12(list);
            job.color=ffxGetResourceDX12(input,ffxGetResourceDescriptionDX12(input),L"Selected OF SDR input",FFX_RESOURCE_STATE_COMPUTE_READ);
            job.opticalFlowVector=ffxGetResourceDX12(p.raw.Get(),ffxGetResourceDescriptionDX12(p.raw.Get()),L"AMD block flow",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            job.opticalFlowSCD=ffxGetResourceDX12(p.scd.Get(),ffxGetResourceDescriptionDX12(p.scd.Get()),L"AMD scene cut",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            job.reset=pair.reset;job.backbufferTransferFunction=0;job.minMaxLuminance={0,1};
            require(ffxOpticalflowContextDispatch(p.amd.get(),&job)==FFX_OK,"AMD OF dispatch failed; session latched");
            if(p.amd_input)barrier(list,p.amd_input.Get(),read,write);
        } else {
#if NR_OF_ENABLE_NVOF
            const auto i=p.current_luma;
            p.prepare.bind(p.device.Get(),p.snapshot.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,p.luma[i].Get(),DXGI_FORMAT_R8_UNORM);
            barrier(list,p.luma[i].Get(),D3D12_RESOURCE_STATE_COMMON,write);
            p.prepare.run(list,p.dimensions,p.selected,1,false);
            barrier(list,p.luma[i].Get(),write,D3D12_RESOURCE_STATE_COMMON);
#else
            require(false,"NVOF backend not compiled");
#endif
        }
        barrier(list,p.snapshot.Get(),read,D3D12_RESOURCE_STATE_COPY_DEST);
        hr(list->Close(),"prep close");
        ID3D12CommandList* lists[]{list};
        p.execution_unpublished=true;
        p.queue->ExecuteCommandLists(1,lists);
        hr(p.queue->Signal(p.prep_done.Get(),++p.prep_value),"prep signal failed; resources retained");
        if(p.selected==Backend::nvidia && !pair.reset) {
#if NR_OF_ENABLE_NVOF
            NV_OF_FENCE_POINT in_fence{p.prep_done.Get(),p.prep_value};
            NV_OF_FENCE_POINT out_fence{p.of_done.Get(),++p.of_value};
            NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
            in.inputFrame=p.luma_handle[p.current_luma];in.referenceFrame=p.luma_handle[1-p.current_luma];
            // Always disable vendor hints in the first A/B experiment; explicit reference is authoritative.
            in.disableTemporalHints=NV_OF_TRUE;in.numFencePoints=1;in.fencePoint=&in_fence;
            NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};out.outputBuffer=p.raw_handle;out.fencePoint=&out_fence;
            require(p.nv.nvOFExecuteD3D12(p.nv_session,&in,&out)==NV_OF_SUCCESS,"NVOF execution failed; resources retained");
            hr(p.queue->Wait(p.of_done.Get(),p.of_value),"NVOF completion dependency failed");
#else
            require(false,"NVOF backend not compiled");
#endif
        }
        hr(p.finish_alloc->Reset(),"finish allocator reset");
        hr(p.finish_list->Reset(p.finish_alloc.Get(),nullptr),"finish list reset");
        list=p.finish_list.Get();
        const auto raw_state=p.selected==Backend::amd?write:D3D12_RESOURCE_STATE_COMMON;
        barrier(list,p.raw.Get(),raw_state,read);
        barrier(list,p.dense.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,write);
        p.convert.run(list,p.dimensions,p.selected,p.grid,pair.reset);
        barrier(list,p.dense.Get(),write,D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(list,p.raw.Get(),read,raw_state);
        const uint64_t completion=p.done_value+1;
        nr_external_provider_feeder::FeedInputs inputs{};
        inputs.command_list=reinterpret_cast<uint64_t>(list);
        inputs.color=reinterpret_cast<uint64_t>(p.snapshot.Get());
        inputs.output=reinterpret_cast<uint64_t>(destination);
        inputs.depth=reinterpret_cast<uint64_t>(p.depth_snapshot.Get());
        inputs.motion=reinterpret_cast<uint64_t>(p.dense.Get());
        inputs.width=p.size.width;inputs.height=p.size.height;
        inputs.depth_width=p.size.width;inputs.depth_height=p.size.height;
        inputs.motion_width=p.size.width;inputs.motion_height=p.size.height;
        inputs.color_format=DXGI_FORMAT_R8G8B8A8_UNORM;
        inputs.depth_format=DXGI_FORMAT_R32_FLOAT;
        inputs.motion_format=DXGI_FORMAT_R32G32_FLOAT;
        inputs.color_state=D3D12_RESOURCE_STATE_COPY_DEST;
        inputs.output_state=static_cast<uint32_t>(destination_state);
        inputs.depth_state=D3D12_RESOURCE_STATE_COPY_DEST;
        inputs.motion_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
        inputs.mv_scale_x=inputs.mv_scale_y=1.0f;
        inputs.motion_unit=NrExternalMotionUnit::Pixels;
        inputs.reset=pair.reset;inputs.depth_inverted=depth_inverted;
        inputs.view_id=frame.stream;inputs.epoch=frame.epoch;inputs.frame_id=frame.sequence;
        inputs.provider_id=kNrExternalProviderIdDl5f;
        inputs.queue=reinterpret_cast<uint64_t>(p.queue.Get());
        inputs.fence=reinterpret_cast<uint64_t>(p.done.Get());
        inputs.fence_value=completion;
        NrExternalFrameV1 external{};NrExternalSubmitResultV1 result{};
        const bool mapped=nr_external_provider_feeder::BuildSyntheticFrame(inputs,external);
        const bool accepted=mapped&&nr_external_provider_client::Submit(external,result);
        const bool writeback=accepted&&result.writeback_performed!=0;
        hr(list->Close(),"finish close");lists[0]=list;
        p.queue->ExecuteCommandLists(1,lists);
        hr(p.queue->Signal(p.done.Get(),completion),"completion signal failed; resources retained");
        p.done_value=completion;p.submitted_at=GetTickCount64();p.execution_unpublished=false;
        p.pending_source=frame;p.pending_pair=pair;p.pending_writeback=writeback;
        delivery={destination,p.done.Get(),completion,frame,writeback};
        p.message=writeback?"Core writeback recorded; exact GPU completion pending":
            (accepted?"Core admitted identity; original frame retained":"Core declined this OF frame; original frame retained");
        return true;
    }catch(const std::exception& e){p.fault(e.what());return false;}
    catch(...){p.fault("unexpected provider failure");return false;}
}
} // namespace nr::fullres

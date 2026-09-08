#pragma once
// Opt-in controlled NVIDIA sample adapter. No game hooks or alternate presenter.
// The host owns the frame, NVRHI command list and exact graphics-queue fence.
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <nvrhi/nvrhi.h>
#include <donut/core/log.h>
#include <sl_core_api.h>
#include <cmath>
#include "nr_external_provider_producer.h"
#include "nr_external_provider_completion.h"
#pragma comment(lib, "d3dcompiler.lib")

// The controlled renderer exports registration only, as the existing Core host
// does. It never invokes the Core's ReShade UI callbacks or claims a game session.
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, unsigned v) { return v == 18; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
extern "C" __declspec(dllexport) const void* ReShadeGetImGuiFunctionTable(unsigned) {
    static const void* table[1024]{}; return table;
}

namespace nr_fg_sample {
using Microsoft::WRL::ComPtr;
[[noreturn]] inline void Fail(const char* stage) {
    // A failed opaque recording/submitted fence cannot license NVRHI teardown
    // or native SR reuse. This bounded host exits; it never catches/retries AV.
    donut::log::error("NR_FG_CHAIN fatal=%s end=1", stage);
    ExitProcess(73);
}
struct Chain {
    HMODULE core = nullptr; // Retained through process exit while hooks exist.
    nr_external_provider_client::QueryFn query = nullptr;
    nr_external_provider_client::ReleaseFn release = nullptr;
    nr_external_provider_client::SubmitFn submit = nullptr;
    ComPtr<ID3D12Device> native_device;
    ComPtr<ID3D12Device> host_device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    UINT64 completed = 0, epoch = 1, resource_generation = 0, last_frame = UINT64_MAX;
    unsigned width = 0, height = 0;
    bool owner = false, frame_recorded = false, requested = false;
    nvrhi::TextureHandle color, depth, motion, output;
    nvrhi::BindingLayoutHandle depth_layout;
    nvrhi::ShaderHandle depth_shader;
    nvrhi::ComputePipelineHandle depth_pipeline;
    nvrhi::BindingSetHandle depth_binding;
    nvrhi::ITexture* source_depth = nullptr;
    const void* renderer_identity = nullptr;
    template<class T> static ComPtr<T> Native(T* value) {
        // Fixed SL 2.12, serial render thread, forceProxies and engine-allocation
        // tracking disabled by the controlled launcher. This API returns AddRef.
        void* base = nullptr;
        if (!value || slGetNativeInterface(value, &base) != sl::Result::eOk || !base)
            Fail("sl-native-interface");
        ComPtr<T> result; result.Attach(static_cast<T*>(base)); return result;
    }

    void Init(nvrhi::IDevice* device, const void* renderer) {
        if (requested) {
            if (renderer != renderer_identity) Fail("multiple-sample-views");
            NrExternalStatusV1 status{};
            if (!nr_external_provider_client::WaitForNativeObserver(query, 3000, status))
                Fail("observer-before-sr-options");
            return;
        }
        wchar_t path[32768]{};
        const DWORD count = GetEnvironmentVariableW(L"NR_FG_SAMPLE_CORE", path, 32768);
        if (!count) return;
        requested = true;
        renderer_identity = renderer;
        if (count >= 32768 || !device || device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
            Fail("unsupported-host");
        host_device = static_cast<ID3D12Device*>(device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
        native_device = Native(host_device.Get());
        queue = Native(static_cast<ID3D12CommandQueue*>(device->getNativeQueue(
            nvrhi::ObjectTypes::D3D12_CommandQueue, nvrhi::CommandQueue::Graphics)));
        if (!native_device || !queue || FAILED(native_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&fence)))) Fail("producer-fence-create");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) Fail("producer-event-create");
        core = LoadLibraryW(path);
        if (!core) Fail("core-load");
        query = reinterpret_cast<nr_external_provider_client::QueryFn>(GetProcAddress(core, "NRExternalProvider_QueryV1"));
        auto claim = reinterpret_cast<nr_external_provider_client::ClaimFn>(GetProcAddress(core, "NRExternalProvider_ClaimOwnershipV1"));
        release = reinterpret_cast<nr_external_provider_client::ReleaseFn>(GetProcAddress(core, "NRExternalProvider_ReleaseOwnershipV1"));
        submit = reinterpret_cast<nr_external_provider_client::SubmitFn>(GetProcAddress(core, "NRExternalProvider_SubmitFrameV1"));
        NrExternalStatusV1 status{};
        if (!claim || !release || !submit ||
            !nr_external_provider_client::WaitForNativeObserver(query, 3000, status))
            Fail("native-observer-preflight");
        if (!claim(kNrExternalProviderAbi, sizeof(status), kNrExternalProviderIdDl5f, &status))
            Fail("single-nr-owner-claim");
        owner = true;
        donut::log::info("NR_FG_CHAIN initialized=1 owner=external coreState=%s registration=host-stub end=1",
            NrExternalProviderReasonName(status.reason));
    }
    nvrhi::TextureHandle Texture(nvrhi::IDevice* device, nvrhi::Format format, const char* name) {
        nvrhi::TextureDesc desc;
        desc.width = width; desc.height = height; desc.format = format;
        desc.isUAV = true; desc.debugName = name;
        desc.initialState = nvrhi::ResourceStates::Common; desc.keepInitialState = true;
        auto result = device->createTexture(desc);
        if (!result) Fail("compact-texture-create");
        return result;
    }
    void Prepare(nvrhi::IDevice* device, unsigned w, unsigned h, nvrhi::ITexture* input_depth) {
        if (!w || !h || w > 4096 || h > 4096 || UINT64(w)*h*24 > 256ull*1024*1024)
            Fail("compact-input-budget");
        if (frame_recorded || fence->GetCompletedValue() != completed) Fail("unretired-input-reuse");
        // One serial scratch set. Allocation changes only after its actual queue
        // fence, never on Present count or a timed-out wait.
        if (w != width || h != height) {
            depth_binding = nullptr; source_depth = nullptr;
            width = w; height = h; ++resource_generation;
            color = Texture(device, nvrhi::Format::RGBA16_FLOAT, "NR.Sample.Color");
            depth = Texture(device, nvrhi::Format::R32_FLOAT, "NR.Sample.Depth");
            motion = Texture(device, nvrhi::Format::RG16_FLOAT, "NR.Sample.Motion");
            output = Texture(device, nvrhi::Format::RGBA16_FLOAT, "NR.Sample.Output");
        }
        if (!depth_pipeline) {
            static constexpr char shader[] =
                "Texture2D<float> source : register(t0); RWTexture2D<float> dest : register(u0);"
                "[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){"
                "uint w,h; dest.GetDimensions(w,h); if(p.x<w&&p.y<h) dest[p.xy]=source.Load(int3(p.xy,0));}";
            ComPtr<ID3DBlob> code, errors;
            if (FAILED(D3DCompile(shader, sizeof(shader)-1, "NR.Sample.Depth", nullptr, nullptr,
                    "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors)))
                Fail("depth-conversion-compile");
            nvrhi::ShaderDesc sd; sd.shaderType = nvrhi::ShaderType::Compute;
            depth_shader = device->createShader(sd, code->GetBufferPointer(), code->GetBufferSize());
            nvrhi::BindingLayoutDesc ld;
            ld.visibility = nvrhi::ShaderType::Compute;
            ld.bindings = { nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Texture_UAV(0) };
            depth_layout = device->createBindingLayout(ld);
            nvrhi::ComputePipelineDesc pd;
            pd.CS = depth_shader; pd.bindingLayouts = { depth_layout };
            depth_pipeline = device->createComputePipeline(pd);
            if (!depth_shader || !depth_layout || !depth_pipeline) Fail("depth-conversion-pipeline");
        }
        if (input_depth != source_depth) {
            nvrhi::BindingSetDesc bd;
            bd.bindings = { nvrhi::BindingSetItem::Texture_SRV(0, input_depth),
                            nvrhi::BindingSetItem::Texture_UAV(0, depth) };
            depth_binding = device->createBindingSet(bd, depth_layout);
            if (!depth_binding) Fail("depth-conversion-binding");
            source_depth = input_depth;
        }
    }
    static NrExternalResourceV1 Resource(nvrhi::ITexture* texture, D3D12_RESOURCE_STATES state) {
        auto* native = static_cast<ID3D12Resource*>(texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource));
        const auto desc = native->GetDesc();
        NrExternalResourceV1 value{};
        value.resource = reinterpret_cast<UINT64>(native); value.format = desc.Format;
        value.width = value.rect_w = static_cast<unsigned>(desc.Width);
        value.height = value.rect_h = desc.Height;
        value.state_before = value.state_after = state;
        value.provenance = NrGuideProvenance::Extracted;
        return value;
    }
    void BeforeTags(nvrhi::ICommandList* list, std::initializer_list<nvrhi::ITexture*> textures) {
        if (!requested) return;
        // eOnlyValidNow depth tags can copy immediately. Restore the state that
        // the fixed SL wrapper declares before tagging, not only before Evaluate.
        for (auto* texture : textures) {
            if (!texture) Fail("tag-resource-missing");
            list->setTextureState(texture, nvrhi::AllSubresources, texture->getDesc().initialState);
        }
        list->commitBarriers();
    }
    void Record(nvrhi::ICommandList* list, nvrhi::ITexture* hdr, nvrhi::ITexture* input_depth,
                nvrhi::ITexture* input_motion, unsigned w, unsigned h, UINT64 frame,
                float jitter_x, float jitter_y, bool reset, bool reverse_depth) {
        if (!requested) return;
        if (!owner || !list || !hdr || !input_depth || !input_motion || frame == last_frame ||
            !std::isfinite(jitter_x) || !std::isfinite(jitter_y)) Fail("source-frame-identity");
        if (static_cast<ID3D12Device*>(list->getDevice()->getNativeObject(nvrhi::ObjectTypes::D3D12_Device)) != host_device.Get())
            Fail("device-changed");
        for (auto* texture : { hdr, input_depth, input_motion }) {
            const auto& d = texture->getDesc();
            if (d.width < w || d.height < h || d.sampleCount != 1 || d.arraySize != 1 || d.mipLevels != 1)
                Fail("source-texture-contract");
        }
        if (hdr->getDesc().format != nvrhi::Format::RGBA16_FLOAT ||
            input_motion->getDesc().format != nvrhi::Format::RG16_FLOAT) Fail("source-format");
        const UINT64 old_generation = resource_generation;
        Prepare(list->getDevice(), w, h, input_depth);
        last_frame = frame;
        nvrhi::TextureSlice rect; rect.width = w; rect.height = h;
        list->copyTexture(color, rect, hdr, rect);
        list->copyTexture(motion, rect, input_motion, rect);
        nvrhi::ComputeState cs; cs.pipeline = depth_pipeline; cs.bindings = { depth_binding };
        list->setComputeState(cs); list->dispatch((w+7)/8, (h+7)/8, 1);
        for (auto* texture : {color.Get(), depth.Get(), motion.Get()})
            list->setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        list->setTextureState(output, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        list->commitBarriers();
        constexpr auto shader_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        NrExternalFrameV1 f{};
        f.provider_id = kNrExternalProviderIdDl5f; f.view_id = 1; f.epoch = epoch; f.frame_id = frame + 1;
        auto native_list = Native(static_cast<ID3D12GraphicsCommandList*>(
            list->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList)));
        f.command_list = reinterpret_cast<UINT64>(native_list.Get());
        f.queue = reinterpret_cast<UINT64>(queue.Get()); f.fence = reinterpret_cast<UINT64>(fence.Get()); f.fence_value = completed;
        f.lease_flags = static_cast<unsigned>(NrExternalLeaseFlags::CommandListOwnedByProducer) |
            static_cast<unsigned>(NrExternalLeaseFlags::QueueFenceRequired) | static_cast<unsigned>(NrExternalLeaseFlags::HdrColor);
        // MC1 epoch identifies the claimed view session, not each allocation.
        // A size change resets history inside that session; changing epoch would
        // correctly be rejected until ownership is explicitly retired/reclaimed.
        if (reset || old_generation != resource_generation) f.lease_flags |= static_cast<unsigned>(NrExternalLeaseFlags::ResetTemporal);
        if (reverse_depth) f.lease_flags |= static_cast<unsigned>(NrExternalLeaseFlags::DepthInverted);
        f.color = Resource(color, shader_state); f.depth = Resource(depth, shader_state);
        f.motion = Resource(motion, shader_state); f.output = Resource(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        f.color.jitter_x = jitter_x; f.color.jitter_y = jitter_y;
        // Donut's actual motion target is in pixels. Streamline normalizes it
        // separately; normalization constants are not the raw field's units.
        f.motion.motion_scale_x = f.motion.motion_scale_y = 1.f;
        f.pre_exposure = f.exposure_scale = 1.f; // Sample HDR target precedes tonemapping/exposure.
        if (frame < 3) {
            ComPtr<ID3D12Device> queue_owner, list_owner;
            const bool queue_same = SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queue_owner))) && queue_owner.Get() == native_device.Get();
            const bool list_same = SUCCEEDED(native_list->GetDevice(IID_PPV_ARGS(&list_owner))) && list_owner.Get() == native_device.Get();
            donut::log::info("NR_FG_CHAIN nativeIdentity frame=%llu queueSame=%u listSame=%u end=1", frame, unsigned(queue_same), unsigned(list_same));
            if (!queue_same || !list_same) Fail("native-device-identity");
        }
        NrExternalSubmitResultV1 result{};
        const auto accepted = submit(kNrExternalProviderAbi, &f, &result);
        if (result.writeback_performed && (!accepted || result.admitted_frame_id != f.frame_id))
            Fail("consumer-output-identity");
        if (result.reason == NrExternalProviderReason::ObserverUncertain ||
            result.reason == NrExternalProviderReason::TransportFailed) Fail("opaque-recording-uncertain");
        list->clearState(); // Direct Core recording invalidates NVRHI's binding cache.
        if (accepted && result.writeback_performed) list->copyTexture(hdr, rect, output, rect);
        // The unchanged Streamline tags declare these exact initial states.
        for (auto* texture : {hdr, input_depth, input_motion})
            list->setTextureState(texture, nvrhi::AllSubresources, texture->getDesc().initialState);
        list->commitBarriers();
        frame_recorded = true;
        donut::log::info("NR_FG_CHAIN frame=%llu epoch=%llu R=%ux%u accepted=%d recorded=%u reason=%s reset=%u provenance=renderer end=1",
            frame, epoch, w, h, accepted, result.writeback_performed,
            NrExternalProviderReasonName(result.reason), unsigned(reset || old_generation != resource_generation));
    }
    void Submitted() {
        if (!frame_recorded) return;
        const auto result = nr_external_provider_completion::SignalAndWait(queue.Get(), fence.Get(), event, completed+1, 3000);
        if (result != nr_external_provider_completion::Result::Complete) Fail("source-batch-fence");
        ++completed; frame_recorded = false;
        donut::log::info("NR_FG_CHAIN completedFence=%llu end=1", completed);
    }
    void Shutdown(const void* renderer) {
        if (!requested || renderer != renderer_identity) return;
        if (frame_recorded || fence->GetCompletedValue() != completed) Fail("shutdown-unretired");
        if (owner && !release(kNrExternalProviderAbi, kNrExternalProviderIdDl5f)) Fail("owner-release");
        owner = false;
        depth_binding = nullptr; output = nullptr; color = nullptr; depth = nullptr; motion = nullptr;
        depth_pipeline = nullptr; depth_shader = nullptr; depth_layout = nullptr;
        CloseHandle(event); event = nullptr; fence.Reset(); queue.Reset(); native_device.Reset(); host_device.Reset();
        requested = false;
    }
};
// Deliberately retain the wrapper until explicit successful Shutdown. Failure
// exits without running destructors on potentially submitted opaque resources.
inline Chain& Get() { static Chain* chain = new Chain; return *chain; }
}

// F8 detached GPU capture backend. Include after the D3D12/lifecycle types
// (NrDiagnosticIdentity and ReleaseCom are provided by nr_before_sr.cpp).
#ifndef NR_F8_GPU_INL
#define NR_F8_GPU_INL

#include <cstdint>

#ifdef NR_F8_GPU_LIFECYCLE_ONLY
struct NrDiagnosticIdentity {
    unsigned long long instance_id = 0;
    unsigned long long frame_id = 0;
    unsigned int config_generation = 0;
    bool warmed_up = false;
};
struct NrStageCaptureInfo {
    NrDiagnosticIdentity identity = {};
    unsigned long long request_id = 0;
    unsigned int width = 0, height = 0, stride = 1;
    unsigned int source_width = 0, source_height = 0;
    unsigned int region_x = 0, region_y = 0;
    unsigned int region_width = 0, region_height = 0;
};
namespace nr_f8 {
struct FrameTicket {
    unsigned long long session = 0;
    unsigned int group = 0;
    unsigned int index = 0;
};
}
#endif

namespace nr_f8_gpu {

constexpr unsigned int kGroupCount = 3;
constexpr unsigned int kFramesPerGroup = 16;
constexpr unsigned int kCapacity = kGroupCount * kFramesPerGroup;
constexpr unsigned int kPlaneCount = 4;
constexpr unsigned int kExposureTrailerBytes = 16;
constexpr unsigned int kMaxRoiSide = 256;

enum class TokenState : unsigned int {
    Free, Armed, Recorded, Submitted, Invalidated, Completed
};

struct TokenIdentity {
    unsigned long long request = 0;
    unsigned int group = 0;
    unsigned int frame_index = 0;
    NrDiagnosticIdentity identity = {};
};

struct TokenEntry {
    TokenIdentity key = {};
    unsigned long long recording_token = 0;
    void *queue_key = nullptr;
    unsigned long long fence_value = 0;
    TokenState state = TokenState::Free;
};

// This ledger is part of the production ring, rather than a test-only mock.
// It makes delayed command-list replay conservative: a resource cannot leave
// the ring until its recording token is invalidated and any submitted fence
// has completed.
class TokenLedger {
public:
    int Arm(const TokenIdentity &key) {
        for (unsigned int i = 0; i < kCapacity; ++i) {
            if (entries_[i].state != TokenState::Free) continue;
            entries_[i] = {};
            entries_[i].key = key;
            entries_[i].state = TokenState::Armed;
            return static_cast<int>(i);
        }
        return -1;
    }

    bool Record(unsigned int slot, unsigned long long token) {
        if (!Valid(slot) || entries_[slot].state != TokenState::Armed ||
            token == 0) return false;
        entries_[slot].recording_token = token;
        entries_[slot].state = TokenState::Recorded;
        return true;
    }

    bool Submit(unsigned long long token, void *queue_key,
                unsigned long long fence_value) {
        if (token == 0 || queue_key == nullptr || fence_value == 0) return false;
        bool found = false;
        for (TokenEntry &entry : entries_) {
            if (entry.recording_token != token ||
                (entry.state != TokenState::Recorded &&
                 entry.state != TokenState::Invalidated)) continue;
            entry.queue_key = queue_key;
            entry.fence_value = fence_value;
            entry.state = TokenState::Submitted;
            found = true;
        }
        return found;
    }

    // definitely_unsubmitted is supplied only when the lifecycle layer has
    // proved Reset/Destroy happened before native Execute. Otherwise retain
    // the resource until a possible delayed submission gets its fence.
    unsigned int Invalidate(unsigned long long token, bool definitely_unsubmitted) {
        unsigned int count = 0;
        for (TokenEntry &entry : entries_) {
            if (entry.recording_token != token ||
                (entry.state != TokenState::Recorded &&
                 entry.state != TokenState::Armed)) continue;
            entry.state = definitely_unsubmitted ? TokenState::Completed
                                                 : TokenState::Invalidated;
            ++count;
        }
        return count;
    }

    bool Ready(unsigned int slot, void *queue_key, unsigned long long fence,
               bool fence_complete) {
        if (!Valid(slot) || entries_[slot].state != TokenState::Submitted ||
            entries_[slot].queue_key != queue_key ||
            entries_[slot].fence_value != fence || !fence_complete) return false;
        entries_[slot].state = TokenState::Completed;
        return true;
    }

    bool Reclaim(unsigned int slot) {
        if (!Valid(slot) || entries_[slot].state != TokenState::Completed)
            return false;
        entries_[slot] = {};
        return true;
    }

    bool DiscardArmed(unsigned int slot) {
        if (!Valid(slot) || entries_[slot].state != TokenState::Armed)
            return false;
        entries_[slot] = {};
        return true;
    }

    const TokenEntry &Get(unsigned int slot) const { return entries_[slot]; }
    unsigned int Count(TokenState state) const {
        unsigned int count = 0;
        for (const TokenEntry &entry : entries_)
            count += entry.state == state ? 1u : 0u;
        return count;
    }

private:
    bool Valid(unsigned int slot) const { return slot < kCapacity; }
    TokenEntry entries_[kCapacity] = {};
};

#ifndef NR_F8_GPU_LIFECYCLE_ONLY

struct ArmArgs {
    ID3D12Device *device = nullptr;
    ID3D12DescriptorHeap *source_heap = nullptr;
    unsigned int descriptor_count = 0;
    unsigned int descriptor_size = 0;
    unsigned int capture_uav_index = 0;
    unsigned long long request = 0;
    unsigned int group = 0;
    unsigned int frame_index = 0;
    NrDiagnosticIdentity identity = {};
    unsigned int roi_width = 0;
    unsigned int roi_height = 0;
    const NrStageCaptureInfo *info = nullptr;
};

struct ArmResult {
    unsigned int slot = 0;
    ID3D12DescriptorHeap *heap = nullptr;
    ID3D12Resource *buffer = nullptr;
    ID3D12Resource *readback = nullptr;
    UINT64 bytes = 0;
    nr_f8::FrameTicket ticket = {};
};

struct ReadbackJob {
    ID3D12Resource *readback = nullptr; // ownership transfers to handoff
    UINT64 bytes = 0;
    TokenIdentity key = {};
    nr_f8::FrameTicket ticket = {};
    NrStageCaptureInfo info = {};
    unsigned int ring_slot = 0;
    unsigned long long recording_token = 0;
};

using FenceCompleteFn = bool (*)(void *queue_key, unsigned long long fence);
using HandoffFn = void (*)(void *context, ReadbackJob *job);

struct ResourceEntry {
    ID3D12Resource *buffer = nullptr;
    ID3D12Resource *readback = nullptr;
    ID3D12DescriptorHeap *heap = nullptr;
    UINT64 bytes = 0;
    TokenIdentity key = {};
    NrStageCaptureInfo info = {};
    unsigned int ledger_slot = 0;
    D3D12_RESOURCE_STATES buffer_state = D3D12_RESOURCE_STATE_COMMON;
};

class CaptureRing {
public:
    bool Arm(const ArmArgs &args, ArmResult &result) {
        result = {};
        if (args.device == nullptr || args.source_heap == nullptr ||
            args.descriptor_count == 0 || args.descriptor_size == 0 ||
            args.capture_uav_index >= args.descriptor_count || args.request == 0 ||
            args.group >= kGroupCount || args.frame_index >= kFramesPerGroup ||
            args.roi_width == 0 || args.roi_height == 0 ||
            args.roi_width > kMaxRoiSide || args.roi_height > kMaxRoiSide)
            return false;
        const UINT64 plane_values = static_cast<UINT64>(args.roi_width) *
            args.roi_height * 4u;
        const UINT64 bytes = plane_values * kPlaneCount * sizeof(float) +
            kExposureTrailerBytes;
        if (bytes == 0 || bytes > 64ull * 1024ull * 1024ull) return false;
        TokenIdentity key = {args.request, args.group, args.frame_index,
                             args.identity};
        const int ledger_slot = ledger_.Arm(key);
        if (ledger_slot < 0) return false;
        ResourceEntry resource = {};
        resource.key = key;
        if (args.info != nullptr) resource.info = *args.info;
        resource.info.identity = args.identity;
        resource.info.request_id = args.request;
        resource.info.region_width = args.roi_width;
        resource.info.region_height = args.roi_height;
        resource.ledger_slot = static_cast<unsigned int>(ledger_slot);
        resource.bytes = bytes;
        if (!CreateResources(args, resource)) {
            ledger_.DiscardArmed(resource.ledger_slot);
            return false;
        }
        resources_[resource.ledger_slot] = resource;
        result.slot = resource.ledger_slot;
        result.heap = resource.heap;
        result.buffer = resource.buffer;
        result.readback = resource.readback;
        result.bytes = resource.bytes;
        result.ticket = {args.request, args.group, args.frame_index};
        return true;
    }

    bool FinishDispatch(nr_f8::FrameTicket ticket, unsigned int slot,
                        ID3D12GraphicsCommandList *command_list,
                        unsigned long long recording_token) {
        (void)ticket;
        return FinishDispatch(command_list, slot, recording_token);
    }

    bool FinishDispatch(ID3D12GraphicsCommandList *command_list,
                        unsigned int slot, unsigned long long recording_token) {
        if (command_list == nullptr || slot >= kCapacity ||
            resources_[slot].buffer == nullptr ||
            !ledger_.Record(slot, recording_token)) return false;
        ResourceEntry &resource = resources_[slot];
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource.buffer;
        ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);
        if (resource.buffer_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource.buffer;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = resource.buffer_state;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);
            resource.buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
        ID3D12GraphicsCommandList_CopyBufferRegion(
            command_list, resource.readback, 0, resource.buffer, 0, resource.bytes);
        return true;
    }

    unsigned int OnSubmit(unsigned long long recording_token, void *queue_key,
                          unsigned long long fence_value) {
        return ledger_.Submit(recording_token, queue_key, fence_value) ? 1u : 0u;
    }

    unsigned int OnRecordingInvalidated(unsigned long long recording_token,
                                        bool definitely_unsubmitted) {
        return ledger_.Invalidate(recording_token, definitely_unsubmitted);
    }

    unsigned int Collect(FenceCompleteFn fence_complete, HandoffFn handoff,
                         void *context) {
        if (fence_complete == nullptr || handoff == nullptr) return 0;
        unsigned int count = 0;
        for (unsigned int i = 0; i < kCapacity; ++i) {
            const TokenEntry &entry = ledger_.Get(i);
            if (entry.state != TokenState::Submitted ||
                !fence_complete(entry.queue_key, entry.fence_value)) continue;
            ResourceEntry &resource = resources_[i];
            ReadbackJob *job = new (std::nothrow) ReadbackJob{};
            if (job == nullptr) {
                // Leave the submitted entry held; the next poll retries
                // without releasing a GPU object prematurely.
                continue;
            }
            if (!ledger_.Ready(i, entry.queue_key, entry.fence_value, true)) {
                delete job;
                continue;
            }
            job->readback = resource.readback;
            job->bytes = resource.bytes;
            job->key = resource.key;
            job->ticket = {resource.key.request, resource.key.group,
                           resource.key.frame_index};
            job->info = resource.info;
            job->ring_slot = i;
            job->recording_token = entry.recording_token;
            resource.readback = nullptr;
            ReleaseCom(resource.buffer);
            ReleaseCom(resource.heap);
            resource.buffer = nullptr;
            resource.heap = nullptr;
            handoff(context, job);
            ledger_.Reclaim(i);
            resources_[i] = {};
            ++count;
        }
        // An invalidated command list proven never to have reached Execute
        // has no GPU readback to hand off. Release its objects here after the
        // lifecycle layer has proved the recording token dead.
        for (unsigned int i = 0; i < kCapacity; ++i) {
            if (ledger_.Get(i).state != TokenState::Completed ||
                resources_[i].buffer == nullptr) continue;
            ReleaseCom(resources_[i].readback);
            ReleaseCom(resources_[i].buffer);
            ReleaseCom(resources_[i].heap);
            resources_[i] = {};
            ledger_.Reclaim(i);
        }
        return count;
    }

    void Destroy() {
        for (unsigned int i = 0; i < kCapacity; ++i) {
            ReleaseCom(resources_[i].readback);
            ReleaseCom(resources_[i].buffer);
            ReleaseCom(resources_[i].heap);
            resources_[i] = {};
            if (ledger_.Get(i).state != TokenState::Free)
                ledger_.Invalidate(ledger_.Get(i).recording_token, true);
            if (ledger_.Get(i).state == TokenState::Completed)
                ledger_.Reclaim(i);
        }
    }

    const TokenLedger &ledger() const { return ledger_; }

private:
    bool CreateResources(const ArmArgs &args, ResourceEntry &resource) {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = resource.bytes;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr = ID3D12Device_CreateCommittedResource(
            args.device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&resource.buffer));
        if (FAILED(hr) || resource.buffer == nullptr) return false;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        hr = ID3D12Device_CreateCommittedResource(
            args.device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&resource.readback));
        if (FAILED(hr) || resource.readback == nullptr) {
            ReleaseCom(resource.buffer);
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors = args.descriptor_count;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = ID3D12Device_CreateDescriptorHeap(
            args.device, &heap_desc, IID_ID3D12DescriptorHeap,
            reinterpret_cast<void **>(&resource.heap));
        if (FAILED(hr) || resource.heap == nullptr) {
            ReleaseCom(resource.readback);
            ReleaseCom(resource.buffer);
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE source =
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(args.source_heap);
        D3D12_CPU_DESCRIPTOR_HANDLE destination =
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource.heap);
        ID3D12Device_CopyDescriptorsSimple(
            args.device, args.descriptor_count, destination, source,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements = static_cast<UINT>(resource.bytes / sizeof(UINT));
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        destination.ptr += static_cast<SIZE_T>(args.descriptor_size) *
            args.capture_uav_index;
        ID3D12Device_CreateUnorderedAccessView(
            args.device, resource.buffer, nullptr, &view, destination);
        return true;
    }

    TokenLedger ledger_ = {};
    ResourceEntry resources_[kCapacity] = {};
};

#endif

} // namespace nr_f8_gpu
#endif

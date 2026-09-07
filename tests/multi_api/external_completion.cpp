#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "src/interop/nr_external_provider_completion.h"
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
using namespace nr_external_provider_completion;
static unsigned checks;
static void Check(bool value) { ++checks; if (!value) { std::fprintf(stderr,"check %u failed\n",checks); std::exit(1); } }
struct FaultOps {
    bool valid = true, reset = true;
    HRESULT arm = S_OK;
    UINT64 completed = 0;
    ULONGLONG now = 0;
    DWORD result = WAIT_OBJECT_0;
    unsigned calls = 0, finish_at = 1;
    bool Valid() { return valid; }
    ULONGLONG Now() { return now; }
    UINT64 Completed() { return completed; }
    bool Reset() { return reset; }
    HRESULT Arm(UINT64) { return arm; }
    DWORD Wait(DWORD timeout) {
        ++calls; now += timeout < 2 ? timeout : 2;
        if (calls == finish_at) completed = 10;
        return result;
    }
};
int main() {
    FaultOps f;
    Check(WaitWith(f, 10, 20) == Result::Complete);
    f = {}; f.finish_at = 3; // two stale event wakes must not authorize copy-home
    Check(WaitWith(f, 10, 20) == Result::Complete && f.calls == 3);
    f = {}; f.finish_at = 100;
    Check(WaitWith(f, 10, 5) == Result::Timeout && f.now == 5);
    f = {}; f.completed = UINT64_MAX;
    Check(WaitWith(f, 10, 20) == Result::Removed);
    f = {}; f.arm = E_FAIL;
    Check(WaitWith(f, 10, 20) == Result::EventFailed && f.calls == 0);
    f = {}; f.reset = false;
    Check(WaitWith(f, 10, 20) == Result::EventFailed);
    f = {}; f.result = WAIT_FAILED;
    Check(WaitWith(f, 10, 20) == Result::EventFailed);
    f = {}; f.valid = false;
    Check(WaitWith(f, 10, 20) == Result::Invalid);
    f = {};
    Check(WaitWith(f, 0, 20) == Result::Invalid);
    Check(WaitWith(f, UINT64_MAX, 20) == Result::Invalid);
    Check(WaitWith(f, 10, 0) == Result::Timeout);
    f.completed = 10;
    Check(WaitWith(f, 10, 0) == Result::Complete);

    IDXGIFactory4 *factory = nullptr; IDXGIAdapter *adapter = nullptr;
    ID3D12Device *device = nullptr; ID3D12CommandQueue *queue = nullptr;
    ID3D12Fence *fence = nullptr;
    Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    Check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    Check(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    D3D12_COMMAND_QUEUE_DESC desc{};
    Check(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))));
    Check(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(event != nullptr);
    Check(SignalAndWait(queue, fence, event, 1, 5000) == Result::Complete);
    Check(Wait(fence, event, 2, 5) == Result::Timeout);
    // Timed-out registration can wake the next wait; fence must still confirm 3.
    Check(SignalAndWait(queue, fence, event, 3, 5000) == Result::Complete);
    Check(SignalAndWait(nullptr, fence, event, 4, 5) == Result::Invalid);
    CloseHandle(event); fence->Release(); queue->Release(); device->Release();
    adapter->Release(); factory->Release();
    std::printf("external completion: %u assertions passed (fault controls + real WARP fence; no NR model)\n", checks);
}

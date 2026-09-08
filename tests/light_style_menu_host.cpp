// Real ReShade/add-on menu integration fixture. No game or NR model calls.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>

static void Check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::printf("FAIL %s %08lx\n", operation, static_cast<unsigned long>(hr));
        std::exit(2);
    }
}
static LONG WINAPI Crash(EXCEPTION_POINTERS* exception) {
    HANDLE file = CreateFileW(L"menu-fault.dmp",GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
    BOOL saved = FALSE;
    if (file != INVALID_HANDLE_VALUE) {
        saved = MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),&info,nullptr,nullptr);
        CloseHandle(file);
    }
    std::printf("FAULT code=%08lx pc=%p dump=%d\n",exception->ExceptionRecord->ExceptionCode,
        exception->ExceptionRecord->ExceptionAddress,saved);
    return EXCEPTION_EXECUTE_HANDLER;
}
static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    return DefWindowProcW(window,message,w,l);
}
int main() {
    SetUnhandledExceptionFilter(Crash);
    setvbuf(stdout,nullptr,_IONBF,0);
    // Also suppress the OS crash dialog; evidence is written by this process.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"LightStyleMenuFixture";
    if (!RegisterClassW(&windowClass)) return 2;
    HWND window = CreateWindowW(windowClass.lpszClassName,L"Light style menu fixture",
        WS_OVERLAPPEDWINDOW,0,0,1600,1600,nullptr,nullptr,windowClass.hInstance,nullptr);
    if (!window) return 2;
    ShowWindow(window,SW_HIDE);
    std::printf("hidden_window %d\n",!IsWindowVisible(window));

    IDXGIFactory4* factory = nullptr;
    Check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"factory");
    IDXGIAdapter* adapter = nullptr;
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP adapter");
    ID3D12Device* device = nullptr;
    Check(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"WARP device");
    ID3D12CommandQueue* queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    Check(device->CreateCommandQueue(&queueDescription,IID_PPV_ARGS(&queue)),"queue");
    DXGI_SWAP_CHAIN_DESC1 swapDescription{};
    swapDescription.Width = swapDescription.Height = 1600;
    swapDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.BufferCount = 2;
    swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* initialSwap = nullptr;
    Check(factory->CreateSwapChainForHwnd(queue,window,&swapDescription,nullptr,nullptr,&initialSwap),"swapchain");
    IDXGISwapChain3* swap = nullptr;
    Check(initialSwap->QueryInterface(IID_PPV_ARGS(&swap)),"swapchain3");
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDescription.NumDescriptors = 2;
    Check(device->CreateDescriptorHeap(&heapDescription,IID_PPV_ARGS(&heap)),"RTV heap");
    UINT stride = device->GetDescriptorHandleIncrementSize(heapDescription.Type);
    ID3D12Resource* buffers[2]{};
    for (UINT i = 0; i < 2; ++i) {
        Check(swap->GetBuffer(i,IID_PPV_ARGS(&buffers[i])),"buffer");
        auto handle = heap->GetCPUDescriptorHandleForHeapStart(); handle.ptr += i*stride;
        device->CreateRenderTargetView(buffers[i],nullptr,handle);
    }
    ID3D12CommandAllocator* allocator = nullptr;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    ID3D12GraphicsCommandList* list = nullptr;
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list)),"list");
    Check(list->Close(),"initial close");
    ID3D12Fence* fence = nullptr;
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    HANDLE event = CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if (!event) return 2;

    for (UINT frame = 1; frame <= 40; ++frame) {
        auto tick = reinterpret_cast<void(*)()>(GetProcAddress(GetModuleHandleW(L"sentinel.addon64"),"MenuTick"));
        if (!tick) { std::printf("FAIL sentinel export missing\n"); return 2; }
        tick(); // Outside ReShade callbacks: open_overlay emits real events.
        MSG message;
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Check(allocator->Reset(),"allocator reset");
        Check(list->Reset(allocator,nullptr),"list reset");
        UINT index = swap->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers[index];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&barrier);
        auto handle = heap->GetCPUDescriptorHandleForHeapStart(); handle.ptr += index*stride;
        const float colour[] = {.05f,.12f,.2f,1};
        list->ClearRenderTargetView(handle,colour,0,nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&barrier);
        Check(list->Close(),"frame close");
        ID3D12CommandList* lists[] = {list}; queue->ExecuteCommandLists(1,lists);
        Check(swap->Present(0,0),"present");
        Check(queue->Signal(fence,frame),"signal");
        if (fence->GetCompletedValue() < frame) {
            Check(fence->SetEventOnCompletion(frame,event),"fence event");
            if (WaitForSingleObject(event,3000) != WAIT_OBJECT_0) return 3;
        }
        std::printf("present_done %u\n",frame);
        Sleep(30);
    }
    CloseHandle(event);
    list->Release(); allocator->Release(); heap->Release(); fence->Release();
    for (auto* buffer : buffers) buffer->Release();
    swap->Release(); initialSwap->Release(); queue->Release(); device->Release();
    adapter->Release(); factory->Release(); DestroyWindow(window);
    std::printf("COMPLETE 40\n");
    return 0;
}

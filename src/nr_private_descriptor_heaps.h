#pragma once
#include <d3d12.h>

// ReShade's full add-on device returns descriptor-heap proxies. A private NGX
// feature can retain that device while recording into the original list. Only
// that private-call boundary may normalize heap interfaces; ordinary game
// calls and the recording/fence identity remain unchanged.
class ScopedPrivateDescriptorHeaps {
public:
    ScopedPrivateDescriptorHeaps(bool enabled, UINT count,
                                ID3D12DescriptorHeap *const *heaps) noexcept
        : forwarded_(heaps) {
        if (!enabled || !heaps || count == 0 || count > 2) return;
        for (UINT i = 0; i < count; ++i) {
            normalized_[i] = heaps[i];
            if (!heaps[i]) continue;
            ID3D12DescriptorHeap *original = nullptr;
            const HRESULT hr = Query(heaps[i], reinterpret_cast<void **>(&original));
            if (SUCCEEDED(hr) && original) {
                retained_[i] = original; // IID_UnwrappedObject returns an owned reference.
                normalized_[i] = original;
                if (original != heaps[i]) ++unwrapped_;
            }
        }
        forwarded_ = normalized_;
    }
    ~ScopedPrivateDescriptorHeaps() {
        for (auto *heap : retained_) if (heap) {
#if defined(CINTERFACE)
            heap->lpVtbl->Release(heap);
#else
            heap->Release();
#endif
        }
    }
    ScopedPrivateDescriptorHeaps(const ScopedPrivateDescriptorHeaps &) = delete;
    ScopedPrivateDescriptorHeaps &operator=(const ScopedPrivateDescriptorHeaps &) = delete;
    ID3D12DescriptorHeap *const *data() const noexcept { return forwarded_; }
    UINT unwrapped_count() const noexcept { return unwrapped_; }
private:
    static HRESULT Query(ID3D12DescriptorHeap *heap, void **out) noexcept {
        // Pinned ReShade object contract, also used by the device identity path.
        static constexpr GUID unwrap = {0x7f2c9a11,0x3b4e,0x4d6a,
            {0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
#if defined(CINTERFACE)
        return heap->lpVtbl->QueryInterface(heap, unwrap, out);
#else
        return heap->QueryInterface(unwrap, out);
#endif
    }
    ID3D12DescriptorHeap *const *forwarded_ = nullptr;
    ID3D12DescriptorHeap *normalized_[2]{};
    ID3D12DescriptorHeap *retained_[2]{};
    UINT unwrapped_ = 0;
};

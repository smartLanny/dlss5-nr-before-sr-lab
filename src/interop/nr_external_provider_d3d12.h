#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include "nr_external_provider.h"

// D3D12 same-frame admission + writeback dispatch for MC1.
// Production records Feature18 work only through the core hook installed by
// nr_external_provider_consumer.inl (BindNrParameters / g_nr_api). When that
// hook is absent or returns Failed, this declines — it never reports copy as NR.
// Deterministic CopyTextureRegion exists solely under NR_TEST_MULTIAPI.
namespace nr_external_provider_d3d12 {

inline ID3D12Resource *AsResource(std::uint64_t value) noexcept {
    return reinterpret_cast<ID3D12Resource *>(static_cast<std::uintptr_t>(value));
}
inline ID3D12GraphicsCommandList *AsList(std::uint64_t value) noexcept {
    return reinterpret_cast<ID3D12GraphicsCommandList *>(static_cast<std::uintptr_t>(value));
}

inline bool RegionFits(const NrExternalResourceV1 &r) noexcept {
    return nr_external_provider::ResourcePresent(r) &&
        nr_external_provider::ResourceRectInside(r);
}

inline void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept {
    if (!list || !resource || before == after) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
#if defined(CINTERFACE)
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
#else
    list->ResourceBarrier(1, &barrier);
#endif
}

inline bool CopyRegion(ID3D12GraphicsCommandList *list,
                       const NrExternalResourceV1 &src,
                       const NrExternalResourceV1 &dst) noexcept {
    if (!list || !RegionFits(src) || !RegionFits(dst)) return false;
    if (src.rect_w != dst.rect_w || src.rect_h != dst.rect_h) return false;
    ID3D12Resource *source = AsResource(src.resource);
    ID3D12Resource *destination = AsResource(dst.resource);
    if (!source || !destination) return false;

    Transition(list, source,
               static_cast<D3D12_RESOURCE_STATES>(src.state_before),
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list, destination,
               static_cast<D3D12_RESOURCE_STATES>(dst.state_before),
               D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource = source;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.SubresourceIndex = src.subresource;
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.pResource = destination;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.SubresourceIndex = dst.subresource;
    D3D12_BOX box{};
    box.left = src.rect_x;
    box.top = src.rect_y;
    box.front = 0;
    box.right = src.rect_x + src.rect_w;
    box.bottom = src.rect_y + src.rect_h;
    box.back = 1;
#if defined(CINTERFACE)
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &to, dst.rect_x, dst.rect_y, 0, &from, &box);
#else
    list->CopyTextureRegion(&to, dst.rect_x, dst.rect_y, 0, &from, &box);
#endif

    Transition(list, source, D3D12_RESOURCE_STATE_COPY_SOURCE,
               static_cast<D3D12_RESOURCE_STATES>(src.state_after));
    Transition(list, destination, D3D12_RESOURCE_STATE_COPY_DEST,
               static_cast<D3D12_RESOURCE_STATES>(dst.state_after));
    return true;
}

#if defined(NR_TEST_MULTIAPI)
// Test double only: same-frame color → output. Never used as production NR.
inline bool DeterministicWriteback(ID3D12GraphicsCommandList *list,
                                   const NrExternalFrameV1 &frame,
                                   bool run_nr) noexcept {
    if (!list) return false;
    if (!run_nr) return true;
    return CopyRegion(list, frame.color, frame.output);
}
#endif

// kind: 0=Failed, 1=IdentityRetained, 2=TransferRecorded
struct CoreEvaluateResult {
    std::uint32_t kind = 0;
    NrExternalProviderReason reason = NrExternalProviderReason::RuntimeUnavailable;
};
using CoreEvaluateFn = CoreEvaluateResult (*)(const NrExternalFrameV1 &frame,
                                              bool history_reset);
inline CoreEvaluateFn g_core_evaluate = nullptr;

struct ProcessResult {
    nr_external_provider::AdmitDecision admit{};
    bool writeback = false;
};

inline ProcessResult ProcessSameFrame(nr_external_provider::Session &session,
                                      const nr_external_provider::ConfigSnapshot &cfg,
                                      const NrExternalFrameV1 &frame) noexcept {
    ProcessResult result;
    const bool wants_nr = nr_external_provider::StrengthAllowsWork(cfg);
    if (wants_nr && g_core_evaluate == nullptr) {
#if defined(NR_TEST_MULTIAPI)
#else
        result.admit.reason = NrExternalProviderReason::RuntimeUnavailable;
        result.admit.admit = false;
        result.admit.run_nr = false;
        session.last_bypass = result.admit.reason;
        ++session.rejected_frames;
        return result;
#endif
    }

    const nr_external_provider::Session before = session;
    result.admit = nr_external_provider::Admit(session, cfg, frame);
    if (!result.admit.admit) return result;
    auto *list = AsList(frame.command_list);
    if (result.admit.run_nr) {
        bool ok = false;
        if (g_core_evaluate != nullptr) {
            const CoreEvaluateResult ev =
                g_core_evaluate(frame, result.admit.history_reset);
            if (ev.kind == 2u) {
                ok = true;
                result.admit.reason = NrExternalProviderReason::Ready;
            } else if (ev.kind == 1u) {
                result.writeback = false;
                result.admit.reason = NrExternalProviderReason::IdentityPassthrough;
                session.last_bypass = result.admit.reason;
                ++session.identity_frames;
                return result;
            } else {
                ok = false;
                result.admit.reason = ev.reason;
            }
        }
#if defined(NR_TEST_MULTIAPI)
        else
            ok = DeterministicWriteback(list, frame, true);
#else
        (void)list;
#endif
        result.writeback = ok;
        if (ok) {
            nr_external_provider::NoteWriteback(session, true);
        } else {
            if (result.admit.reason == NrExternalProviderReason::Ready)
                result.admit.reason = NrExternalProviderReason::TransportFailed;
            result.admit.admit = false;
            result.admit.run_nr = false;
            result.writeback = false;
            nr_external_provider::RollbackAdmit(session, before,
                                                result.admit.reason);
        }
    } else {
        result.writeback = false;
        result.admit.reason = NrExternalProviderReason::Disabled;
    }
    return result;
}

} // namespace nr_external_provider_d3d12

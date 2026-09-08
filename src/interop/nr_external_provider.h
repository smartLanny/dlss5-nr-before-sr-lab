#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include "nr_external_provider_abi.h"

// Host-testable admission, ownership, provenance and history policy for MC1.
// No D3D12/COM calls. The D3D12 consumer records work only after Admit succeeds.
namespace nr_external_provider {

struct ConfigSnapshot {
    bool master_enabled = false;
    float transfer_strength = 0.0f;
    bool observer_uncertain = false;
    bool competing_nr = false; // ShortFuse / generic RenoDX / second consumer
    std::uint64_t observer_epoch = 0;
};

struct Session {
    std::uint64_t owner_provider_id = 0;
    std::uint64_t active_view_id = 0;
    std::uint64_t active_epoch = 0;
    std::uint64_t last_frame_id = 0;
    std::uint64_t last_provider_id = 0;
    NrGuideProvenance last_color_provenance = NrGuideProvenance::Unknown;
    NrGuideProvenance last_depth_provenance = NrGuideProvenance::Unknown;
    NrGuideProvenance last_motion_provenance = NrGuideProvenance::Unknown;
    std::uint64_t admitted_frames = 0;
    std::uint64_t writeback_frames = 0;
    std::uint64_t identity_frames = 0;
    std::uint64_t rejected_frames = 0;
    NrExternalProviderReason last_bypass = NrExternalProviderReason::Starting;
    bool history_dirty = true;
    bool owned = false;
};

inline bool StrengthAllowsWork(const ConfigSnapshot &cfg) noexcept {
    return cfg.master_enabled && std::isfinite(cfg.transfer_strength) &&
        cfg.transfer_strength > 0.000001f;
}

inline bool ResourcePresent(const NrExternalResourceV1 &r) noexcept {
    return r.resource != 0 && r.width > 0 && r.height > 0 && r.rect_w > 0 && r.rect_h > 0;
}

inline bool ResourceRectInside(const NrExternalResourceV1 &r) noexcept {
    if (r.rect_x > r.width || r.rect_y > r.height) return false;
    if (r.rect_w > r.width - r.rect_x || r.rect_h > r.height - r.rect_y) return false;
    return true;
}

// Prefer Native, then Extracted, then Synthetic. Low quality is not a reject.
inline NrGuideProvenance PreferProvenance(NrGuideProvenance a, NrGuideProvenance b) noexcept {
    auto rank = [](NrGuideProvenance p) -> int {
        switch (p) {
        case NrGuideProvenance::Native: return 3;
        case NrGuideProvenance::Extracted: return 2;
        case NrGuideProvenance::Synthetic: return 1;
        default: return 0;
        }
    };
    return rank(a) >= rank(b) ? a : b;
}

inline bool FrameHeaderOk(const NrExternalFrameV1 &frame) noexcept {
    return frame.size == sizeof(NrExternalFrameV1) &&
        frame.abi == kNrExternalProviderAbi &&
        frame.reserved == 0 &&
        frame.provider_id != 0 &&
        frame.view_id != 0 &&
        frame.frame_id != 0;
}

inline bool ValidateLeases(const NrExternalFrameV1 &frame) noexcept {
    if (frame.command_list == 0) return false;
    if ((frame.lease_flags & static_cast<std::uint32_t>(NrExternalLeaseFlags::QueueFenceRequired)) != 0) {
        if (frame.queue == 0 || frame.fence == 0) return false;
    }
    if (!ResourcePresent(frame.color) || !ResourceRectInside(frame.color)) return false;
    if (!ResourcePresent(frame.output) || !ResourceRectInside(frame.output)) return false;
    if ((frame.lease_flags & static_cast<std::uint32_t>(NrExternalLeaseFlags::OutputSameAsColor)) != 0) {
        if (frame.color.resource != frame.output.resource) return false;
    } else if (frame.color.resource == frame.output.resource) {
        // In-place without the explicit flag is an ambiguous ownership contract.
        return false;
    }
    // Depth/MV may be absent only when provenance is Unknown AND sizes are zero.
    // Synthetic/Native/Extracted guides that claim presence must be well-formed.
    if (frame.depth.provenance != NrGuideProvenance::Unknown) {
        if (!ResourcePresent(frame.depth) || !ResourceRectInside(frame.depth)) return false;
    }
    if (frame.motion.provenance != NrGuideProvenance::Unknown) {
        if (!ResourcePresent(frame.motion) || !ResourceRectInside(frame.motion)) return false;
    }
    if (frame.exposure.resource != 0) {
        if (!ResourcePresent(frame.exposure) || !ResourceRectInside(frame.exposure)) return false;
    }
    return true;
}

inline bool SourceBoundaryChanged(const Session &s, const NrExternalFrameV1 &frame) noexcept {
    if (!s.owned) return true;
    if (s.last_provider_id != frame.provider_id) return true;
    if (s.active_view_id != frame.view_id) return true;
    if (s.active_epoch != frame.epoch) return true;
    if (s.last_color_provenance != frame.color.provenance) return true;
    if (s.last_depth_provenance != frame.depth.provenance) return true;
    if (s.last_motion_provenance != frame.motion.provenance) return true;
    return false;
}

struct AdmitDecision {
    NrExternalProviderReason reason = NrExternalProviderReason::Starting;
    bool admit = false;
    bool history_reset = false;
    bool run_nr = false; // false => identity/passthrough or reject
};

inline AdmitDecision Admit(Session &session, const ConfigSnapshot &cfg,
                           const NrExternalFrameV1 &frame) noexcept {
    AdmitDecision d;
    if (!FrameHeaderOk(frame)) {
        d.reason = NrExternalProviderReason::UnsupportedContract;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (cfg.observer_uncertain || (cfg.observer_epoch & 1u)) {
        d.reason = NrExternalProviderReason::ObserverUncertain;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (session.owned && session.owner_provider_id != frame.provider_id) {
        d.reason = NrExternalProviderReason::CompetingNr;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (cfg.competing_nr) {
        d.reason = NrExternalProviderReason::CompetingNr;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (!session.owned) {
        d.reason = NrExternalProviderReason::Starting;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (session.active_view_id != 0 && session.active_view_id != frame.view_id) {
        d.reason = NrExternalProviderReason::ViewMismatch;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (session.active_epoch != 0 && session.active_epoch != frame.epoch) {
        d.reason = NrExternalProviderReason::EpochMismatch;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (session.last_frame_id != 0 && frame.frame_id <= session.last_frame_id) {
        d.reason = NrExternalProviderReason::FrameMismatch;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    if (!ValidateLeases(frame)) {
        // Distinguish missing required color/output from other lease faults.
        if (!ResourcePresent(frame.color) || !ResourcePresent(frame.output))
            d.reason = NrExternalProviderReason::MissingRequired;
        else
            d.reason = NrExternalProviderReason::LeaseInvalid;
        session.last_bypass = d.reason;
        ++session.rejected_frames;
        return d;
    }
    // Synthetic alone is never a reject. Missing required color/output already failed.
    d.history_reset = SourceBoundaryChanged(session, frame) || session.history_dirty ||
        ((frame.lease_flags & static_cast<std::uint32_t>(NrExternalLeaseFlags::ResetTemporal)) != 0);
    if (!StrengthAllowsWork(cfg)) {
        d.reason = NrExternalProviderReason::Disabled;
        d.admit = true;
        d.run_nr = false;
        session.last_bypass = d.reason;
        session.last_frame_id = frame.frame_id;
        session.active_view_id = frame.view_id;
        session.active_epoch = frame.epoch;
        session.last_provider_id = frame.provider_id;
        session.last_color_provenance = frame.color.provenance;
        session.last_depth_provenance = frame.depth.provenance;
        session.last_motion_provenance = frame.motion.provenance;
        ++session.admitted_frames;
        ++session.identity_frames;
        session.history_dirty = true;
        return d;
    }
    d.reason = NrExternalProviderReason::Ready;
    d.admit = true;
    d.run_nr = true;
    session.last_bypass = NrExternalProviderReason::Ready;
    session.last_frame_id = frame.frame_id;
    session.active_view_id = frame.view_id;
    session.active_epoch = frame.epoch;
    session.last_provider_id = frame.provider_id;
    session.last_color_provenance = frame.color.provenance;
    session.last_depth_provenance = frame.depth.provenance;
    session.last_motion_provenance = frame.motion.provenance;
    if (d.history_reset) session.history_dirty = false;
    ++session.admitted_frames;
    return d;
}

inline bool ClaimOwnership(Session &session, const ConfigSnapshot &cfg,
                           std::uint64_t provider_id) noexcept {
    if (!provider_id) return false;
    if (cfg.competing_nr || cfg.observer_uncertain || (cfg.observer_epoch & 1u)) return false;
    if (session.owned) return session.owner_provider_id == provider_id;
    session.owned = true;
    session.owner_provider_id = provider_id;
    session.history_dirty = true;
    session.active_view_id = 0;
    session.active_epoch = 0;
    session.last_frame_id = 0;
    session.last_bypass = StrengthAllowsWork(cfg)
        ? NrExternalProviderReason::Ready
        : NrExternalProviderReason::Disabled;
    return true;
}

inline void ReleaseOwnership(Session &session, std::uint64_t provider_id) noexcept {
    if (!session.owned || session.owner_provider_id != provider_id) return;
    session = Session{};
    session.last_bypass = NrExternalProviderReason::Starting;
}

inline void NoteWriteback(Session &session, bool ok) noexcept {
    if (ok) ++session.writeback_frames;
    else {
        session.last_bypass = NrExternalProviderReason::TransportFailed;
        session.history_dirty = true;
    }
}

// Roll back Admit bookkeeping when GPU evaluate declines after admission so the
 // producer can retry the same logical frame_id without FrameMismatch.
inline void RollbackAdmit(Session &session, const Session &before,
                          NrExternalProviderReason reason) noexcept {
    session = before;
    session.last_bypass = reason;
    session.history_dirty = true;
    ++session.rejected_frames;
}

inline void FillStatus(const Session &session, const ConfigSnapshot &cfg,
                       NrExternalStatusV1 &out) noexcept {
    out = NrExternalStatusV1{};
    out.observer_epoch = cfg.observer_epoch;
    out.owner_provider_id = session.owner_provider_id;
    out.admitted_frames = session.admitted_frames;
    out.writeback_frames = session.writeback_frames;
    out.last_provenance = static_cast<std::uint32_t>(session.last_color_provenance);
    out.last_bypass = static_cast<std::uint32_t>(session.last_bypass);
    if (cfg.observer_uncertain || (cfg.observer_epoch & 1u))
        out.reason = NrExternalProviderReason::ObserverUncertain;
    else if (cfg.competing_nr)
        out.reason = NrExternalProviderReason::CompetingNr;
    else if (!session.owned)
        out.reason = NrExternalProviderReason::Starting;
    else if (!StrengthAllowsWork(cfg))
        out.reason = NrExternalProviderReason::Disabled;
    else
        out.reason = NrExternalProviderReason::Ready;
}

// Unified entry bookkeeping for the native Feature1 path. Does not create a
// fake Feature1; it only stamps truthful Native provenance when the existing
// Feature1 NR chain is the active owner for a frame.
inline void NoteNativeFeature1Frame(Session &session, std::uint64_t view_id,
                                    std::uint64_t frame_id, bool nr_ran) noexcept {
    if (session.owned && session.owner_provider_id != 0 &&
        session.owner_provider_id != 1ull /* native sentinel unused */) {
        // External producer owns NR; native Feature1 must not also claim work.
        return;
    }
    session.last_color_provenance = NrGuideProvenance::Native;
    session.last_depth_provenance = NrGuideProvenance::Native;
    session.last_motion_provenance = NrGuideProvenance::Native;
    session.active_view_id = view_id;
    if (frame_id > session.last_frame_id) session.last_frame_id = frame_id;
    ++session.admitted_frames;
    if (nr_ran) ++session.writeback_frames;
    session.last_bypass = nr_ran ? NrExternalProviderReason::Ready
                                 : NrExternalProviderReason::IdentityPassthrough;
}

inline void FillSubmitResult(const AdmitDecision &d, const NrExternalFrameV1 &frame,
                             bool writeback, NrExternalSubmitResultV1 &out) noexcept {
    out = NrExternalSubmitResultV1{};
    out.reason = d.reason;
    out.writeback_performed = writeback ? 1u : 0u;
    out.admitted_frame_id = d.admit ? frame.frame_id : 0;
    out.history_reset = d.history_reset ? 1u : 0u;
    out.color_provenance = static_cast<std::uint32_t>(frame.color.provenance);
    const char *name = NrExternalProviderReasonName(d.reason);
    std::memset(out.bypass, 0, sizeof(out.bypass));
    for (std::size_t i = 0; name[i] && i + 1 < sizeof(out.bypass); ++i)
        out.bypass[i] = name[i];
}

} // namespace nr_external_provider

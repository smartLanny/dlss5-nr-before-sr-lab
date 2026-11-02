#pragma once

// Both buffers are members of heap-owned capture/package objects. Keep one
// fixed bound from the live writer through CreateJob, including the final NUL.
namespace nr_feedback_limits {
// Bounded F8-only CPU storage: legacy metadata plus two allocation snapshots
// and the separately capped Esc trace. This does not allocate GPU resources.
inline constexpr unsigned int kMetadataBytes = 288u * 1024u;
inline constexpr unsigned int kFinalStatusReserveBytes = 256u;
// The complete-TU capacity fixture covers full-width timing, route and
// descriptor/boundary records, an 8 KiB worker envelope and 2 KiB for private
// counter/failure width variation. Keep the same-call identities, separate
// descriptor pools and per-coordinate storage comparison in the reserved end,
// while retaining room for start/timeline records and the start ledger.
// Add 25 KiB for the maximum end allocation ledger and 24 KiB for Esc text.
// The start allocation ledger fits before the timeline reserve gate.
// The end-only retirement POD formatter has a separate worst-width CPU gate.
// Keep the physical metadata buffer unchanged. All context masks fit; optional
// holder detail has explicit omitted counts, while the full POD stays bounded.
inline constexpr unsigned int kRetirementTextBytes = 24u * 1024u;
inline constexpr unsigned int kMandatoryTailBytes = 185u * 1024u;
inline constexpr unsigned int kModuleInventoryBytes = 4u * 1024u;
// The timeline gate runs before an observation burst, which may also append
// the bounded NGX/resource summary on observations 1 and 60.
inline constexpr unsigned int kTimelineBurstBytes = 4u * 1024u;
inline constexpr unsigned int kTimelineReserveBytes = kMandatoryTailBytes + kTimelineBurstBytes;
static_assert(kFinalStatusReserveBytes < kMandatoryTailBytes);
static_assert(kTimelineReserveBytes < kMetadataBytes);
}

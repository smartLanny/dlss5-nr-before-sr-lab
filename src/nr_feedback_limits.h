#pragma once

// Both buffers are members of heap-owned capture/package objects. Keep one
// fixed bound from the live writer through CreateJob, including the final NUL.
namespace nr_feedback_limits {
inline constexpr unsigned int kMetadataBytes = 160u * 1024u;
inline constexpr unsigned int kFinalStatusReserveBytes = 256u;
// The complete-TU capacity fixture covers full-width timing, route and
// descriptor/boundary records, an 8 KiB worker envelope and 2 KiB for private
// counter/failure width variation. Its mandatory end uses <88 KiB. Preserve
// that evidence and leave a separate 60 KiB start/timeline budget.
inline constexpr unsigned int kMandatoryTailBytes = 96u * 1024u;
inline constexpr unsigned int kModuleInventoryBytes = 4u * 1024u;
// The timeline gate runs before an observation burst, which may also append
// the bounded NGX/resource summary on observations 1 and 60.
inline constexpr unsigned int kTimelineBurstBytes = 4u * 1024u;
inline constexpr unsigned int kTimelineReserveBytes = kMandatoryTailBytes + kTimelineBurstBytes;
static_assert(kFinalStatusReserveBytes < kMandatoryTailBytes);
static_assert(kTimelineReserveBytes < kMetadataBytes);
}

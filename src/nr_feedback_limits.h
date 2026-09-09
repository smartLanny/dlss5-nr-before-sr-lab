#pragma once

// Both buffers are members of heap-owned capture/package objects. Keep one
// fixed bound from the live writer through CreateJob, including the final NUL.
namespace nr_feedback_limits {
inline constexpr unsigned int kMetadataBytes = 128u * 1024u;
inline constexpr unsigned int kFinalStatusReserveBytes = 256u;
inline constexpr unsigned int kMandatoryTailBytes = 64u * 1024u;
// The timeline gate runs before an observation burst, which may also append
// the bounded NGX/resource summary on observations 1 and 60.
inline constexpr unsigned int kTimelineBurstBytes = 4u * 1024u;
inline constexpr unsigned int kTimelineReserveBytes = kMandatoryTailBytes + kTimelineBurstBytes;
static_assert(kFinalStatusReserveBytes < kMandatoryTailBytes);
static_assert(kTimelineReserveBytes < kMetadataBytes);
}

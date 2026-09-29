#pragma once

// This is a same-frame post-native experiment, independent of the low-resolution
// reconstruction and private NGX features. Zero is the persisted safe default.
namespace nr_spatial_experiment {
enum class Mode : unsigned { Off=0, AreaGuided=1, LocalDelta=2 };
inline unsigned Normalize(unsigned value) { return value<=2u ? value : 0u; }
enum class Reason : unsigned { Off, WaitingRoute, Conflict, UnsupportedFormat,
    UnsupportedExtent, NeedLowerScale, BudgetCap, BudgetHeadroom, MemoryUnknown,
    Allocation, Pipeline, Busy, Recorded, Submitted, Discarded };
}

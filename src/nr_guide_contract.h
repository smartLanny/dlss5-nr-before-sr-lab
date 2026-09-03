#pragma once

// Semantic resource identity: allocation pointers and per-frame camera jitter
// are deliberately absent. Rotating equivalent game textures share history.
struct NrGuideSignature {
    unsigned long long depth_physical_width = 0;
    unsigned long long motion_physical_width = 0;
    unsigned long long color_physical_width = 0;
    unsigned int depth_physical_height = 0;
    unsigned int motion_physical_height = 0;
    unsigned int color_physical_height = 0;
    unsigned int depth_format = 0;
    unsigned int motion_format = 0;
    unsigned int color_format = 0;
    unsigned int depth_width = 0, depth_height = 0;
    unsigned int motion_source_width = 0, motion_source_height = 0;
    unsigned int motion_bound_width = 0, motion_bound_height = 0;
    unsigned int depth_x = 0, depth_y = 0;
    unsigned int motion_x = 0, motion_y = 0;
    unsigned int color_x = 0, color_y = 0;
    float motion_scale_x = 1.0f, motion_scale_y = 1.0f;
    int normalized_create_flags = 0;
    int depth_inverted = 0;
    bool resample_motion = false;
};

inline bool SameNrGuideStorageContract(const NrGuideSignature &a,
                                       const NrGuideSignature &b) {
    return a.depth_physical_width == b.depth_physical_width &&
           a.depth_physical_height == b.depth_physical_height &&
           a.depth_format == b.depth_format &&
           a.motion_physical_width == b.motion_physical_width &&
           a.motion_physical_height == b.motion_physical_height &&
           a.motion_format == b.motion_format &&
           a.color_physical_width == b.color_physical_width &&
           a.color_physical_height == b.color_physical_height &&
           a.color_format == b.color_format &&
           a.depth_width == b.depth_width && a.depth_height == b.depth_height &&
           a.motion_source_width == b.motion_source_width &&
           a.motion_source_height == b.motion_source_height &&
           a.motion_bound_width == b.motion_bound_width &&
           a.motion_bound_height == b.motion_bound_height &&
           a.normalized_create_flags == b.normalized_create_flags &&
           a.resample_motion == b.resample_motion;
}

inline bool SameNrGuideMappingContract(const NrGuideSignature &a,
                                       const NrGuideSignature &b) {
    return SameNrGuideStorageContract(a, b) &&
           a.depth_x == b.depth_x && a.depth_y == b.depth_y &&
           a.motion_x == b.motion_x && a.motion_y == b.motion_y &&
           a.color_x == b.color_x && a.color_y == b.color_y &&
           a.motion_scale_x == b.motion_scale_x &&
           a.motion_scale_y == b.motion_scale_y &&
           a.depth_inverted == b.depth_inverted;
}

// Commit only successful temporal evaluations. In particular a failed Reset
// must not make the next evaluation believe the new mapping already applied.
struct NrGuideHistory {
    NrGuideSignature committed = {};
    bool initialized = false;

    bool NeedsReset(const NrGuideSignature &next) const {
        return !initialized || !SameNrGuideMappingContract(committed, next);
    }

    void CommitOnEvaluate(const NrGuideSignature &next, bool succeeded) {
        if (!succeeded) return;
        committed = next;
        initialized = true;
    }
};

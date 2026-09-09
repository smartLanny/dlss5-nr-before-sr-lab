#pragma once

// Geometry only: no borrowed resource pointers or evidence of GPU completion.
// The selector supports a recent full-view mapping; atlas/sub-view mappings
// remain unavailable until their position in the displayed frame is known.
namespace nr_face_capture_geometry {
struct OutputMapping {
    unsigned width = 0, height = 0;
    bool full_extent = false;
};
struct Sample {
    unsigned source_width = 0, source_height = 0;
    unsigned source_base_x = 0, source_base_y = 0;
    unsigned destination_base_x = 0, destination_base_y = 0;
    unsigned config_generation = 0;
    unsigned long long instance = 0, sampled_at = 0;
    OutputMapping output = {};
};
struct View {
    bool valid = false;
    unsigned source_width = 0, source_height = 0;
    unsigned display_width = 0, display_height = 0;
    unsigned viewport_x = 0, viewport_y = 0;
    unsigned viewport_width = 0, viewport_height = 0;
    unsigned long long sampled_at = 0;
    const char *reason = "no-recent-composition";
};
inline View Resolve(const Sample &s, unsigned current_generation,
                    unsigned long long current_instance, unsigned long long now,
                    unsigned display_width, unsigned display_height) {
    View v{};
    v.sampled_at = s.sampled_at;
    if (!s.instance || s.instance != current_instance || !s.sampled_at ||
        now < s.sampled_at || now - s.sampled_at > 2000 ||
        s.config_generation != current_generation) return v;
    if (!s.source_width || !s.source_height || !display_width || !display_height ||
        s.source_width > 16384 || s.source_height > 16384 ||
        display_width > 32768 || display_height > 32768) {
        v.reason = "unsupported-extent"; return v;
    }
    if (!s.output.full_extent || s.output.width != display_width ||
        s.output.height != display_height) {
        v.reason = "dlss-output-not-full-display"; return v;
    }
    if (s.source_base_x || s.source_base_y ||
        s.destination_base_x || s.destination_base_y) {
        v.reason = "display-subview-unmapped"; return v;
    }
    const double a = static_cast<double>(s.source_width) * display_height;
    const double b = static_cast<double>(s.source_height) * display_width;
    const double difference = a > b ? a - b : b - a;
    if (difference > (a > b ? a : b) * 0.01) {
        v.reason = "display-aspect-unmapped"; return v;
    }
    v.valid = true;
    v.source_width = s.source_width; v.source_height = s.source_height;
    v.display_width = v.viewport_width = display_width;
    v.display_height = v.viewport_height = display_height;
    v.reason = "recent-full-view-composition";
    return v;
}
} // namespace nr_face_capture_geometry

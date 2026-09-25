#pragma once
// Plain immutable capture metadata, copied before the opaque native SR call.
struct NrCaptureParams {
    float game_jitter_x = NAN, game_jitter_y = NAN;
    float nr_jitter_x = NAN, nr_jitter_y = NAN;
    float game_mv_x = NAN, game_mv_y = NAN;
    float nr_mv_x = NAN, nr_mv_y = NAN;
    bool game_jitter_valid = false, nr_jitter_forwarded = false;
    bool game_mv_valid = false, reset_known = false, reset = false;
    unsigned int depth_width = 0, depth_height = 0;
    unsigned int mv_width = 0, mv_height = 0;
    // NGX input-contract receipt, not proof that the game supplied native guides.
    // Logical MV storage and displacement measurement grids are independent.
    bool guide_domain_known = false;
    bool game_mv_low_resolution = false;
    bool mv_fallback_x = false, mv_fallback_y = false;
    unsigned int render_width = 0, render_height = 0;
    unsigned int work_width = 0, work_height = 0;
    unsigned int mv_source_width = 0, mv_source_height = 0;
    unsigned int mv_source_x = 0, mv_source_y = 0;
    float mv_to_work_x = NAN, mv_to_work_y = NAN;
};
namespace nr_f8 {
struct FrameTicket {
    unsigned long long session = 0;
    unsigned int group = 0, index = 0;
};
}

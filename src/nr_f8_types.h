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
};
namespace nr_f8 {
struct FrameTicket {
    unsigned long long session = 0;
    unsigned int group = 0, index = 0;
};
}

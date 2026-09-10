// Picture preferences only. Keep the user's master switch and game-specific
// depth/jitter/R8 contracts. Never clear runtime fault/quarantine latches here.
// Explicit restore uses the same full-resolution work and model defaults as
// a fresh installation. Existing preferences change only when requested.
void RestoreRecommendedPictureSettings(ChainConfig &next) {
    next.mode = 2;
    next.identity_use_original = false;
    next.transfer_strength = 1.0f;
    next.post_transfer_strength = 1.0f;
    next.work_mode = 0;
    next.custom_work_scale = 1.0f;
    next.lighting_lock = 0.20f;
    next.edge_guard = 0.25f;
    next.detail_stability = 0.0f;
    next.high_strength_protection = true;
    next.color_protection = true;
    next.layered_enhancement = false;
    next.independent_chroma_composition = false;
    // The second inference stays opt-in, but its first requested precision is
    // 50%.  Existing saved legal ratios are preserved by the config loader.
    next.nr_passes=1;next.nr_second_numerator=1;next.nr_second_denominator=2;
    next.generic_residual=false;std::fill(next.generic_gains,next.generic_gains+5,1.f);
    next.region_version=1;next.region_enabled=false;next.region_preview=0;
    std::fill(next.region_gains,next.region_gains+11,1.f);
    next.face_protection_version=1;next.face_protection=false;next.face_extra_cap=true;
    next.face_original_luma=0;next.face_original_color=0;next.face_preview=0;
    next.low_grid_edit_field = false;
    next.catmull_edit_restore = false;
    next.low_grid_color_edit_field = false;
    next.nr_input_filter = 0;
    next.layer_tone = 1.0f;
    next.layer_structure = 1.0f;
    next.layer_bright = 1.0f;
    next.layer_shadow = 1.0f;
    next.motion_stability = false;
    next.proxy_curve = 1;
    next.allow_unverified_hdr_color = false;
    next.suppress_reset_frame = true;
    next.color_strength = 1.0f;
    nr_light_style::Reset(next);
    next.light_control_mode = 1;
    next.max_luma_ratio = 4.0f;
    next.preserve_lighting = false;
    next.proxy_scale = 1.0f;
    next.experimental_peak_shoulder = false;
    next.warmup_after_create = 2;
    next.retry_original_on_sr_failure = true;
    next.intensity = 1.2f;
    next.local_tone = 1.0f;
    next.local_structure = 1.0f;
    next.skin_structure = -1.0f;
    next.auto_mask = false;
    next.style = 0;
    next.ui_correction = true;
}

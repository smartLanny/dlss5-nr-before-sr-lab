// Picture preferences only. Keep the user's master switch and game-specific
// depth/jitter/R8 contracts. Never clear runtime fault/quarantine latches here.
// This explicit button retains the historical recommended Quality / 0.75
// preset. It intentionally differs from the fresh-install Reference / 1.0
// work default; other fields remain the established recommendation.
void RestoreRecommendedPictureSettings(ChainConfig &next) {
    next.mode = 2;
    next.identity_use_original = false;
    next.transfer_strength = 1.0f;
    next.post_transfer_strength = 1.0f;
    next.work_mode = 1;
    next.custom_work_scale = 0.75f;
    next.lighting_lock = 0.20f;
    next.edge_guard = 0.25f;
    next.detail_stability = 0.0f;
    next.high_strength_protection = true;
    next.layered_enhancement = false;
    next.independent_chroma_composition = false;
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
    next.max_luma_ratio = 4.0f;
    next.preserve_lighting = false;
    next.proxy_scale = 1.0f;
    next.experimental_peak_shoulder = false;
    next.warmup_after_create = 2;
    next.retry_original_on_sr_failure = true;
    next.intensity = 1.0f;
    next.local_tone = 1.0f;
    next.local_structure = 1.0f;
    next.skin_structure = -1.0f;
    next.auto_mask = false;
    next.style = 0;
    next.ui_correction = true;
}

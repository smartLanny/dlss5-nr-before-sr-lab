// Core metadata uses the coherent config copied on Present, never a late g_config
// read from the worker. These are requested settings, not proof of NR execution.
void AppendPictureConfig(FeedbackSession &s, const ChainConfig &config, bool valid) {
    FeedbackAppend(s, "requested.pictureSnapshotValid=%d\n", valid ? 1 : 0);
    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.
    FeedbackAppend(s,
        "requested.highStrengthProtection=%d\nrequested.layeredEnhancement=%d\n"
        "requested.layerTone=%.9g\nrequested.layerStructure=%.9g\n"
        "requested.layerBright=%.9g\nrequested.layerShadow=%.9g\n"
        "requested.unverifiedHdrColor=%d\n"
        "requested.lightStyleVersion=%u\nrequested.lightPreset=%u\nrequested.lightPreview=%u\nrequested.lightControlMode=%u\n"
        "requested.lightBroad=%.9g\nrequested.lightDark=%.9g\n"
        "requested.lightReflection=%.9g\nrequested.lightStructure=%.9g\n"
        "requested.lightGlow=%.9g\nrequested.lightPresetColor=%.9g\n"
        "requested.pictureContract=configuration snapshot; F7 codec flags describe submitted composition, not measured quality\n",
        config.high_strength_protection ? 1 : 0, config.layered_enhancement ? 1 : 0,
        static_cast<double>(config.layer_tone), static_cast<double>(config.layer_structure),
        static_cast<double>(config.layer_bright), static_cast<double>(config.layer_shadow),
        config.allow_unverified_hdr_color ? 1 : 0,
        config.light_style_version, config.light_preset, config.light_preview, config.light_control_mode,
        static_cast<double>(config.light_broad), static_cast<double>(config.light_dark),
        static_cast<double>(config.light_reflection), static_cast<double>(config.light_structure),
        static_cast<double>(config.light_glow), static_cast<double>(config.light_preset_color));
}

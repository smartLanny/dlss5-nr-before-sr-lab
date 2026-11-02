// Core metadata uses the coherent config copied on Present, never a late g_config
// read from the worker. These are requested settings, not proof of NR execution.
void AppendPictureConfig(FeedbackSession &s, const ChainConfig &config, bool valid) {
    FeedbackAppend(s, "requested.pictureSnapshotValid=%d\n", valid ? 1 : 0);
    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.
    FeedbackAppend(s,"requested.spatialExperimentMode=%u\n",config.spatial_experiment_mode);
    FeedbackAppend(s,"requested.privateResidualSR=%d\n",config.private_residual_sr?1:0);
    const char* private_gate=!config.private_residual_sr?"off":!config.enabled?"master-off":
        config.temporal_backend?"residual-stabilizer-conflict":
        static_cast<nr_processing::Start>(nr_processing_runtime::effective.load())!=nr_processing::Start::After?"unsupported-current-route":
        (g_last_carrier_feature.load()!=1&&g_last_carrier_feature.load()!=13)?"waiting-native-Feature1-or-13":"consult-last-event-and-counter-deltas";
    FeedbackAppend(s,"requested.privateSR.configurationGate=%s\n",private_gate);
    FeedbackAppend(s,"requested.reconstructionMode=%d\nrequested.nearBlackChromaGuard=%d\n",
        config.reconstruction_mode,config.near_black_chroma_guard?1:0);
    FeedbackAppend(s,"reconstruction.contract=RegionFlags bits8-9 paired source-guided 4/16; bit10 continuous near-black chroma; same-call encoding; unchanged model/history; preserves chosen luminance; requested or recorded is not GPU/pixel proof; capture P/N uses effective pre-generic pair; optional detail-decision plane retains original detail estimator\n");

    FeedbackAppend(s, "requested.colourEdgeRecovery=%d\ncolourEdge.contract=bit24 paired positive weights; actual captured N kept; recorded not GPU completion\n",
        config.colour_edge_recovery);
    FeedbackAppend(s, "requested.colourNeutralProbe=%d\ncolourStage.contract=bit25 synthetic consumer N=P; real captured N unchanged; detail-decision plane is legacy, not the early consumer; output-only, session-local, not GPU completion\n",
        config.colour_neutral_probe ? 1 : 0);
    FeedbackAppend(s, "requested.colourLabMode=%d\nrequested.colourRestoreMode=%d\n"
        "requested.colourRestore=%s\nrequested.colourInputCurve=%d\nrequested.colourInputCurveName=%s\n"
        "requested.colourScaledProxyMode=%d\nrequested.colourScaledProxy=%s\n"
        "requested.colourRestoreContract=bit30 RGB, bit29 post EncodeCS, bit28 N2 HDR, bit27 raw edit reference, bit26 proxy-first scaled input; not GPU completion\n",
        static_cast<int>(nr_colour_lab::Normalize(config.colour_lab_mode)),
        nr_colour_restore::Normalize(config.colour_restore_mode),
        nr_colour_restore::Name(config.colour_restore_mode),
        config.colour_input_curve, nr_colour_restore::CurveName(config.colour_input_curve),
        config.colour_scaled_proxy_mode, nr_colour_restore::ScaledProxyName(config.colour_scaled_proxy_mode));
    FeedbackAppend(s,"requested.regionEnabled=%d\nrequested.regionVersion=%u\nrequested.regionPreview=%u\n"
        "requested.regionGains=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n"
        "requested.regionGainOrder=broadBrightLuma,broadBrightColour,fineBrightLuma,fineBrightColour,broadDarkLuma,broadDarkColour,fineDarkLuma,fineDarkColour,specularLuma,specularColour,pureColour\n"
        "requested.regionColourContract=multiplies effective global colour; consult color.status and color.reason; not proof of visible change\n",
        config.region_enabled?1:0,config.region_version,config.region_preview,
        static_cast<double>(config.region_gains[0]),static_cast<double>(config.region_gains[1]),
        static_cast<double>(config.region_gains[2]),static_cast<double>(config.region_gains[3]),
        static_cast<double>(config.region_gains[4]),static_cast<double>(config.region_gains[5]),
        static_cast<double>(config.region_gains[6]),static_cast<double>(config.region_gains[7]),
        static_cast<double>(config.region_gains[8]),static_cast<double>(config.region_gains[9]),static_cast<double>(config.region_gains[10]));
    FeedbackAppend(s,"requested.nrPasses=%u\narchived.nrSecondScale=%u/%u\n"
        "requested.genericResidual=%d\nrequested.genericGains=%.9g,%.9g,%.9g,%.9g,%.9g\n"
        "requested.genericContract=%s\n",
        config.nr_passes,config.nr_second_numerator,config.nr_second_denominator,config.generic_residual?1:0,
        static_cast<double>(config.generic_gains[0]),static_cast<double>(config.generic_gains[1]),static_cast<double>(config.generic_gains[2]),static_cast<double>(config.generic_gains[3]),static_cast<double>(config.generic_gains[4]),
        config.generic_algorithm==1u?
            "verified-sRGB-or-linear-709 final composition; Cartesian colour candidate and independent linear-luma signal; final Core Y/gamut/colour-budget retained; raw and low-grid field consumers excluded; unknown/HDR retained without new controls; no extra NR inference":
            "normalized model-domain directional RGB and relative HSL; D final protection retained; not material masks");
    FeedbackAppend(s,"requested.genericAlgorithm=%u\nrequested.genericAlgorithmName=%s\n",
        config.generic_algorithm,nr_generic::AlgorithmName(config.generic_algorithm));
    FeedbackAppend(s,"requested.uniformChainVersion=1\nrequested.activeLayers=%u\nrequested.uniformMigrationChangedWork=%d\n",
        nr_layers::Active(config.nr_layers),config.nr_uniform_migrated?1:0);
    for(unsigned i=0;i<nr_layers::Count;++i){
        const auto m=nr_layers::Read(config,i);
        FeedbackAppend(s,"requested.layer%u.enabled=%d\nrequested.layer%u.model=%.9g,%.9g,%.9g,%.9g,%d,%u,%d\n",
            i+1,i==0||config.nr_layers[i-1].enabled,i+1,
            double(m.intensity),double(m.local_structure),double(m.local_tone),double(m.skin_structure),
            m.auto_mask?1:0,m.style,m.ui_correction?1:0);
    }
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

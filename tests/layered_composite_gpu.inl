// Included after the existing Run helper; dispatches the real production HLSL.
unsigned int ValidateLayeredComposition(Gpu &gpu, bool negative_control) {
    unsigned int scenarios = 0;
    const UINT layer_flag = negative_control ? 0u : 2048u;
    const auto same = [](const RunResult &a, const RunResult &b) {
        return a.output.size() == b.output.size() &&
            std::memcmp(a.output.data(), b.output.data(), a.output.size()*sizeof(Float4)) == 0;
    };
    const auto centre = [](const RunResult &r) { return r.output[1u*17u+2u]; };
    const auto luma = [](Float4 p) { return .2126f*p.x + .7152f*p.y + .0722f*p.z; };
    const Float4 neutral{1.f,1.f,1.f,1.f};
    const Float4 no_bright{1.f,1.f,0.f,1.f};
    const Float4 no_dark{1.f,1.f,1.f,0.f};

    const auto bright_off = Run(gpu,13,9,0,1,1u|layer_flag,8,true,7,5,{},0.f,{},2.f,no_bright);
    const auto b = centre(bright_off);
    Require(std::abs(b.x-.60f)<.0001f && std::abs(b.y-.55f)<.0001f && std::abs(b.z-.50f)<.0001f,
            "LAYER_DIRECTION_ORACLE");
    ++scenarios;
    const auto bright_on = Run(gpu,13,9,0,1,1u,8,true,7,5,{},0.f,{},2.f);
    Require(luma(centre(bright_on)) > luma(b)+.05f,
            "Layer fixture must contain a real positive luminance edit");
    ++scenarios;
    const auto dark_off = Run(gpu,13,9,0,1,1u|layer_flag,11,true,7,5,{},0.f,{},2.f,no_dark);
    const auto d = centre(dark_off);
    Require(std::abs(d.x-.60f)<.0001f && std::abs(d.y-.55f)<.0001f && std::abs(d.z-.50f)<.0001f,
            "Layer darkening control did not remove its luminance contribution");
    ++scenarios;
    const auto dark_old = Run(gpu,13,9,0,1,1u,11,true,7,5,{},0.f,{},2.f);
    const auto dark_bright_knob = Run(gpu,13,9,0,1,1u|layer_flag,11,true,7,5,{},0.f,{},2.f,no_bright);
    Require(same(dark_old,dark_bright_knob), "Brightening knob changed a dark edit");
    ++scenarios;
    const auto bright_dark_knob = Run(gpu,13,9,0,1,1u|layer_flag,8,true,7,5,{},0.f,{},2.f,no_dark);
    Require(same(bright_on,bright_dark_knob), "Darkening knob changed a bright edit");
    ++scenarios;

    for (UINT flags : {0u,1u,16u,32u,64u,128u,192u}) {
        for (float strength : {0.f,.5f,1.f,1.2f,2.f,4.f}) {
            for (UINT guard : {0u,1024u}) {
                const auto legacy = Run(gpu,13,9,0,1,flags|guard,0,true,7,5,{},0.f,{},strength);
                const auto enabled = Run(gpu,13,9,0,1,flags|guard|layer_flag,0,true,7,5,{},0.f,{},strength,neutral);
                const auto disabled = Run(gpu,13,9,0,1,flags|guard,0,true,7,5,{},0.f,{},strength,{0.f,2.f,0.f,2.f});
                Require(same(legacy,enabled), "Neutral layers changed baseline bytes");
                Require(same(legacy,disabled), "Disabled layers leaked their stored preferences");
                scenarios += 2;
            }
        }
        for (int variant : {1,2,3,4}) {
            const float strength = variant == 2 ? -1.f : 4.f;
            const auto legacy = Run(gpu,13,9,0,1,flags|1024u,variant,true,0,0,{},0.f,{},strength);
            const auto enabled = Run(gpu,13,9,0,1,flags|1024u|layer_flag,variant,true,0,0,{},0.f,{},strength,{.5f,2.f,.5f,2.f});
            if (variant != 3) Require(same(legacy,enabled), "Layers changed zero/identity/reset output");
            // Run checks nonfinite fixture output, alpha and outside-rect sentinels.
            ++scenarios;
        }
        const auto old_post = Run(gpu,13,9,0,1,flags,0,false,0,0,{},0.f,{},4.f);
        const auto forced = Run(gpu,13,9,0,1,flags|layer_flag,0,false,0,0,{},0.f,{},4.f,{0.f,2.f,0.f,2.f});
        Require(same(old_post,forced), "Forced layer bit changed Mode1 reference");
        ++scenarios;
    }
    const Float4 custom{.65f,1.5f,.75f,1.25f};
    const auto uncaptured = Run(gpu,13,9,0,1,1u|1024u|layer_flag,6,true,7,5,{},.3f,{},4.f,custom);
    const auto captured = Run(gpu,13,9,1,1,1u|1024u|512u|layer_flag,6,true,7,5,{},.3f,{3,2,5,4},4.f,custom);
    Require(same(uncaptured,captured), "Layer F7 ROI capture perturbed composition");
    ++scenarios;
    for (int variant : {6,7,8,9,10,11}) {
        for (float strength : {1.f,2.f,4.f}) {
            const UINT domain = variant == 9 ? 0u : 1u;
            const auto r = Run(gpu,13,9,0,1,domain|1024u|layer_flag,variant,true,7,5,{},.3f,{},strength,custom);
            const auto p = centre(r);
            Require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z), "Layer output not finite");
            if (variant == 9) Require(p.x>1.f, "Layers applied an SDR clamp to source HDR highlights");
            else Require(p.x>=0.f && p.x<=1.000001f && p.y>=0.f && p.y<=1.000001f && p.z>=0.f && p.z<=1.000001f,
                         "Layer SDR output outside storage domain");
            ++scenarios;
        }
    }
    std::printf("Layered production WARP oracle: %u scenarios passed; one-inference composition only.\n",scenarios);
    return scenarios;
}

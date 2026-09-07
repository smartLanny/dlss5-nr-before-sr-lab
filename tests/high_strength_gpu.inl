// Included by the existing codec harness after Run. Uses the extracted
// production CombineV2CS with real D3D12 dispatch and readback.
unsigned int ValidateHighStrengthV3(Gpu &gpu, bool negative_control) {
    unsigned int scenarios = 0;
    const UINT guard = negative_control ? 0u : 1024u;
    const auto same = [](const RunResult &a, const RunResult &b) {
        return a.output.size() == b.output.size() &&
            std::memcmp(a.output.data(), b.output.data(),
                        a.output.size() * sizeof(Float4)) == 0;
    };
    const auto centre = [](const RunResult &r) {
        // Run's destination base is (2,1), width is 13+2+2.
        return r.output[1u * 17u + 2u];
    };
    const auto luma = [](Float4 p) {
        return .2126f*p.x + .7152f*p.y + .0722f*p.z;
    };

    // A deliberate fixture that the old 8x path clips to white. A disabled
    // guard must fail this exact colour assertion, not merely crash elsewhere.
    const auto bright = Run(gpu,13,13,0,1,1u|guard,12,true,7,5,{},0.f,{},8.f);
    const Float4 p = centre(bright);
    Require(p.x > .84f && p.x <= 1.000001f && p.z > .70f &&
            p.x-p.z > .03f && luma(p) < .99f,
            "V3_SDR_EXTRA_BOOST_COLOUR");
    ++scenarios;

    // <=1x takes the original arithmetic; no new colour/exposure transform.
    for (UINT flags : {0u,1u,16u,32u,64u,128u,192u}) {
        for (float strength : {0.f,.5f,1.f}) {
            const auto old = Run(gpu,13,13,0,1,flags,0,true,7,5,{},0.f,{},strength);
            const auto safe = Run(gpu,13,13,0,1,flags|guard,0,true,7,5,{},0.f,{},strength);
            Require(same(old,safe), "V3 changed the 0-1x anchor");
            ++scenarios;
        }
        for (int variant : {1,2,3,4}) {
            // variant2 selects zero transfer itself; 1 identity, 3 invalid,
            // 4 reset suppression. Run also checks alpha and subrect sentinels.
            const float strength = variant == 2 ? -1.f : 8.f;
            const auto old = Run(gpu,13,13,0,1,flags,variant,true,0,0,{},0.f,{},strength);
            const auto safe = Run(gpu,13,13,0,1,flags|guard,variant,true,0,0,{},0.f,{},strength);
            if (variant != 3) Require(same(old,safe), "V3 changed identity/zero/reset output");
            ++scenarios;
        }
        // Mode1 does not use CombineV2CS; even a forced bit cannot affect it.
        const auto old = Run(gpu,13,13,0,1,flags,0,false,0,0,{},0.f,{},8.f);
        const auto forced = Run(gpu,13,13,0,1,flags|guard,0,false,0,0,{},0.f,{},8.f);
        Require(same(old,forced), "V3 modified the full-resolution Mode1 reference");
        ++scenarios;
    }
    float previous = 0.f;
    for (float strength : {1.f,1.2f,2.f,4.f,8.f}) {
        const auto safe = Run(gpu,13,13,0,1,1u|guard,12,true,7,5,{},0.f,{},strength);
        const auto legacy = Run(gpu,13,13,0,1,1u,12,true,7,5,{},0.f,{},strength);
        const auto now = centre(safe);
        Require(luma(now)+.000002f >= previous, "V3 SDR boost is not monotone");
        Require(luma(now) <= luma(centre(legacy))+.000002f, "V3 invented extra luminance");
        Require(now.x <= 1.000001f && now.y >= 0.f && now.z >= 0.f,
                "V3 SDR extra boost left the display cube");
        previous = luma(now);
        ++scenarios;
    }
    const auto capture = Run(gpu,13,13,1,1,1u|512u|guard,12,true,7,5,{},0.f,{3,2,5,4},8.f);
    Require(same(bright,capture), "V3 F7 capture changed the output");
    ++scenarios;

    for (float strength : {1.f,2.f,4.f,8.f}) {
        const auto hdr = Run(gpu,13,13,0,1,guard,13,true,7,5,{},0.f,{},strength);
        const auto old = Run(gpu,13,13,0,1,0u,13,true,7,5,{},0.f,{},strength);
        const auto h = centre(hdr);
        Require(h.x > 1.f && luma(h) >= 4.7067f &&
                luma(h) <= luma(centre(old))+.00002f,
                "V3 applied an SDR ceiling to HDR or lost source highlight energy");
        const auto chroma = Run(gpu,13,13,0,1,1u|guard,14,true,7,5,{},0.f,{},strength);
        const auto c = centre(chroma);
        Require(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) &&
                c.x >= 0.f && c.x <= 1.f && c.y >= 0.f && c.y <= 1.f &&
                c.z >= 0.f && c.z <= 1.f,
                "V3 chroma amplification was non-finite or unbounded");
        scenarios += 2;
    }
    std::printf("V3 production WARP boost oracle: %u scenarios passed (not game tests).\n", scenarios);
    return scenarios;
}

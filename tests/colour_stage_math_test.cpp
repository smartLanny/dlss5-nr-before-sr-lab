// Included AFTER the production math and sampling functions by the runner.
// Texture loads are host substitutes. This is not D3D12/NGX execution.
static unsigned checks = 0;
void require(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s at %u\n", label, checks); std::exit(1); }
}
void close(float3 a, float3 b, float epsilon, const char* label) {
    require(ColourLabAbsPeak(a-b) <= epsilon, label);
}
int main() {
    using namespace nr_colour_restore;
    static_assert(nr_colour_stage::Flags(true,true,Encoding::Unknown)==0);
    static_assert(nr_colour_stage::Flags(true,false,Encoding::V2)==0);
    static_assert(nr_colour_stage::Flags(false,true,Encoding::Post)==0);
    static_assert(nr_colour_stage::Flags(true,true,Encoding::Post)==1778384896u);
    static_assert(nr_colour_stage::Flags(true,true,Encoding::V2)==1241513984u);
    constexpr EncodingReceipt receipt{Encoding::V2, 10};
    static_assert(nr_colour_stage::Flags(true,true,receipt.For(9))==0);
    static_assert(nr_colour_stage::Flags(true,true,receipt.For(0))==0);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    float3 source{.2f,.3f,.4f}, edit{.04f,-.02f,.06f};
    Passthrough = 0;
    auto raw=ColourStageRestore(source,source,source,source+edit,true,1,1,1,8);
    require(raw.valid,"raw valid");close(raw.rgb,source+edit,2e-6f,"raw before aesthetic controls");
    for(float transfer: {0.0f,-1.0f,0.0000001f}) {
        auto r=ColourStageRestore(source,source,source,source+edit,true,transfer,1,1,8);
        require(r.valid,"master-off valid");close(r.rgb,source,0,"master-off exact identity");
    }
    auto notReady=ColourStageRestore(source,source,source,source+edit,true,1,0,1,8);
    close(notReady.rgb,source,0,"readiness preserved");
    require(!ColourStageRestore(source,source,source,source+edit,false,1,1,1,8).valid,"no stale/invalid sample admitted");
    for(float bad: {nan,inf}) {
        require(!ColourStageRestore(source,source,source,source+edit,true,bad,1,1,8).valid,"invalid transfer");
        require(!ColourStageRestore(source,source,source,source+edit,true,1,bad,1,8).valid,"invalid readiness");
        require(!ColourStageRestore(source,source,source,{bad,.2f,.3f},true,1,1,1,8).valid,"invalid neural");
        require(!ColourStageRestore(source,source,source,source+edit,true,1,1,bad,8).valid,"invalid colour");
        require(!ColourStageRestore(source,source,source,source+edit,true,1,1,1,bad).valid,"invalid ratio");
    }
    close(ColourStageRestore(source,source,source,source+edit,true,1,1,1,1).rgb,source,0,"ratio one remains identity");
    require(!ColourStageRestore({-.1f,.3f,.4f},source,source,source+edit,true,1,1,1,8).valid,"signed source rejected");
    for(unsigned domain: {0u,1u,16u,32u,128u}) {
        Passthrough=domain;
        for(unsigned i=0;i<10000;++i) {
            float peak=.08f+float(i%613)/1000.0f;
            float3 s{peak*.7f,peak*.2f,peak};
            float compression=.01f+float(i%79)/100.0f;
            float3 p=s*compression;
            auto identity=ColourStageRestore(s,p,p,p,true,2,1,2,4);
            require(identity.valid,"N=P valid");close(identity.rgb,s,0,"N=P exact source identity");
            float3 desired=s*1.03f;
            auto v=ColourStageRestore(s,p,p,desired*compression,true,2,1,1,4);
            require(v.valid,"source-scale valid");close(v.rgb,desired,2e-6f,"T>0 raw gain remains unity");
        }
    }
    // Production P sampler and production-derived N sampler: 1:1, 99%, 75%,
    // 50%, one-pixel axes, odd extents, all borders and nonzero source offsets.
    unsigned dims[][4]={{2228,1253,2228,1253},{2228,1253,2208,1240},
        {2228,1253,1671,939},{2228,1253,1114,626},{7,5,3,2},{1,1,1,1},
        {1,31,1,15},{31,1,15,1}};
    for(auto& d:dims) {
        Width=d[0];Height=d[1];ModelWidth=d[2];ModelHeight=d[3];
        for(unsigned pattern=0;pattern<4;++pattern) {
            loadPattern=pattern;
            for(unsigned offset: {0u,17u}) {
                sourceOffset=offset;
                for(unsigned i=0;i<256;++i) {
                    float2 center{float((i*149u)%Width)+.5f,float((i*43u)%Height)+.5f};
                    Passthrough=nr_colour_stage::kNeutralFlag;
                    auto p=SampleProxyLinearV2(center), n=ColourStageSampleNeural(center);
                    close(p,n,0,"neutral pairing uses production sampler coordinates");
                    Passthrough=0;
                    close(ColourStageSampleNeural(center),SampleNeuralLinearV2(center),0,
                          "probe off preserves production N sampler");
                }
            }
        }
    }
    std::printf("colour_stage_math: %u checks passed (host production math/sampling; not GPU/game)\n",checks);
}

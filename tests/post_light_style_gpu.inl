// Optional coordinator hook after the common Run/Gpu declarations. Synthetic
// post-source/Neural data only; this file does not execute a real RR or NR call.
RunResult PostLightRun(Gpu &gpu, int variant, LightCase control={},
                       UINT model_width=0, UINT model_height=0,
                       float colour=0.f, bool marked=true) {
    return Run(gpu,25,23,0,1,1u|(marked?262144u:0u),variant,false,
        model_width,model_height,{},0.f,{},1.f,{1,1,1,1},true,colour,control);
}
Float4 PostLightCenter(const RunResult &r) { return r.output[12u*29u+14u]; }
Float4 PostLightOriginal(const RunResult &r) { return r.original[13u*30u+15u]; }
bool PostLightSame(const RunResult &a,const RunResult &b) {
    return a.output.size()==b.output.size() &&
        std::memcmp(a.output.data(),b.output.data(),a.output.size()*sizeof(Float4))==0;
}
float PostLightY(Float4 p) { return .2126f*p.x+.7152f*p.y+.0722f*p.z; }
void ValidatePostLightStyle(Gpu &gpu) {
    Gpu baseline(kMaintenanceShader);
    unsigned groups=0;
    for(float lighting:{0.f,.2f,1.f}) for(UINT nw:{0u,13u})
        for(int variant:{30,31,32,33}) {
            LightCase neutral;neutral.lighting=lighting;
            const auto old=PostLightRun(baseline,variant,neutral,nw,nw?12u:0u,0.f,false);
            const auto now=PostLightRun(gpu,variant,neutral,nw,nw?12u:0u,0.f,false);
            const auto marked=PostLightRun(gpu,variant,neutral,nw,nw?12u:0u);
            Require(PostLightSame(old,now),"POST_LIGHT_NEUTRAL_BASELINE_BYTES");
            Require(PostLightSame(now,marked),"POST_LIGHT_NEUTRAL_MARKED_BYTES");
            ++groups;
        }
    for(float lighting:{0.f,.2f,1.f}) {
        LightCase neutral;neutral.lighting=lighting;
        LightCase glow=neutral;glow.glow=1.f;
        const auto wide=PostLightRun(gpu,30,glow);
        Require(std::abs(PostLightY(PostLightCenter(wide))-PostLightY(PostLightOriginal(wide)))<1e-5f,
            "POST_LIGHT_GLOW_WIDE_RETURNS_SOURCE_LUMA");
        Require(PostLightSame(PostLightRun(gpu,31,neutral),PostLightRun(gpu,31,glow)),
            "POST_LIGHT_GLOW_WIDE_NEGATIVE_UNCHANGED");
        for(int variant:{32,33}) {
            Float4 original=PostLightCenter(PostLightRun(gpu,variant,neutral));
            Float4 suppressed=PostLightCenter(PostLightRun(gpu,variant,glow));
            Require(std::memcmp(&original,&suppressed,sizeof(Float4))==0,
                "POST_LIGHT_GLOW_FINE_CENTER_UNCHANGED");
        }
        for(bool reflection:{true,false}) {
            LightCase zero=neutral, doubled=neutral;
            if(reflection) { zero.reflection=0.f; doubled.reflection=2.f; }
            else { zero.structure=0.f; doubled.structure=2.f; }
            for(int variant:{30,31,32,33}) {
                const auto off=PostLightRun(gpu,variant,zero);
                const auto on=PostLightRun(gpu,variant,doubled);
                const Float4 offCenter=PostLightCenter(off), onCenter=PostLightCenter(on);
                if(variant==(reflection?32:33)) {
                    // The isolated centre has the opposite sign to every tap.
                    // Zero fine gain must restore source luma; the 2x endpoint
                    // must move it in the target sign, without copying the shader.
                    Require(std::abs(PostLightY(offCenter)-PostLightY(PostLightOriginal(off)))<1e-5f,
                        reflection?"POST_LIGHT_REFLECTION_ZERO_RETURNS_SOURCE_LUMA":
                                   "POST_LIGHT_STRUCTURE_ZERO_RETURNS_SOURCE_LUMA");
                    const auto targetResponse=[reflection](Float4 low,Float4 high) {
                        return reflection?PostLightY(high)>PostLightY(low)+.001f:
                                          PostLightY(high)<PostLightY(low)-.001f;
                    };
                    Require(targetResponse(offCenter,onCenter),
                        reflection?"POST_LIGHT_REFLECTION_0_TO_2_FINE_RESPONSE":
                                   "POST_LIGHT_STRUCTURE_0_TO_2_FINE_RESPONSE");
                    // The same acceptance predicate rejects a reversed response
                    // and an ignored gain, with no extra GPU dispatches.
                    Require(!targetResponse(onCenter,offCenter),"POST_LIGHT_WRONG_SIGN_ORACLE");
                    Require(!targetResponse(offCenter,offCenter),"POST_LIGHT_IGNORED_GAIN_ORACLE");
                } else if(variant<32) {
                    Require(PostLightSame(off,on),
                        reflection?"POST_LIGHT_REFLECTION_0_TO_2_WIDE_UNCHANGED":
                                   "POST_LIGHT_STRUCTURE_0_TO_2_WIDE_UNCHANGED");
                } else {
                    // Only the centre has the non-target sign; its surroundings
                    // may legitimately respond to this control.
                    Require(std::memcmp(&offCenter,&onCenter,sizeof(Float4))==0,
                        reflection?"POST_LIGHT_REFLECTION_NEGATIVE_CENTER_UNCHANGED":
                                   "POST_LIGHT_STRUCTURE_POSITIVE_CENTER_UNCHANGED");
                }
                ++groups;
            }
        }
        LightCase natural{1.f,.85f,1.10f,1.05f,.30f,0u,lighting};
        for(int variant:{0,3,30,31,32,33,34,35})
            (void)PostLightRun(gpu,variant,natural,13,12,.8f);
        groups+=12;
    }
    for(UINT preview:{1u,2u}) {
        LightCase control;control.glow=.7f;control.preview=preview;
        const auto shown=PostLightRun(gpu,32,control);
        Require(PostLightCenter(shown).w==.625f,"POST_LIGHT_PREVIEW_ALPHA");
        ++groups;
    }
    std::printf("Post light style: %u WARP groups; legacy bytes, reflection/structure 0-to-2 response and non-target isolation, independent broad/fine signs, LightingLock 0/.2/1, scaled colour/finite/alpha and previews. Synthetic model planes only.\n",groups);
}

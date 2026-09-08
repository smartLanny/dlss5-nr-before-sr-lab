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
    std::printf("Post light style: %u WARP groups; legacy bytes, independent broad/fine signs, LightingLock 0/.2/1, scaled colour/finite/alpha and previews. Synthetic model planes only.\n",groups);
}

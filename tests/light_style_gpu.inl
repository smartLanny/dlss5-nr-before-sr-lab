// Production shader, synthetic Neural inputs. No NVIDIA/game quality claims.
constexpr UINT kLightFlag=262144u;
float LightLuma(Float4 p) { return .2126f*p.x+.7152f*p.y+.0722f*p.z; }
bool LightEqual(const RunResult &a,const RunResult &b) {
    return a.output.size()==b.output.size() &&
        std::memcmp(a.output.data(),b.output.data(),a.output.size()*sizeof(Float4))==0;
}
RunResult LightRun(Gpu &gpu, int variant, LightCase controls={}, UINT extra=0,
        UINT nw=0, UINT nh=0,float strength=1.f,float color=0.f,bool active=true,bool before=true) {
    try {
        // The old capture oracle cannot compare reconstructed NaN payloads;
        // invalid-input cases still validate output, alpha and subrect guards.
        return Run(gpu,25,23,variant==3?0u:1u,1,1u|extra|(active?kLightFlag:0u),variant,before,nw,nh,
            {},0.f,{},strength,{1.f,1.f,1.f,1.f},false,color,controls);
    } catch (...) {
        std::fprintf(stderr,"Light case variant=%d flags=%u N=%ux%u strength=%g color=%g active=%d before=%d preview=%u\n",variant,extra,nw,nh,strength,color,active,before,controls.preview);
        throw;
    }
}
Float4 LightCenter(const RunResult &r) { return r.output[(23/2+1)*(25+4)+(25/2+2)]; }
constexpr UINT kDirectionalLightFlag=524288u;
RunResult WholeLightRun(Gpu &gpu,int variant,LightCase controls={},bool whole=true,
        float strength=1.f,UINT nw=0,UINT nh=0,float color=0.f,UINT storage=1u) {
    return Run(gpu,25,23,0,1,storage|kLightFlag|(whole?kDirectionalLightFlag:0u),variant,true,
        nw,nh,{},0.f,{},strength,{1,1,1,1},true,color,controls);
}
unsigned ValidateDirectionalLightStyle(Gpu &gpu) {
    Gpu d4(kD4BaselineShader);
    unsigned groups=0;
    for(int variant:{0,18,30,31,32,33}) {
        const UINT nw=variant<30?13u:0u,nh=nw?12u:0u;
        LightCase custom{2.f,.05f,1.91f,1.95f,.7f,0u,.2f};
        Require(LightEqual(WholeLightRun(d4,variant,custom,false,1,nw,nh,.8f),
                           WholeLightRun(gpu,variant,custom,false,1,nw,nh,.8f)),
            "DIRECTION_PRE_MODE0_NONNEUTRAL_D4_BYTES");
        LightCase neutral;neutral.lighting=.2f;
        Require(LightEqual(WholeLightRun(d4,variant,neutral,false,1,nw,nh,.8f),
                           WholeLightRun(gpu,variant,neutral,true,1,nw,nh,.8f)),
            "DIRECTION_PRE_MODE1_NEUTRAL_D4_BYTES");
        ++groups;
    }
    for(float lighting:{0.f,.2f,1.f}) for(bool positive:{true,false})
    for(int variant:{30,31}) {
        LightCase off,on;off.lighting=on.lighting=lighting;
        if(positive){off.reflection=0;on.reflection=2;}
        else {off.structure=0;on.structure=2;}
        const auto zero=WholeLightRun(gpu,variant,off);
        const auto doubled=WholeLightRun(gpu,variant,on);
        if(variant==(positive?30:31)) {
            const float source=LightLuma(zero.original[13u*30u+15u]);
            const float a=LightLuma(LightCenter(zero)),b=LightLuma(LightCenter(doubled));
            Require(std::abs(a-source)<1e-5f,"DIRECTION_PRE_WIDE_ZERO_RETURNS_SOURCE");
            if(lighting<1.f)
                Require(positive?b>a+.001f:b<a-.001f,"DIRECTION_PRE_WHOLE_WIDE_0_TO_2_RESPONSE");
            else Require(std::abs(b-source)<1e-5f,"DIRECTION_PRE_FULL_LOCK_NOT_RESURRECTED");
        } else Require(LightEqual(zero,doubled),"DIRECTION_PRE_NON_TARGET_SIGN_BYTES");
        ++groups;
    }
    for(float lighting:{0.f,.2f,1.f}) for(int variant:{30,31,32,33}) {
        LightCase base{2.f,.5f,2.f,2.f,0.f,0u,lighting};
        LightCase glow=base;glow.glow=1;
        const auto a=WholeLightRun(gpu,variant,base),b=WholeLightRun(gpu,variant,glow);
        if(variant==30)
            Require(std::abs(LightLuma(LightCenter(b))-LightLuma(b.original[13u*30u+15u]))<1e-5f,
                "DIRECTION_PRE_GLOW100_REMOVES_WIDE_AT_2X");
        else if(variant==31)Require(LightEqual(a,b),"DIRECTION_PRE_GLOW_NEGATIVE_BYTES");
        else {
            const Float4 ac=LightCenter(a),bc=LightCenter(b);
            Require(std::memcmp(&ac,&bc,sizeof(Float4))==0,"DIRECTION_PRE_GLOW_FINE_CENTER_BYTES");
        }
        ++groups;
    }
    for(int variant:{32,33}) {
        // Eight pixels from the isolated centre, one of four taps has the
        // opposite sign. The broad/fine mixture is real texture input. At .5x
        // master neither comparison is hidden by the legacy ratio ceiling.
        LightCase controls;controls.reflection=controls.structure=2;
        const auto old=WholeLightRun(d4,variant,controls,false,.5f);
        const auto now=WholeLightRun(gpu,variant,controls,true,.5f);
        const float a=LightLuma(old.output[12u*29u+6u]),b=LightLuma(now.output[12u*29u+6u]);
        Require(variant==33?b>a+.001f:b<a-.001f,"DIRECTION_PRE_MIXED_SCOPE_RESPONSE");
        ++groups;
    }
    for(UINT storage:{0u,128u,192u}) for(int variant:{0,18}) for(float strength:{1.f,4.f}) {
        LightCase controls{2.f,.5f,2.f,2.f,.3f,0u,.2f};
        const auto r=WholeLightRun(gpu,variant,controls,true,strength,13,12,.8f,storage);
        for(const auto &p:r.output)
            Require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.w),
                "DIRECTION_PRE_HDR_FINITE");
        const Float4 center=LightCenter(r);
        Require(center.w==.625f,"DIRECTION_PRE_HDR_ALPHA");
        if(storage==0u&&variant==18) {
            const float original=LightLuma(r.original[13u*30u+15u]),actual=LightLuma(center);
            Require(actual>=original*.5f-1e-4f&&actual<=original*2.f+1e-4f,
                "DIRECTION_PRE_HDR_RATIO_BOUND");
            Require(std::max(center.x,std::max(center.y,center.z))>1.f,
                "DIRECTION_PRE_HDR_NOT_CLAMPED_TO_SDR");
        }
        ++groups;
    }
    return groups;
}
void ValidateLightStyle(Gpu &gpu) {
    Gpu baseline(kMaintenanceShader);
    unsigned groups=0;
    for(bool before : {false,true}) for(int variant : {0,1,3,30,31,32,33,34,35})
        for(float strength : {0.f,1.f,4.f}) {
            auto ref=LightRun(baseline,variant,{},0,0,0,strength,0.f,false,before);
            auto now=LightRun(gpu,variant,{},0,0,0,strength,0.f,false,before);
            Require(LightEqual(ref,now),"LIGHT_NEUTRAL_BASELINE_BYTES");
            auto marked=LightRun(gpu,variant,{},0,0,0,strength,0.f,true,before);
            Require(LightEqual(now,marked),"LIGHT_NEUTRAL_MARKED_BYTES");
            ++groups;
        }
    LightCase suppress; suppress.glow=1.f;
    auto bright=LightRun(gpu,30), suppressed=LightRun(gpu,30,suppress);
    Require(LightLuma(LightCenter(suppressed))<LightLuma(LightCenter(bright))-.01f,
        "LIGHT_GLOW_WIDE_POSITIVE");
    Require(LightEqual(LightRun(gpu,31),LightRun(gpu,31,suppress)),"LIGHT_GLOW_NEGATIVE_UNCHANGED");
    auto fine=LightRun(gpu,32),fine_suppressed=LightRun(gpu,32,suppress);
    auto f=LightCenter(fine),fs=LightCenter(fine_suppressed);
    Require(std::memcmp(&f,&fs,sizeof(f))==0,"LIGHT_GLOW_FINE_POSITIVE_UNCHANGED");
    LightCase reflection;reflection.reflection=1.3f;
    Require(LightEqual(LightRun(gpu,30),LightRun(gpu,30,reflection)),"LIGHT_REFLECTION_WIDE_UNCHANGED");
    Require(LightLuma(LightCenter(LightRun(gpu,32,reflection)))>LightLuma(f)+.001f,"LIGHT_REFLECTION_FINE_RESPONSE");
    LightCase structure;structure.structure=1.3f;
    Require(LightEqual(LightRun(gpu,31),LightRun(gpu,31,structure)),"LIGHT_STRUCTURE_WIDE_UNCHANGED");
    Require(LightLuma(LightCenter(LightRun(gpu,33,structure)))<LightLuma(LightCenter(LightRun(gpu,33)))-.001f,"LIGHT_STRUCTURE_FINE_RESPONSE");
    // Independent negative controls: neither global darkening nor all-positive
    // suppression can satisfy the fine-positive and negative invariants.
    Float4 wrong=f;wrong.x*=.7f;wrong.y*=.7f;wrong.z*=.7f;
    Require(std::memcmp(&wrong,&fs,sizeof(f))!=0,"LIGHT_GLOBAL_DIM_NEGATIVE_ORACLE");
    Require(LightLuma(fs)>LightLuma(f)*.8f,"LIGHT_ALL_POSITIVE_NEGATIVE_ORACLE");
    groups+=6;
    for(UINT storage : {0u,16u,32u,64u,128u,192u})
        for(float strength : {0.f,1.f,4.f}) for(int variant : {0,18}) {
            auto invoke=[&](Gpu &target, bool active) {
                return Run(target,25,23,0,1,storage|(active?kLightFlag:0u),variant,true,
                    13,12,{},0.f,{},strength,{1.f,1.f,1.f,1.f},false,.8f,
                    active?LightCase{1.f,.85f,1.10f,1.05f,.30f,0u,.2f}:LightCase{});
            };
            Require(LightEqual(invoke(baseline,false),invoke(gpu,false)),"LIGHT_HDR_NEUTRAL_BYTES");
            auto r=invoke(gpu,true);
            for(const auto &p:r.output) Require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.w),"LIGHT_HDR_FINITE");
            Require(LightCenter(r).w==.625f,"LIGHT_HDR_ALPHA");++groups;
        }
    for(UINT field : {0u,8192u,8192u|16384u,8192u|32768u})
        for(float lighting : {0.f,.2f,1.f}) for(int variant : {0,3,30,31,34,35}) {
            LightCase natural{1.f,.85f,1.10f,1.05f,.30f,0u,lighting};
            const float color=(field==8192u || field==(8192u|16384u))?0.f:.8f;
            auto r=LightRun(gpu,variant,natural,field,13,12,1.f,color);
            for(const auto &p:r.output) Require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.w),"LIGHT_FINITE_OUTPUT");
            Require(LightCenter(r).w==.625f,"LIGHT_ALPHA_PRESERVED");
            ++groups;
        }
    for(UINT preview : {1u,2u}) {
        suppress.preview=preview;
        auto r=LightRun(gpu,32,suppress);
        Require(LightCenter(r).w==.625f,"LIGHT_PREVIEW_ALPHA");++groups;
    }
    const unsigned directional=ValidateDirectionalLightStyle(gpu);groups+=directional;
    std::printf("Light style: %u production WARP groups (%u whole-direction); D4 mode0/nonneutral and mode1/neutral bytes, wide/fine signs, glow, low-grid/colour/Catmull, HDR/alpha and preview. Synthetic Neural only.\n",groups,directional);
}

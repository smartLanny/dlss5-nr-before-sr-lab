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
RunResult WholePostLightRun(Gpu &gpu,int variant,LightCase controls={},bool whole=true,
        float strength=1.f,UINT nw=0,UINT nh=0,float color=0.f,UINT storage=1u) {
    return Run(gpu,25,23,0,1,storage|262144u|(whole?524288u:0u),variant,false,
        nw,nh,{},0.f,{},strength,{1,1,1,1},true,color,controls);
}
unsigned ValidateDirectionalPostLightStyle(Gpu &gpu) {
    Gpu d4(kD4BaselineShader);
    unsigned groups=0;
    for(int variant:{0,18,30,31,32,33}) {
        const UINT nw=variant<30?13u:0u,nh=nw?12u:0u;
        LightCase custom{2.f,.05f,1.91f,1.95f,.7f,0u,.2f};
        Require(PostLightSame(WholePostLightRun(d4,variant,custom,false,1,nw,nh,.8f),
                             WholePostLightRun(gpu,variant,custom,false,1,nw,nh,.8f)),
            "DIRECTION_POST_MODE0_NONNEUTRAL_D4_BYTES");
        LightCase neutral;neutral.lighting=.2f;
        Require(PostLightSame(WholePostLightRun(d4,variant,neutral,false,1,nw,nh,.8f),
                             WholePostLightRun(gpu,variant,neutral,true,1,nw,nh,.8f)),
            "DIRECTION_POST_MODE1_NEUTRAL_D4_BYTES");
        ++groups;
    }
    for(float lighting:{0.f,.2f,1.f}) for(bool positive:{true,false})
    for(int variant:{30,31}) {
        LightCase off,on;off.lighting=on.lighting=lighting;
        if(positive){off.reflection=0;on.reflection=2;}
        else {off.structure=0;on.structure=2;}
        const auto zero=WholePostLightRun(gpu,variant,off);
        const auto doubled=WholePostLightRun(gpu,variant,on);
        if(variant==(positive?30:31)) {
            const float source=PostLightY(PostLightOriginal(zero));
            const float a=PostLightY(PostLightCenter(zero)),b=PostLightY(PostLightCenter(doubled));
            Require(std::abs(a-source)<1e-5f,"DIRECTION_POST_WIDE_ZERO_RETURNS_SOURCE");
            if(lighting<1.f)
                Require(positive?b>a+.001f:b<a-.001f,"DIRECTION_POST_WHOLE_WIDE_0_TO_2_RESPONSE");
            else Require(std::abs(b-source)<1e-5f,"DIRECTION_POST_FULL_LOCK_NOT_RESURRECTED");
        } else Require(PostLightSame(zero,doubled),"DIRECTION_POST_NON_TARGET_SIGN_BYTES");
        ++groups;
    }
    for(float lighting:{0.f,.2f,1.f}) for(int variant:{30,31,32,33}) {
        LightCase base{2.f,.5f,2.f,2.f,0.f,0u,lighting};
        LightCase glow=base;glow.glow=1;
        const auto a=WholePostLightRun(gpu,variant,base),b=WholePostLightRun(gpu,variant,glow);
        if(variant==30)
            Require(std::abs(PostLightY(PostLightCenter(b))-PostLightY(PostLightOriginal(b)))<1e-5f,
                "DIRECTION_POST_GLOW100_REMOVES_WIDE_AT_2X");
        else if(variant==31)Require(PostLightSame(a,b),"DIRECTION_POST_GLOW_NEGATIVE_BYTES");
        else {
            const Float4 ac=PostLightCenter(a),bc=PostLightCenter(b);
            Require(std::memcmp(&ac,&bc,sizeof(Float4))==0,"DIRECTION_POST_GLOW_FINE_CENTER_BYTES");
        }
        ++groups;
    }
    for(int variant:{32,33}) {
        // One opposite-sign tap gives a real broad/fine mixture. Half master
        // keeps both outcomes below the inherited transfer-ratio ceiling.
        LightCase controls;controls.reflection=controls.structure=2;
        const auto old=WholePostLightRun(d4,variant,controls,false,.5f);
        const auto now=WholePostLightRun(gpu,variant,controls,true,.5f);
        const float a=PostLightY(old.output[12u*29u+6u]),b=PostLightY(now.output[12u*29u+6u]);
        Require(variant==33?b>a+.001f:b<a-.001f,"DIRECTION_POST_MIXED_SCOPE_RESPONSE");
        ++groups;
    }
    for(UINT storage:{0u,128u,192u}) for(int variant:{0,18}) for(float strength:{1.f,4.f}) {
        LightCase controls{2.f,.5f,2.f,2.f,.3f,0u,.2f};
        const auto r=WholePostLightRun(gpu,variant,controls,true,strength,13,12,.8f,storage);
        for(const auto &p:r.output)
            Require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.w),
                "DIRECTION_POST_HDR_FINITE");
        const Float4 center=PostLightCenter(r);
        Require(center.w==.625f,"DIRECTION_POST_HDR_ALPHA");
        if(storage==0u&&variant==18) {
            const float original=PostLightY(PostLightOriginal(r)),actual=PostLightY(center);
            Require(actual>=original*.5f-1e-4f&&actual<=original*2.f+1e-4f,
                "DIRECTION_POST_HDR_RATIO_BOUND");
            Require(std::max(center.x,std::max(center.y,center.z))>1.f,
                "DIRECTION_POST_HDR_NOT_CLAMPED_TO_SDR");
        }
        ++groups;
    }
    return groups;
}
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
    const unsigned directional=ValidateDirectionalPostLightStyle(gpu);groups+=directional;
    std::printf("Post light style: %u WARP groups (%u whole-direction); D4 mode0/nonneutral and mode1/neutral bytes, whole/fine signs, LightingLock 0/.2/1, glow, scaled colour, HDR/alpha and previews. Synthetic model planes only.\n",groups,directional);
}

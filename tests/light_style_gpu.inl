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
    std::printf("Light style: %u production WARP groups; baseline bytes, four-region independence, low-grid/colour/Catmull, finite/alpha and preview passed. Synthetic Neural only.\n",groups);
}

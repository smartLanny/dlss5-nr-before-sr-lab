// Actual RGBA32F BuildColorEditFieldCS -> strict sampling -> CombineV2CS.
void ValidateColorEditField(Gpu &gpu) {
#ifdef NR_EDIT_FIELD_BASELINE_SOURCE_SHA
    Gpu baseline(kMaintenanceShader);
#endif
    unsigned cases=0, changed=0, invalid_fields=0;
    for(bool reduced:{false,true}) for(int variant:{0,3,14,17,18})
    for(float colour:{0.f,1.f,2.f}) {
        const UINT nw=reduced?7u:13u,nh=reduced?5u:13u;
        const auto reference=Run(gpu,13,13,0,1,1u,variant,true,nw,nh,
            {},0.f,{},1.f,{1,1,1,1},true,colour);
#ifdef NR_EDIT_FIELD_BASELINE_SOURCE_SHA
        const auto old=Run(baseline,13,13,0,1,1u,variant,true,nw,nh,
            {},0.f,{},1.f,{1,1,1,1},true,colour);
        Require(reference.output.size()==old.output.size() &&
            std::memcmp(reference.output.data(),old.output.data(),
                reference.output.size()*sizeof(Float4))==0,
            "#86 default-off output differs from frozen dev6 shader");
#endif
        const auto candidate=Run(gpu,13,13,0,1,1u|8192u|16384u,variant,
            true,nw,nh,{},0.f,{},1.f,{1,1,1,1},true,colour);
        Require(candidate.edit_field.size()==nw*nh*4u,"RGBA field extent");
        for(UINT i=0;i<nw*nh;++i) {
            const float *f=candidate.edit_field.data()+i*4u;
            Require(std::isfinite(f[0])&&std::isfinite(f[1])&&
                std::isfinite(f[2])&&std::isfinite(f[3]),"finite RGBA field");
            Require(f[3]==0.f||f[3]==1.f||f[3]==3.f,"explicit field validity");
            invalid_fields+=f[3]!=3.f;
        }
        if(colour>0.f && (variant==14||variant==18) &&
            std::memcmp(reference.output.data(),candidate.output.data(),
                reference.output.size()*sizeof(Float4))!=0) ++changed;
        ++cases;
    }
    Require(changed==8,"normal/extreme colour cases did not exercise candidate");
    Require(invalid_fields>0,"near-black/invalid field cases did not fail closed");
    std::printf("#86 RG/RGBA EditField: %u WARP groups; default-off dev6 identity, explicit validity and finite outputs.\n",cases);
}

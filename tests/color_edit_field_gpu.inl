// Actual RGBA32F BuildColorEditFieldCS -> strict sampling -> CombineV2CS.
#include "nr_color_amplification.h"
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
        const auto candidate=Run(gpu,13,13,0,1,1u|8192u|32768u,variant,
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
    // Feed the production budget into both real independent shader consumers.
    // The CPU policy gate separately extracts actual route negotiation. These
    // GPU cases check that its coefficient reaches a useful colour edit while
    // the existing master/identity/confidence guards still retain source pixels.
    unsigned consumer_cases=0;
    for(UINT consumer_flags:{4096u,8192u|32768u}) for(bool reduced:{false,true}) {
        const UINT nw=reduced?7u:13u,nh=reduced?5u:13u;
        const auto anchor=Run(gpu,13,13,0,1,1u|consumer_flags,14,true,nw,nh,
            {},0.f,{},1.f,{1,1,1,1},true,.5f);
        const auto a=anchor.output[7u*17u+8u];
        for(float gain:{2.f,4.f}) {
            const auto budget=nr_color_amplification::ResolveForConsumer(.5f,gain,true,
                nr_color_amplification::ChromaConsumer::GainIndependent);
            const auto corrected=Run(gpu,13,13,0,1,1u|consumer_flags,14,true,nw,nh,
                {},0.f,{},gain,{1,1,1,1},true,budget.coefficient);
            const auto old=Run(gpu,13,13,0,1,1u|consumer_flags,14,true,nw,nh,
                {},0.f,{},gain,{1,1,1,1},true,
                nr_color_amplification::Resolve(.5f,gain,true).coefficient);
            const auto c=corrected.output[7u*17u+8u];
            Require(std::abs(c.x/c.y-a.x/a.y)<.0001f && std::abs(c.z/c.y-a.z/a.y)<.0001f,
                "independent shader chromaticity must not weaken with global gain");
            Require(std::memcmp(corrected.output.data(),old.output.data(),
                corrected.output.size()*sizeof(Float4))!=0,"consumer budget fixes an observable colour regression");
            ++consumer_cases;
        }
        for(int variant:{1,4,14}) {
            const float gain=variant==14?0.f:4.f;
            const auto identity=Run(gpu,13,13,0,1,1u|consumer_flags,variant,true,nw,nh,
                {},0.f,{},gain,{1,1,1,1},true,
                nr_color_amplification::ResolveForConsumer(1.f,gain,true,
                    nr_color_amplification::ChromaConsumer::GainIndependent).coefficient);
            for(UINT y=0;y<13;++y) for(UINT x=0;x<13;++x)
                Require(std::memcmp(&identity.output[(y+1u)*17u+x+2u],
                    &identity.original[(y+2u)*18u+x+3u],sizeof(Float4))==0,
                    "independent colour retains exact source for zero gain/confidence/model edit");
            ++consumer_cases;
        }
    }
    std::printf("#86 RG/RGBA EditField: %u WARP groups; default-off dev6 identity, explicit validity and finite outputs.\n",cases);
    std::printf("Colour consumer budget: %u WARP groups; independent-log/opponent-field, full/reduced grids, gain and exact source guards.\n",consumer_cases);
}

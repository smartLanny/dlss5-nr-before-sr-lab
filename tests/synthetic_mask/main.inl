// Current-frame host admission is separate from probability zero (known background).
struct MaskReceipt { unsigned frame,view,epoch,owner; bool valid; };
bool Admit(MaskReceipt r) { return r.valid&&r.frame==7&&r.view==3&&r.epoch==2&&r.owner==9; }
int main() {
    try {
        unsigned rejected=0;
        for(auto r:{MaskReceipt{7,3,2,9,false},{6,3,2,9,true},{7,4,2,9,true},{7,3,1,9,true},{7,3,2,8,true}}) {
            Require(!Admit(r),"Unknown/stale/foreign mask admitted");++rejected;
        }
        Require(Admit({7,3,2,9,true}),"current synthetic receipt rejected");
        Gpu gpu; unsigned dispatches=0;
        auto run=[&](float global,float face,float probability) {
            synthetic_face=face;synthetic_mask.assign(4,Float4{probability,0,0,0});++dispatches;
            return Run(gpu,4,4,0,1,0,0,true,0,0,{},0,{},global);
        };
        auto identity=run(0,0,0), reference=run(0.4f,0.4f,0);
        auto full_zero=run(4,0,1), independent=run(0,0.4f,1), amplified=run(4,0.4f,1);
        Require(std::memcmp(identity.output.data(),full_zero.output.data(),identity.output.size()*sizeof(Float4))==0,"full mask zero failed strict identity");
        Require(std::memcmp(reference.output.data(),independent.output.data(),reference.output.size()*sizeof(Float4))==0,"global zero suppressed independent strength");
        Require(std::memcmp(reference.output.data(),amplified.output.data(),reference.output.size()*sizeof(Float4))==0,"global multiplier changed full-mask result");
        Require(std::memcmp(identity.output.data(),reference.output.data(),identity.output.size()*sizeof(Float4))!=0,"nonidentity fixture ineffective");
        synthetic_face=0.8f;synthetic_mask={{0,0,0,0},{1,0,0,0},{0,0,0,0},{1,0,0,0}};
        const auto boundary=Run(gpu,4,4,0,1,0,0,true,0,0,{},0,{},0);++dispatches;
        // CPU oracle for centers x={0,1,2,3}: q={-.25,.25,.75,1.25}, m={0,.25,.75,1}.
        const float weights[]={0,0.25f,0.75f,1};
        for(unsigned x=0;x<4;++x) {
            Require(weights[x]>=0&&weights[x]<=1,"negative probability weight");
            const auto expected=run(weights[x]*0.8f,0,0);
            for(unsigned y=0;y<4;++y) {
                const auto i=(y+1)*8+x+2;
                Require(std::memcmp(&expected.output[i],&boundary.output[i],sizeof(Float4))==0,"bilinear center/boundary oracle mismatch");
            }
        }
        std::printf("PASS SyntheticMask dispatches=%u admission_rejections=%u actual_CombineV2_luma_chroma=true independent=true strict_identity=true nonnegative_boundary=true face_recognition=false\n",dispatches,rejected);
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}

float MaxRgbDifference(const Float4& a,const Float4& b){return std::max(std::abs(a.x-b.x),std::max(std::abs(a.y-b.y),std::abs(a.z-b.z)));}
void ValidateFaceComposition(Gpu& gpu) {
    unsigned scenarios=0;
    for(bool before:{false,true})for(UINT flags:{0u,1u,16u,32u,128u})for(int variant:{40,41})
    for(unsigned scale:{100u,75u,50u}) {
        constexpr unsigned w=16,h=12,ow=w+4;
        const unsigned mw=w*scale/100,mh=h*scale/100;
        const UINT actualFlags=flags|(before?1024u:0u);
        auto run=[&](float strength,FaceCase face,LightCase light=LightCase{}){
            return Run(gpu,w,h,0,1,actualFlags|(light.broad!=1?262144u:0u),variant,before,mw,mh,{},0,{},strength,{1,1,1,1},true,1,light,face);
        };
        const auto baseline=run(1,{}),boost=run(4,{}),protectedFace=run(4,{3,0,0}),original=run(4,{1,1,1});
        const auto zeroMask=run(4,{3,1,1,0}),invalid=run(4,{3,1,1,1,false});
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const auto at=(y+1)*ow+x+2;
            const auto& p=protectedFace.output[at];const auto& one=baseline.output[at];const auto& old=boost.output[at];
            const auto& src=original.original[(y+2)*(w+5)+x+3];
            if(x<w/2) {
                Require(MaxRgbDifference(p,one)<.00003f,"FACE_C1_ANCHOR");
                Require(std::memcmp(&original.output[at],&src,sizeof(Float4))==0,"FACE_ORIGINAL_EXACT_ENDPOINT");
            } else {
                Require(std::memcmp(&p,&old,sizeof(Float4))==0,"FACE_OUTSIDE_EXACT");
                Require(std::memcmp(&original.output[at],&old,sizeof(Float4))==0,"FACE_ORIGINAL_OUTSIDE_EXACT");
            }
            Require(std::memcmp(&zeroMask.output[at],&old,sizeof(Float4))==0,"FACE_ZERO_MASK_EXACT");
            Require(std::memcmp(&invalid.output[at],&old,sizeof(Float4))==0,"FACE_INVALID_REGION_EXACT");
        }
        ++scenarios;
    }
    // Luminance and colour attenuation remain independent in an encoded-value
    // contract where the exact luminance/chromaticity can be measured directly.
    for(bool before:{false,true}) {
        auto run=[&](FaceCase face,LightCase light=LightCase{}){
            return Run(gpu,16,12,0,1,1u|(light.broad!=1?262144u:0u),40,before,16,12,{},0,{},4,{1,1,1,1},false,1,light,face);
        };
        auto lum=run({1,1,0}),col=run({1,0,1});
        for(unsigned y=0;y<12;++y)for(unsigned x=0;x<8;++x) {
            const auto p=lum.output[(y+1)*20+x+2],q=col.output[(y+1)*20+x+2];
            Require(std::abs(.2126f*p.x+.7152f*p.y+.0722f*p.z-.2f)<.00003f,"FACE_LUMA_ONLY_ENDPOINT");
            Require(std::abs(q.x-q.y)<.00003f&&std::abs(q.y-q.z)<.00003f,"FACE_COLOR_ONLY_ENDPOINT");
        }
        const auto cap=run({3,0,0},{4,4,4,4,0});
        const auto anchor=Run(gpu,16,12,0,1,1,40,before,16,12,{},0,{},1,{1,1,1,1},false,1,{});
        for(unsigned y=0;y<12;++y)for(unsigned x=0;x<8;++x)
            Require(MaxRgbDifference(cap.output[(y+1)*20+x+2],anchor.output[(y+1)*20+x+2])<.00003f,"FACE_FINAL_REGION_C1_ANCHOR");
        ++scenarios;
    }
    std::printf("Face composition actual WARP: %u pre/post x colour-domain x 100/75/50-grid scenarios PASS; C1, original endpoints, outside/zero/invalid exact identity and independent luma/colour. Synthetic regions, not face recognition or game performance.\n",scenarios);
}

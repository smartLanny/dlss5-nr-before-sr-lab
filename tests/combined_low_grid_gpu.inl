// Small integration matrix. The input slice ends at Proxy and is never called
// a Neural result; the edit slices use the existing synthetic Neural fixture.
bool HalfPlaneFinite(const std::vector<std::uint8_t> &bytes) {
    for (size_t i=0;i+1<bytes.size();i+=2) {
        const unsigned value=unsigned(bytes[i])|(unsigned(bytes[i+1])<<8);
        if ((value&0x7c00u)==0x7c00u) return false;
    }
    return true;
}

void ValidateCombinedLowGrid(Gpu &gpu) {
    Gpu baseline(kMaintenanceShader);
    unsigned groups=0;
    for(bool reduced:{false,true}) for(int variant:{0,3,13,14}) {
        const UINT nw=reduced?7u:13u,nh=reduced?5u:13u;
        const auto old=Run(baseline,13,13,0,1,1u,variant,true,nw,nh,
            {},0.f,{},1.f,{1,1,1,1},true,0.f);
        const auto now=Run(gpu,13,13,0,1,1u,variant,true,nw,nh,
            {},0.f,{},1.f,{1,1,1,1},true,0.f);
        Require(old.output.size()==now.output.size() &&
            std::memcmp(old.output.data(),now.output.data(),
                old.output.size()*sizeof(Float4))==0,
            "all-low-grid-off differs from frozen dev6");
        ++groups;
    }

    const auto luma=Run(gpu,20,13,0,1,1u|8192u,20,true,10,6,
        {},0.f,{},1.f,{1,1,1,1},true,0.f);
    Require(luma.edit_field.size()==10u*6u*2u,"RG luma field routing");
    ++groups;
    const auto catmull=Run(gpu,20,13,0,1,1u|8192u|16384u,20,true,10,6,
        {},0.f,{},1.f,{1,1,1,1},true,0.f);
    Require(catmull.edit_field.size()==10u*6u*2u &&
        !catmull.horizontal_half.empty(),"RG luma + Catmull routing");
    ++groups;
    const auto colour=Run(gpu,13,13,0,1,1u|8192u|32768u,14,true,7,5,
        {},0.f,{},1.f,{1,1,1,1},true,1.f);
    Require(colour.edit_field.size()==7u*5u*4u,"RGBA colour field routing");
    ++groups;

    InputFilterPipelines input_pipelines(gpu);
    const auto finite=RunInputFilter(gpu,input_pipelines,17,13,1u,5,1);
    Require(HalfPlaneFinite(finite.proxy)&&HalfPlaneFinite(finite.intermediate),
        "finite Lanczos input prefix");
    constexpr UINT sw=36,rw=31,rh=23,sx=3,sy=2;
    for(UINT y=0;y<27;++y) for(UINT x=0;x<sw;++x) {
        const bool active=x>=sx&&x<sx+rw&&y>=sy&&y<sy+rh;
        if(!active) {
            const Float4 &v=finite.source[y*sw+x];
            Require(v.x==4096.f&&v.y==2048.f&&v.z==1024.f&&v.w==.125f,
                "Lanczos touched source prefix/padding");
        }
    }
    ++groups;
    const auto unknown_invalid=RunInputFilter(gpu,input_pipelines,17,13,0u,6,1);
    Require(HalfPlaneFinite(unknown_invalid.proxy),
        "unknown-domain invalid input was not protected");
    ++groups;
    std::printf("Combined low-grid contracts: %u groups; dev6-off, luma, Catmull, colour and input-prefix/unknown protection. Input stops at Proxy; no real NR result.\n",groups);
}

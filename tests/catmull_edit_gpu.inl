#include <filesystem>
#include <fstream>
void ExportCatmullContract(Gpu &gpu,const char *directory) {
    const std::filesystem::path root(directory);
    Require(!std::filesystem::exists(root),"Never overwrite Catmull evidence");
    std::filesystem::create_directories(root);
    std::ofstream metadata(root/"metadata.json");
    metadata<<"{\"kind\":\"catmull-edit-fused-reference\",\"R\":[20,13],\"cases\":[";
    unsigned count=0;size_t bytes=0;
    auto save=[&](const std::string& file,const void *data,size_t size) {
        bytes+=size;Require(bytes<8*1024*1024,"Bounded Catmull capture bytes");
        std::ofstream stream(root/file,std::ios::binary);stream.write(static_cast<const char*>(data),size);
        Require(bool(stream),"Write Catmull raw evidence");
    };
    for(const auto n:{std::pair<UINT,UINT>{20,13},{10,6},{13,8},{15,9},{17,11},{7,5}})
    for(int variant:{1,3,13,20,21,22,23,24})
    for(float strength:{0.f,1.f,2.f,4.f,8.f}) {
        if(n.first!=7 && strength!=1.f) continue;
        const UINT sourceFlags=variant==13?16u:1u;
        auto run=[&](UINT flags){return Run(gpu,20,13,0,1,sourceFlags|8192u|flags,variant,true,
            n.first,n.second,{},0.f,{},strength,{1,1,1,1},true,0.f);};
        const auto bilinear=run(0),fused=run(16384u),reference=run(16384u|131072u);
        float maximum=0.f;
        for(size_t i=0;i<fused.output.size();++i) {
            const float *f=&fused.output[i].x,*r=&reference.output[i].x;
            for(int c=0;c<4;++c) {
                const float difference=std::abs(f[c]-r[c]);maximum=std::max(maximum,difference);
                Require(difference<=3e-5f+3e-4f*std::max(std::abs(f[c]),std::abs(r[c])),"Fused/nonfused declared precision");
            }
            if((n.first==20 && n.second==13)||strength==0.f||variant==1)
                Require(std::memcmp(&fused.output[i],&bilinear.output[i],sizeof(Float4))==0,"Same-grid/zero/identity BilinearSafe equality");
        }
        Require(fused.horizontal_half==reference.horizontal_half,"Fused and reference same FP16 intermediate");
        const auto prefix=std::to_string(count);
        for(const auto &item:{std::pair<const char*,const std::vector<Float4>*>{"bilinear",&bilinear.output},
                {"fused",&fused.output},{"reference",&reference.output},{"original",&fused.original}})
            save(prefix+"-"+item.first+".rgba32f",item.second->data(),item.second->size()*sizeof(Float4));
        save(prefix+"-edit.rg32f",fused.edit_field.data(),fused.edit_field.size()*sizeof(float));
        save(prefix+"-horizontal.rg16f",fused.horizontal_half.data(),fused.horizontal_half.size()*sizeof(std::uint16_t));
        if(count)metadata<<',';
        metadata<<"{\"id\":"<<count<<",\"N\":["<<n.first<<','<<n.second<<"],\"fixture\":"<<variant
            <<",\"strength\":"<<strength<<",\"max_fused_reference_error\":"<<maximum<<'}';
        ++count;
    }
    metadata<<"],\"count\":"<<count<<",\"bytes\":"<<bytes<<"}\n";
    Require(bool(metadata),"Write Catmull metadata");
    std::printf("Catmull production fused/nonfused: %u cases, %zu raw bytes; no timing claim.\n",count,bytes);
}

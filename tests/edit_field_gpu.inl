// Actual BuildEditFieldCS -> UAV barrier -> SRV -> CombineV2CS; no model call.
#include <filesystem>
#include <fstream>
#include <iomanip>

void ExportEditFieldContract(Gpu &gpu,const char *directory) {
    const std::filesystem::path root(directory);
    Require(!std::filesystem::exists(root),"EditField output already exists");
    Require(std::filesystem::create_directories(root),"Create EditField output");
    std::ofstream metadata(root/"edit-field-contract.json",std::ios::binary);
    metadata<<std::setprecision(9);
    metadata<<R"({"schema":"nr-edit-field-fixture-v1","synthetic":true,"nvidia_inference":false,"game_capture":false,
"field_format":"R32G32_FLOAT","field_units":"log2 luminance ratio, validity 0 or 1","epsilon":0.000244140625,
"source_texture":[18,17],"output_texture":[17,16],"source_rect":[3,2,13,13],"output_rect":[2,1,13,13],
"color_strength":0,"lighting_lock":0,"edge_guard":0,"detail_stability":0,"protection":false,
"input_kernel":"existing fixed synthetic Proxy/Neural","restore_kernel":"Bilinear","NR_calls":0,
"exposure_texture":1,"pre_exposure":1,"source_to_canonical":1,"proxy_scale":1,
"frame_identity":"same synthetic inputs per A/B pair","byte_budget":4194304,"cases":[)";
    std::uint64_t bytes=0;unsigned index=0;
    const auto save=[&](const std::string&name,const void*data,std::size_t size){
        bytes+=size;Require(bytes<=4u*1024u*1024u,"EditField raw budget");
        std::ofstream out(root/name,std::ios::binary);out.write(static_cast<const char*>(data),size);
        Require(bool(out),"Write EditField raw plane");
    };
    const auto equal=[](const std::vector<Float4>&a,const std::vector<Float4>&b){
        return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(Float4))==0;};
    for(bool reduced:{false,true}) for(int variant:{0,1,3,6,12,13,15,16})
    for(float strength:{0.f,1.f,2.f,4.f}) {
        const UINT nw=reduced?7:13,nh=reduced?5:13,flags=variant==13?0u:1u;
        const auto a=Run(gpu,13,13,0,1,flags,variant,true,nw,nh,{},0.f,{},strength,{1,1,1,1},true,0.f);
        const auto b=Run(gpu,13,13,0,1,flags|8192u,variant,true,nw,nh,{},0.f,{},strength,{1,1,1,1},true,0.f);
        Require(equal(a.original,b.original)&&equal(a.proxy,b.proxy)&&equal(a.neural,b.neural),"EditField A/B identical raw input");
        double difference=0;
        for(UINT y=0;y<13;++y)for(UINT x=0;x<13;++x){
            const auto av=a.output[(y+1)*17+x+2],bv=b.output[(y+1)*17+x+2],o=a.original[(y+2)*18+x+3];
            difference+=(std::abs(av.x-bv.x)+std::abs(av.y-bv.y)+std::abs(av.z-bv.z))/3;
            if(strength==0.f)Require(std::memcmp(&bv,&o,sizeof(Float4))==0,"EditField zero strength identity");
        }
        const std::string prefix="case-"+std::to_string(index);
        for(const auto &plane:{"original","proxy","neural","a","b"}) {
            const auto &data=std::strcmp(plane,"original")==0?a.original:std::strcmp(plane,"proxy")==0?a.proxy:
                std::strcmp(plane,"neural")==0?a.neural:std::strcmp(plane,"a")==0?a.output:b.output;
            save(prefix+"-"+plane+".rgba32f",data.data(),data.size()*sizeof(Float4));
        }
        save(prefix+"-edit.rg32f",b.edit_field.data(),b.edit_field.size()*sizeof(float));
        if(index)metadata<<',';
        metadata<<"{\"id\":"<<index<<",\"fixture\":"<<variant<<",\"N\":["<<nw<<','<<nh
            <<"],\"codec_flags\":"<<flags<<",\"strength\":"<<strength<<",\"a_b_rgb_mae\":"<<difference/169<<'}';
        ++index;
    }
    metadata<<"],\"cases_total\":"<<index<<",\"plane_bytes\":"<<bytes<<"}\n";
    Require(bool(metadata),"Write EditField metadata");
    std::printf("Production EditField A/B: %u pairs, %llu raw bytes; actual N field and queue completion.\n",index,static_cast<unsigned long long>(bytes));
}

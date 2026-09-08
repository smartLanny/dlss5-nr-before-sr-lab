// Explicit, bounded test-only readback of the actual N-sized textures. No F7
// reconstruction is relabelled as a model plane, and no NVIDIA model is run.
#include <filesystem>
#include <fstream>
#include <iomanip>

void ExportCompositionContract(Gpu &gpu, const char *directory) {
    const std::filesystem::path root(directory);
    Require(!std::filesystem::exists(root), "Comparison output already exists");
    Require(std::filesystem::create_directories(root), "Create comparison output");
    std::ofstream metadata(root / "contract.json", std::ios::binary);
    Require(bool(metadata), "Create comparison metadata");
    metadata << std::setprecision(9);
    metadata << R"({"schema":"nr-composition-fixture-v2","synthetic":true,"nvidia_inference":false,
"game_capture":false,"final_sr_pair":false,"new_algorithm":true,
"candidate":"independent-chroma-v4","candidate_flag":4096,
"storage_format":"R32G32B32A32_FLOAT","typed_view":"R32G32B32A32_FLOAT",
"transfer_function":"linear","color_primaries":"BT.709","units":"fixture-relative",
"paper_white_nits":null,"paper_white_status":"not-defined-by-fixture",
"exposure_texture":1,"pre_exposure":1,"source_to_canonical":1,"proxy_scale":1,
"color_strength":0.25,"max_luma_ratio":2,"lighting_lock":0,"detail_stability":0,
"high_strength_protection":false,"layered_enhancement":false,
"R":[13,13],"O":[13,13],"source_texture":[18,17],"output_texture":[17,16],
"source_rect":[3,2,13,13],"output_rect":[2,1,13,13],"config_generation":1,
"model_identity":"deterministic synthetic fixture, not Feature18",
"readback":"copy after dispatch, map only after real queue fence",
"byte_budget":8388608,"cases":[)";
    std::uint64_t bytes = 0;
    unsigned index = 0, different = 0;
    const auto save = [&](const std::string &name, const std::vector<Float4> &data) {
        bytes += data.size() * sizeof(Float4);
        Require(bytes <= 8u * 1024u * 1024u, "Comparison readback budget");
        std::ofstream stream(root / name, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(data.data()), data.size() * sizeof(Float4));
        Require(bool(stream), "Write raw float plane");
    };
    const auto equal = [](const std::vector<Float4> &a, const std::vector<Float4> &b) {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()*sizeof(Float4)) == 0;
    };
    for (bool reduced : {false, true})
    for (int variant : {0,1,3,6,7,12,13,14,15})
    for (float strength : {0.f,1.f,1.2f,2.f}) {
        const UINT nw = reduced ? 7 : 13, nh = reduced ? 5 : 13;
        // HDR fixture has an explicit unclamped linear contract; storage alone
        // never selects HDR. All other fixtures explicitly choose SDR bounds.
        const UINT flags = variant == 13 ? 0u : 1u;
        const auto pre = Run(gpu,13,13,0,1,flags,variant,true,nw,nh,{},0.f,{},strength,{1,1,1,1},true);
        const auto post = Run(gpu,13,13,0,1,flags,variant,false,nw,nh,{},0.f,{},strength,{1,1,1,1},true);
        const auto candidate = Run(gpu,13,13,0,1,flags|4096u,variant,true,nw,nh,{},0.f,{},strength,{1,1,1,1},true);
        Require(equal(pre.original,post.original) && equal(pre.proxy,post.proxy) && equal(pre.neural,post.neural),
                "PRE_POST_INPUT_IDENTITY");
        Require(equal(pre.original,candidate.original) && equal(pre.proxy,candidate.proxy) && equal(pre.neural,candidate.neural),
                "CANDIDATE_INPUT_IDENTITY");
        const auto uncaptured = Run(gpu,13,13,0,1,flags,variant,true,nw,nh,{},0.f,{},strength);
        Require(equal(pre.output,uncaptured.output), "RAW_N_CAPTURE_PERTURBED_OUTPUT");
        const auto candidate_uncaptured = Run(gpu,13,13,0,1,flags|4096u,variant,true,nw,nh,{},0.f,{},strength);
        Require(equal(candidate.output,candidate_uncaptured.output), "CANDIDATE_CAPTURE_PERTURBED_OUTPUT");
        double delta = 0, pre_edit = 0, post_edit = 0, candidate_edit = 0, candidate_delta = 0;
        for (UINT y=0;y<13;++y) for (UINT x=0;x<13;++x) {
            const auto a=pre.output[(y+1)*17+x+2], b=post.output[(y+1)*17+x+2];
            const auto o=pre.original[(y+2)*18+x+3];
            const auto c=candidate.output[(y+1)*17+x+2];
            const auto luma=[](Float4 v){return .2126*v.x+.7152*v.y+.0722*v.z;};
            delta += (std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z))/3;
            pre_edit += std::abs(luma(a)-luma(o)); post_edit += std::abs(luma(b)-luma(o));
            candidate_edit += std::abs(luma(c)-luma(o));
            candidate_delta += (std::abs(c.x-a.x)+std::abs(c.y-a.y)+std::abs(c.z-a.z))/3;
            if (strength == 0.f) Require(std::memcmp(&c,&o,sizeof(Float4))==0,"CANDIDATE_ZERO_IDENTITY");
        }
        if (delta > 1e-6) ++different;
        const std::string prefix = "case-" + std::to_string(index);
        for (const auto &plane : {"original","proxy","neural","pre","post","candidate"}) {
            const auto &data = std::strcmp(plane,"original")==0 ? pre.original :
                std::strcmp(plane,"proxy")==0 ? pre.proxy : std::strcmp(plane,"neural")==0 ? pre.neural :
                std::strcmp(plane,"pre")==0 ? pre.output : std::strcmp(plane,"post")==0 ? post.output : candidate.output;
            save(prefix+"-"+plane+".rgba32f",data);
        }
        if(index) metadata << ',';
        metadata << "{\"id\":" << index << ",\"frame_identity\":\"" << prefix
            << "\",\"fixture\":" << variant << ",\"N\":[" << nw << ',' << nh
            << "],\"codec_flags\":" << flags << ",\"strength\":" << strength
            << ",\"pre_post_rgb_mae\":" << delta/169 << ",\"pre_luma_edit_mae\":" << pre_edit/169
            << ",\"post_luma_edit_mae\":" << post_edit/169
            << ",\"candidate_luma_edit_mae\":" << candidate_edit/169
            << ",\"candidate_pre_rgb_mae\":" << candidate_delta/169 << '}';
        ++index;
    }
    Require(different > 0, "PRE_POST_MATH_DIFFERENCE_NOT_OBSERVED");
    metadata << "],\"cases_total\":" << index << ",\"different_outputs\":" << different
        << ",\"plane_bytes\":" << bytes << "}\n";
    Require(bool(metadata), "Finish comparison metadata");
    std::printf("Production pre/post composition contract: %u cases, %u different outputs, %llu raw-plane bytes; no NVIDIA/game claim.\n",
        index,different,static_cast<unsigned long long>(bytes));
}

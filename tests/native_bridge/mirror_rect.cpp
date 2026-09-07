#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include "src/interop/nr_mirror_rect.h"
#include "src/interop/nr_deployment_names.h"
using namespace nr_mirror_rect;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
struct Params {
    std::map<std::string, std::uint32_t> data{{"DLSS.Feature.Create.Flags", 2}};
    bool Get(const char* key, std::uint32_t& value) const {
        auto i = data.find(key);
        value = 0xffffffffu; // emulate failed Get clobbering an output
        if (i == data.end()) return false;
        value = i->second; return true;
    }
};
struct Texture { unsigned w, h; std::vector<unsigned> pixels; };
struct Box { unsigned left, top, front, right, bottom, back; };
struct Context {
    unsigned whole = 0, region = 0;
    void CopyResource(Texture* d, Texture* s) { ++whole; d->pixels = s->pixels; }
    void CopySubresourceRegion(Texture* d, unsigned ds, unsigned x, unsigned y, unsigned z,
                               Texture* s, unsigned ss, const Box* b) {
        CHECK(ds == 0 && ss == 0 && z == 0 && b->front == 0 && b->back == 1);
        ++region;
        for (unsigned sy = b->top; sy < b->bottom; ++sy)
            for (unsigned sx = b->left; sx < b->right; ++sx)
                d->pixels[(y + sy - b->top) * d->w + x + sx - b->left] = s->pixels[sy*s->w+sx];
    }
};
Plan Common(const Params& p) {
    return Resolve(p, {1920,1080}, {1920,1080}, {1920,1080}, {1920,1080}, {1920,1080}, {1920,1080});
}
int main() {
    Params p;
    CHECK(Common(p));
    p.data["DLSS.Render.Subrect.Dimensions.Width"] = 1280;
    p.data["DLSS.Render.Subrect.Dimensions.Height"] = 720;
    for (const char* key : {"Color", "Depth", "MV"}) {
        p.data[std::string("DLSS.Input.") + key + ".Subrect.Base.X"] = 320;
        p.data[std::string("DLSS.Input.") + key + ".Subrect.Base.Y"] = 180;
    }
    auto plan = Common(p);
    CHECK(plan && plan.active_declared && plan.render.w == 1280 && plan.render.h == 720);
    CHECK(plan.color_base.x == 320 && plan.depth_base.y == 180 && plan.output.w == 1920);
    // Exact odd dimensions, independent depth/MV bases and padded allocations.
    for (unsigned size : {1280u,1600u,1919u,1280u}) {
        Params q; q.data["DLSS.Render.Subrect.Dimensions.Width"] = size;
        q.data["DLSS.Render.Subrect.Dimensions.Height"] = 719;
        auto r = Common(q); CHECK(r && r.render.w == size && r.render.h == 719);
    }
    auto missing = p;
    missing.data.erase("DLSS.Render.Subrect.Dimensions.Height");
    CHECK(Common(missing).reason == Reason::IncompleteActive);
    missing = p; missing.data["DLSS.Render.Subrect.Dimensions.Height"] = 0;
    CHECK(Common(missing).reason == Reason::IncompleteActive);
    missing = p; missing.data.erase("DLSS.Input.Depth.Subrect.Base.Y");
    CHECK(Common(missing).reason == Reason::IncompleteBase);
    missing = p; missing.data.erase("DLSS.Feature.Create.Flags");
    CHECK(Common(missing).reason == Reason::MissingFlags);
    for (const char* key : {"Color", "Depth", "MV"}) {
        auto q = p; q.data[std::string("DLSS.Input.") + key + ".Subrect.Base.X"] = 0xffffffffu;
        CHECK(!Common(q));
    }
    auto high_mv = p; high_mv.data["DLSS.Feature.Create.Flags"] = 0;
    CHECK(Common(high_mv).reason == Reason::MotionBounds);
    high_mv.data["DLSS.Input.MV.Subrect.Base.X"] = high_mv.data["DLSS.Input.MV.Subrect.Base.Y"] = 0;
    CHECK(Common(high_mv));
    Params zero; zero.data["DLSS.Render.Subrect.Dimensions.Width"] = 0;
    zero.data["DLSS.Render.Subrect.Dimensions.Height"] = 0;
    CHECK(Common(zero) && !Common(zero).active_declared);
    CHECK(!Resolve(p, {1920,1080}, {100,100}, {1920,1080}, {1920,1080}, {1920,1080}, {1920,1080}));
    // Exhaustive small output rectangles. Execute the production copy helper,
    // including a negative control proving the old 0,0 copy damages the border.
    for (unsigned ox=0; ox<5; ++ox) for (unsigned oy=0; oy<4; ++oy) {
        Params q; q.data["DLSS.Output.Subrect.Base.X"] = ox;
        q.data["DLSS.Output.Subrect.Base.Y"] = oy;
        auto r = Resolve(q, {3,2}, {4,3}, {3,2}, {3,2}, {3,2}, {8,6});
        CHECK(r && r.partial_output);
        Texture src{8,6,std::vector<unsigned>(48)}, dst{8,6,std::vector<unsigned>(48,999)};
        for (unsigned i=0;i<48;++i) src.pixels[i]=i+1;
        Context ctx;
        CopyOutput<Context,Texture,Box>(&ctx,&dst,&src,r);
        CHECK(ctx.whole == 0 && ctx.region == 1);
        for (unsigned y=0;y<6;++y) for (unsigned x=0;x<8;++x)
            CHECK(dst.pixels[y*8+x] == ((x>=ox && x<ox+4 && y>=oy && y<oy+3) ? src.pixels[y*8+x] : 999));
        if (ox || oy) {
            auto wrong = r; wrong.output_base = {};
            Texture negative{8,6,std::vector<unsigned>(48,999)};
            CopyOutput<Context,Texture,Box>(&ctx,&negative,&src,wrong);
            CHECK(negative.pixels != dst.pixels);
        }
    }
    CHECK(!Fits({10,10},{0xffffffffu,0},{100,100}));
    CHECK(!Fits({0xffffffffu,10},{1,0},{0xffffffffu,100}));
    CHECK(Fits({0xffffffffu,10},{0,0},{0xffffffffu,100}));
    using namespace nr_deployment_names;
    for (auto name : {L"renodx-dlss5.addon64",L"RENODX-DLSS5-V_4_5_5.ADDON64",L"renodx-dlss5_v4.70.addon64"}) CHECK(GenericNr(name));
    for (auto name : {L"Luma.addon64",L"renodx-streetfighter6.addon64",L"renodx-dlss.addon64",L"renodx-dlss5-hdr.addon64",L"renodx-dlss5-v.addon64",L"renodx-dlss5.addon64.bak",L"renodx-dlss5-v4.dll",L""}) CHECK(!GenericNr(name));
    CHECK(Carrier(L"dlss5-native-carrier-045-exp3.addon64"));
#ifdef NR_TEST_MULTIAPI
    CHECK(MatchingCarrier(L"dlss5-native-carrier-multiapi-dev3.addon64"));
    CHECK(!MatchingCarrier(L"dlss5-native-carrier-multiapi-dev2.addon64"));
    CHECK(!MatchingCarrier(L"dlss5-native-carrier-multiapi-dev1.addon64"));
    CHECK(!MatchingCarrier(L"dlss5-native-carrier-045-exp3.addon64"));
#else
    CHECK(!MatchingCarrier(L"dlss5-native-carrier-multiapi-dev2.addon64"));
    CHECK(MatchingCarrier(L"dlss5-native-carrier-045-exp3.addon64"));
    CHECK(!MatchingCarrier(L"dlss5-native-carrier-multiapi-dev1.addon64"));
#endif
    CHECK(!MatchingCarrier(L"dlss5-native-carrier-r3-exp2.addon64"));
    CHECK(!Carrier(L"nvngx_dlss.dll"));
    std::cout << checks << " production rectangle/copy/deployment checks passed; no GPU/game claim\n";
}

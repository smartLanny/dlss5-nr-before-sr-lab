#include <cassert>
#include <map>
#include <string>
#include <iostream>
#include "src/interop/nr_render_scale_policy.h"
using namespace nr_render_scale;
struct Fake {
    std::map<std::string,std::uint32_t> values;
    unsigned writes=0;
    bool Get(const char* key,std::uint32_t& out) { auto i=values.find(key); if(i==values.end()) return false;out=i->second;return true; }
    void Set(const char* key,std::uint32_t v) { ++writes; values[key]=v; }
};
Fake Params() { return {{{"Width",3840},{"Height",2160},{"OutWidth",2560},{"OutHeight",1440},
    {"DLSS.Get.Dynamic.Min.Render.Width",1280},{"DLSS.Get.Dynamic.Min.Render.Height",720},
    {"DLSS.Get.Dynamic.Max.Render.Width",3840},{"DLSS.Get.Dynamic.Max.Render.Height",2160},
    {"PerfQualityValue",2},{"Reset",0}},0}; }
int main() {
    auto p=Params(); auto a=ApplyOptimal(p,50,true);
    assert(a.decision==Decision::Accepted && p.values["OutWidth"]==1920 && p.values["OutHeight"]==1080 && p.writes==2);
    assert(p.values["Width"]==3840 && p.values["PerfQualityValue"]==2 && p.values["Reset"]==0);
    auto original=Params(); p=original;
    assert(ApplyOptimal(p,50,false).decision==Decision::Disabled && p.values==original.values && !p.writes);
    assert(ApplyOptimal(p,0,true).decision==Decision::Disabled && !p.writes);
    for(unsigned percent : {1u,49u,101u,0xffffffffu}) { p=Params(); assert(ApplyOptimal(p,percent,true).decision==Decision::Invalid && !p.writes); }
    for(const char* missing : {"Width","Height","DLSS.Get.Dynamic.Min.Render.Width","DLSS.Get.Dynamic.Min.Render.Height","DLSS.Get.Dynamic.Max.Render.Width","DLSS.Get.Dynamic.Max.Render.Height"}) {
        p=Params();p.values.erase(missing);auto before=p.values;
        assert(ApplyOptimal(p,75,true).decision==Decision::Invalid && p.values==before && !p.writes);
    }
    p=Params(); p.values["DLSS.Get.Dynamic.Min.Render.Width"]=3000;
    assert(ApplyOptimal(p,50,true).decision==Decision::UnsupportedRange && !p.writes);
    for(unsigned w : {64u,1280u,1920u,2560u,3440u,3839u,3840u,7680u,32768u}) {
        for(unsigned percent=50;percent<=100;++percent) {
            auto r=Negotiate(percent,{w,w},{{64,64},{32768,32768}});
            if(r.decision==Decision::Accepted) { assert(r.render.w==r.render.h); assert(!(r.render.w&1)); assert(r.render.w<=w+1); }
        }
    }
    assert(Negotiate(75,{0xffffffffu,2160},{{64,64},{32768,32768}}).decision==Decision::Invalid);
    std::cout << "Production render-scale negotiation: success, missing bounds, unsupported range, disabled and overflow cases passed\n";
}

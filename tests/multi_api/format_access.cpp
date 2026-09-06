#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>
#include "src/interop/nr_format_access.h"
using namespace nr_format_access;
static unsigned checks=0;
#define CHECK(x) do {++checks; if(!(x)) {std::cerr<<"FAIL "<<__LINE__<<": " #x "\n"; std::exit(1);}} while(0)
struct Probe {
    std::map<Format,bool> load,store;
    std::vector<std::pair<Format,Usage>> calls;
    bool operator()(Format f,Usage u) {calls.push_back({f,u});return u==Usage::ShaderLoad?load[f]:store[f];}
};
int main() {
    Probe probe;
    for(auto f:{Rgba32Float,Rgba16Float,R11g11b10Float,Rgba16Unorm,Rgba8Unorm,Rgba8Srgb,Bgra8Unorm})
        probe.load[f]=true;
    probe.store[Rgba8Unorm]=probe.store[Rgba32Float]=true;
    for(auto f:{Rgba32Typeless,Rgba16Typeless,Rgba32Float,Rgba16Float,R11g11b10Float}) {
        Request r; r.resource=f; r.permit_float_interpretation=true;
        auto p=Resolve(r,probe);
        CHECK(p); CHECK(p.srv==(f==Rgba32Typeless?Rgba32Float:f==Rgba16Typeless?Rgba16Float:f));
        CHECK(p.uav==0 && !p.copyback_required); // pre-SR source needs no UAV
        r.needs_inplace_store=true;
        auto post=Resolve(r,probe);
        CHECK(post.status==Status::WritebackUnavailable);
        r.equivalent_copyback_available=true;
        CHECK(Resolve(r,probe).copyback_required);
    }
    Request r; r.resource=Rgba32Typeless;
    CHECK(Resolve(r,probe).status==Status::AmbiguousType);
    r.permit_float_interpretation=true;
    probe.load[Rgba32Float]=false; probe.load[Rgba32Uint]=true;
    CHECK(Resolve(r,probe).status==Status::UnsupportedRead); // never float -> uint
    CHECK(!Resolve(r,probe));
    probe.load[Rgba32Float]=true;
    CHECK(Resolve(r,probe)); // a failed query did not latch the process off
    r.observed_view=Rgba16Float;
    CHECK(Resolve(r,probe).status==Status::IllegalView);
    r.observed_view=Rgba32Uint;
    CHECK(Resolve(r,probe).status==Status::IntegerShaderRequired);
    r.observed_view=Rgba32Float; r.deny_shader_resource=true;
    CHECK(Resolve(r,probe).status==Status::DeniedRead);
    r={}; r.resource=Rgba8Srgb; r.needs_inplace_store=true; r.allow_unordered_access=true;
    CHECK(Resolve(r,probe).status==Status::WritebackUnavailable); // typed sRGB != UNORM
    r.resource=Rgba8Typeless; r.observed_view=Rgba8Srgb;
    auto p=Resolve(r,probe);
    CHECK(p && p.srv==Rgba8Srgb && p.uav==Rgba8Unorm);
    CHECK(p.provenance==Provenance::ObservedView);
    r={}; r.resource=Rgba16Typeless; r.permit_float_interpretation=true;
    probe.load[Rgba16Float]=false; probe.load[Rgba16Unorm]=true;
    CHECK(Resolve(r,probe).status==Status::UnsupportedRead); // no numerical reinterpretation fallback
    r.observed_view=Rgba16Unorm;
    CHECK(Resolve(r,probe)); // an explicit provider-known interpretation is a different case
    r={};r.resource=0xffffffffu;
    CHECK(Resolve(r,probe).status==Status::UnknownLayout);
    for(unsigned res=0;res<=110;++res) for(unsigned view=0;view<=110;++view) {
        const bool legal=LegalView(res,view);
        if(IsTypeless(res)) {
            bool member=false;auto family=FamilyViews(res);
            for(unsigned i=0;i<family.count;++i) member|=family.values[i]==view;
            CHECK(legal==member);
        } else CHECK(legal==(view!=0 && !IsTypeless(view) && res==view));
    }
    // A deliberately wrong old policy accepts TYPELESS as an SRV just because
    // the allocation reports texture support. The oracle must distinguish it.
    Request old;old.resource=Rgba32Typeless;old.permit_float_interpretation=true;
    auto actual=Resolve(old,probe);
    CHECK(actual.srv!=old.resource);
    CHECK(LegalView(old.resource,actual.srv));
    CHECK(!LegalView(old.resource,old.resource));
    std::cout<<checks<<" production format-family/capability assertions passed (host CPU; no Windows/GPU claim)\n";
}

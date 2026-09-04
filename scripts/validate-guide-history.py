"""Exercise production guide/context lookup and work-raster conversion on CPU.

This is a CPU regression seam, not a replacement for in-game temporal tests.
The function and tuning declarations are extracted verbatim from the add-on;
only device/lifetime services are stubbed (retirement remains fence-pending).
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def definition(source, marker, trailing_semicolon=False):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(trailing_semicolon)]


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--source", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (args.source or root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
has_guides = "const NrGuideSignature &requested_guides" in source
guide_include = '#include "nr_guide_contract.h"' if has_guides else ""
guide_fields = "NrGuideSignature guide_storage_signature{}; NrGuideHistory guide_history{};" if has_guides else ""
guide_arg = ", guides" if has_guides else ""
jitter_types = definition(source, "struct NrJitterBinding", True) if has_guides else ""
jitter_resolver = definition(source, "NrJitterBinding ResolveNrJitter(") if has_guides else ""
harness = r'''
#include <algorithm>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cmath>
#include <climits>
#include <cstring>
GUIDE_INCLUDE
struct NgxHandle { unsigned id; };
enum class CodecColorMode : unsigned { Automatic, LinearSdr, ModelEncodedValueDomain };
enum class NrJitterMode : int { Legacy=0, GameInputPixel=1 };
struct Config { float intensity=1, local_tone=1, local_structure=1, skin_structure=-1; bool auto_mask=false; unsigned style=1; bool ui_correction=false; int depth_mode=-1; NrJitterMode jitter_mode=NrJitterMode::GameInputPixel; } g_config;
TUNING
CURRENT_TUNING
SAME_TUNING
struct NrContext {
    const NgxHandle* sr_handle=nullptr;
    unsigned long long feature_generation=0, instance_id=0;
    unsigned long long stage_capture_request_id=0;
    unsigned width=0, height=0;
    int mode=0;
    bool retired=false, codec_color_mode_initialized=false, tuning_signature_initialized=false, codec_peak_shoulder=false;
    CodecColorMode codec_color_mode=CodecColorMode::Automatic;
    NrTuningSignature tuning_signature{};
    GUIDE_FIELDS
};
NrContext g_contexts[16]{};
std::atomic<bool> g_lifecycle_tracking_uncertain{false}, g_nr_failed_create_process_latched{false};
std::atomic<unsigned long long> g_nr_instance_serial{0}, g_next_nr_instance_id{0};
void CollectRetiredContextsLocked() {} // No fence has completed in this trace.
void Log(const char*, ...) {}
// Stage capture stays off in the guide/context regression. Its request and
// retirement behavior is exercised by validate-stage-capture-lifecycle.ps1.
void PrepareStageCaptureContextLocked(const NgxHandle*, unsigned long long, int) {}
unsigned long long PreparedStageCaptureRequestId(const NgxHandle*, unsigned long long, int) { return 0; }
JITTER_TYPES
GUIDE_RESOLVER
LOOKUP
int failures=0;
void check(bool value, const char* name) { if (!value) { ++failures; std::printf("FAIL: %s\n", name); } }
void clear() { for (auto& c:g_contexts) c={}; }
int main() {
    NgxHandle carrier{1};
    GUIDE_DECL
    auto lookup=[&](int mode,unsigned w,unsigned h) { return FindOrCreateContext(&carrier, 1, mode,w,h,CodecColorMode::Automatic GUIDE_ARG); };
    auto* first_a=lookup(2,1920,1080);
    auto* same_a=lookup(2,1920,1080);
    check(first_a==same_a,"stable dimensions reuse current history");
    auto* b=lookup(2,1280,720);
    check(first_a->retired,"A must retire when B supersedes its dimensions");
    check(b && b!=first_a,"B gets separate pending-fence context");
    auto* second_a=lookup(2,1920,1080);
    check(second_a && second_a!=first_a,"A->B->A must not resurrect first A history");
    check(b->retired,"B retires on return to A");
    auto* post=lookup(1,3840,2160);
    check(post && !second_a->retired,"independent mode does not retire other mode");
    clear();
    auto* legacy=lookup(2,1920,1080);
    auto* peak=FindOrCreateContext(&carrier,1,2,1920,1080,CodecColorMode::Automatic GUIDE_ARG,true);
    check(legacy && peak && legacy!=peak && legacy->retired,"proxy mapping change retires history with pending fence");
    check(peak && peak->codec_peak_shoulder,"new context records effective proxy mapping");
    auto* stable_peak=FindOrCreateContext(&carrier,1,2,1920,1080,CodecColorMode::Automatic GUIDE_ARG,true);
    check(stable_peak==peak,"same proxy mapping reuses history");
    auto* restored=lookup(2,1920,1080);
    check(restored && restored!=legacy && restored!=peak && peak->retired,"legacy->peak->legacy never resurrects history");
    check(restored && !restored->codec_peak_shoulder,"return to default records legacy mapping");
    clear();
    for(unsigned i=0;i<16;++i) check(lookup(2,100+i,100)!=nullptr,"free slot available before capacity");
    check(lookup(2,100,100)==nullptr,"pending-fence capacity cannot resurrect retired size");
    GUIDE_TESTS
    std::printf("guide/context regression: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1:0;
}
'''
replacements = {
    "GUIDE_INCLUDE": guide_include,
    "JITTER_TYPES": jitter_types,
    "GUIDE_FIELDS": guide_fields,
    "GUIDE_DECL": "NrGuideSignature guides{};" if has_guides else "",
    "GUIDE_ARG": guide_arg,
    "TUNING": definition(source, "struct NrTuningSignature", True),
    "CURRENT_TUNING": definition(source, "NrTuningSignature CurrentNrTuningSignature("),
    "SAME_TUNING": definition(source, "bool SameNrTuningSignature("),
    "LOOKUP": definition(source, "NrContext *FindOrCreateContext("),
    "GUIDE_RESOLVER": r'''
struct D3D12_RESOURCE_DESC { unsigned long long Width=1920; unsigned Height=1080, Format=40; };
struct ID3D12Resource { D3D12_RESOURCE_DESC desc{}; };
struct FeatureRecord { int create_flags=2; };
struct NgxParams {
    ID3D12Resource *depth=nullptr, *motion=nullptr, *output=nullptr;
    unsigned depth_x=0,depth_y=0,motion_x=0,motion_y=0,output_width=3840,output_height=2160;
    float scale_x=-2,scale_y=4,jitter_x=0.5f,jitter_y=-0.25f;
    bool has_scale_x=true,has_scale_y=true,has_jitter_x=true,has_jitter_y=true;
    int reset=0;
};
D3D12_RESOURCE_DESC ID3D12Resource_GetDesc(ID3D12Resource* r) { return r->desc; }
bool GetResource(const NgxParams* p,const char* k,ID3D12Resource** out) {
    *out = !std::strcmp(k,"Depth") ? p->depth : !std::strcmp(k,"MotionVectors") ? p->motion : !std::strcmp(k,"Output") ? p->output : nullptr;
    return *out!=nullptr;
}
bool GetUInt(const NgxParams* p,const char* k,unsigned* out) {
    if(!std::strcmp(k,"DLSS.Input.Depth.Subrect.Base.X")) *out=p->depth_x;
    else if(!std::strcmp(k,"DLSS.Input.Depth.Subrect.Base.Y")) *out=p->depth_y;
    else if(!std::strcmp(k,"DLSS.Input.MV.Subrect.Base.X")) *out=p->motion_x;
    else if(!std::strcmp(k,"DLSS.Input.MV.Subrect.Base.Y")) *out=p->motion_y;
    else return false;
    return true;
}
bool GetFloat(const NgxParams* p,const char* k,float* out) {
    if(!std::strcmp(k,"MV.Scale.X")) {
        if(!p->has_scale_x) return false; *out=p->scale_x; return true;
    }
    if(!std::strcmp(k,"MV.Scale.Y")) {
        if(!p->has_scale_y) return false; *out=p->scale_y; return true;
    }
    if(!std::strcmp(k,"Jitter.Offset.X")) {
        if(!p->has_jitter_x) return false; *out=p->jitter_x; return true;
    }
    if(!std::strcmp(k,"Jitter.Offset.Y")) {
        if(!p->has_jitter_y) return false; *out=p->jitter_y; return true;
    }
    return false;
}
bool GetInt(const NgxParams* p,const char*,int* out) { *out=p->reset; return true; }
bool TextureContainsRegion(ID3D12Resource* r,unsigned x,unsigned y,unsigned w,unsigned h) {
    return r && w && h && x<=r->desc.Width && y<=r->desc.Height && w<=r->desc.Width-x && h<=r->desc.Height-y;
}
bool ResolveOutputExtentFromEvaluate(const NgxParams* p,const FeatureRecord&, ID3D12Resource*,unsigned,unsigned,unsigned,unsigned,unsigned& w,unsigned& h) {
    w=p->output_width; h=p->output_height; return w && h;
}
''' + (root / 'src/nr_f8_types.h').read_text(encoding='utf-8') + '\n' + definition(source, "NrJitterBinding ResolveNrJitter(") + '\n' + definition(source, "struct NrGuideBinding", True) + "\n" + definition(source, "bool ResolveNrGuideBinding(") if has_guides else "",
    "GUIDE_TESTS": r'''
    clear();
    guides.depth_format=40; guides.depth_physical_width=1920; guides.depth_physical_height=1080;
    guides.motion_format=34; guides.motion_physical_width=1920; guides.motion_physical_height=1080;
    guides.color_format=10; guides.color_physical_width=1920; guides.color_physical_height=1080;
    guides.depth_width=guides.motion_source_width=guides.motion_bound_width=1920;
    guides.depth_height=guides.motion_source_height=guides.motion_bound_height=1080;
    auto* original=lookup(1,3840,2160);
    check(original->guide_history.NeedsReset(guides),"first evaluation resets");
    original->guide_history.CommitOnEvaluate(guides,true);
    check(!original->guide_history.NeedsReset(guides),"successful unchanged guide preserves history");
    guides.motion_scale_x=-2.0f;
    check(lookup(1,3840,2160)==original,"MV unit change resets without reallocating storage");
    check(original->guide_history.NeedsReset(guides),"MV unit change requires Reset");
    original->guide_history.CommitOnEvaluate(guides,false);
    check(original->guide_history.NeedsReset(guides),"failed evaluation must not consume guide Reset");
    original->guide_history.CommitOnEvaluate(guides,true);
    check(!original->guide_history.NeedsReset(guides),"successful reset commits mapping");
    guides.depth_x=16; guides.motion_x=8; guides.color_x=4; guides.depth_inverted=1;
    check(lookup(1,3840,2160)==original,"subrect and inversion changes preserve allocation");
    check(original->guide_history.NeedsReset(guides),"subrect and inversion changes reset history");
    guides.normalized_create_flags ^= 2;
    auto* changed_flags=lookup(1,3840,2160);
    check(original->retired && changed_flags!=original,"MVLowRes change rebuilds before latched create flags can hide it");
    guides.motion_format=16;
    auto* changed_format=lookup(1,3840,2160);
    check(changed_flags->retired && changed_format!=changed_flags,"original motion format change rebuilds");
    changed_format->guide_history.CommitOnEvaluate(guides,true);
    guides.depth_width=1280; guides.depth_height=720;
    auto* changed_guide_extent=lookup(1,3840,2160);
    check(changed_guide_extent==changed_format && !changed_format->retired,"DRS depth extent reuses the same Feature 18 context");
    check(changed_guide_extent->guide_history.NeedsReset(guides),"DRS depth extent still resets temporal history");
    changed_guide_extent->guide_history.CommitOnEvaluate(guides,true);
    guides.depth_physical_width=2048; guides.depth_physical_height=1152;
    auto* changed_physical=lookup(1,3840,2160);
    check(changed_physical==changed_guide_extent,"rotating DRS allocation size does not exhaust the context pool");
    check(changed_physical->guide_history.NeedsReset(guides),"physical guide reallocation is a history boundary");
    changed_physical->guide_history.CommitOnEvaluate(guides,true);
    guides.resample_motion=true;
    auto* changed_mapping=lookup(1,3840,2160);
    check(changed_physical->retired && changed_mapping!=changed_physical,"original/proxy motion binding change rebuilds storage");
    ID3D12Resource depth{},motion{},color{},output{};
    motion.desc.Format=34;
    output.desc={3840,2160,10};
    NgxParams params{}; params.depth=&depth; params.motion=&motion; params.output=&output;
    FeatureRecord feature{};
    NrGuideBinding resolved{};
    check(ResolveNrGuideBinding(&params,feature,2,1920,1080,1920,1080,&color,0,0,CodecColorMode::Automatic,resolved),"valid low-res guide entrance resolves");
    check((resolved.signature.normalized_create_flags&2)==0,"equal-sized pre-SR motion clears MVLowRes before lookup");
    check(resolved.signature.motion_scale_x==-2 && resolved.signature.motion_scale_y==4,"reference raster preserves source-pixel motion units");
    check(resolved.jitter_offset_x==0.5f && resolved.jitter_offset_y==-0.25f,"reference raster preserves source-pixel jitter");
    feature.create_flags=0; motion.desc={3840,2160,34};
    resolved={};
    check(ResolveNrGuideBinding(&params,feature,2,1920,1080,1920,1080,&color,0,0,CodecColorMode::Automatic,resolved),"full-resolution motion conversion resolves");
    check(resolved.signature.resample_motion && resolved.signature.motion_source_width==3840 && resolved.signature.motion_bound_width==1920,"motion signature retains original and converted domains");
    check(resolved.signature.motion_scale_x==-2 && resolved.signature.motion_scale_y==4,"1:1 work raster preserves vector scale after spatial resampling");
    NrGuideBinding reduced{};
    check(ResolveNrGuideBinding(&params,feature,2,1440,810,1920,1080,&color,0,0,CodecColorMode::Automatic,reduced),"75-percent working raster resolves");
    check(std::fabs(reduced.signature.motion_scale_x+1.5f)<1e-6f && std::fabs(reduced.signature.motion_scale_y-3.0f)<1e-6f,"motion scale converts render-pixel displacement into work-raster pixels");
    check(std::fabs(reduced.jitter_offset_x-0.375f)<1e-6f && std::fabs(reduced.jitter_offset_y+0.1875f)<1e-6f,"jitter converts into the same Feature 18 source-pixel domain");
    NrGuideSignature unchanged=resolved.signature;
    ID3D12Resource rotated_motion=motion; params.motion=&rotated_motion; resolved={};
    check(ResolveNrGuideBinding(&params,feature,2,1920,1080,1920,1080,&color,0,0,CodecColorMode::Automatic,resolved) && SameNrGuideMappingContract(unchanged,resolved.signature),"rotating equivalent resource pointers do not reset history");
    params.motion=&motion; motion.desc={4,4,34}; resolved={};
    check(!ResolveNrGuideBinding(&params,feature,2,1920,1080,1920,1080,&color,0,0,CodecColorMode::Automatic,resolved),"transient 4x4 motion rejects before lookup/recording");
    motion.desc={3840,2160,34}; resolved={};
    check(ResolveNrGuideBinding(&params,feature,1,3840,2160,1920,1080,&output,0,0,CodecColorMode::Automatic,resolved),"post-SR full-resolution motion resolves");
    check(!resolved.signature.resample_motion && resolved.signature.motion_bound_width==3840 && !(resolved.signature.normalized_create_flags&2),"post-SR full-res MV contract keeps original binding");
    feature.create_flags=2; motion.desc={1920,1080,34}; resolved={};
    check(ResolveNrGuideBinding(&params,feature,1,3840,2160,1920,1080,&output,0,0,CodecColorMode::Automatic,resolved) && (resolved.signature.normalized_create_flags&2),"post-SR low-res MV relationship is computed before creation");
    params.scale_x=0; params.scale_y=NAN; params.has_jitter_x=false; params.has_jitter_y=false; resolved={};
    feature.create_flags=3;
    check(ResolveNrGuideBinding(&params,feature,2,1920,1080,1920,1080,&color,0,0,CodecColorMode::ModelEncodedValueDomain,resolved),"bounded-R8 guide contract resolves");
    check(resolved.signature.normalized_create_flags==0 && resolved.signature.motion_scale_x==1 && resolved.signature.motion_scale_y==1,"live bounded SDR normalizes HDR flag and invalid MV scales safely");
    check(resolved.jitter_offset_x==0 && resolved.jitter_offset_y==0,"missing jitter falls back to zero");
    params.scale_x=8; params.reset=1;
    check(resolved.signature.motion_scale_x==1 && resolved.game_reset==0,"binding remains an immutable entrance snapshot across native SR");
''' if has_guides else "",
}
for key in sorted(replacements, key=len, reverse=True):
    harness = harness.replace(key, replacements[key])
with tempfile.TemporaryDirectory(prefix="dlss5-guide-regression-") as tmp:
    cpp = Path(tmp) / "test.cpp"
    exe = Path(tmp) / "test.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([args.compiler, "-std=c++20", "-O0", "-static", "-I", str(root / "src"), str(cpp), "-o", str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)

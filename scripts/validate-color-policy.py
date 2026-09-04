"""Exercise the production colour policy, dispatch wiring and actual UI block."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def definition(source, marker, semicolon=False):
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(semicolon)]


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
ui_source = (root / "src/nr_settings_ui.inl").read_text(encoding="utf-8-sig")
dispatch = source[source.index("    const NrColorPolicy color_policy = ResolveNrColorPolicy("):]
dispatch = dispatch[:dispatch.index("    if (capture_stages) {")]
ui_function = definition(ui_source, "void DrawColorControls(")
ui_block = ui_function[ui_function.index("{") + 1:ui_function.rindex("}")]

harness = r'''
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
enum DXGI_FORMAT { DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   DXGI_FORMAT_R11G11B10_FLOAT };
struct ID3D12Resource {};
COLOR_CONTRACT
UNITY_EXPOSURE
CODEC_ENUM
struct Config {
    bool enabled=true, identity_use_original=false, preserve_lighting=false;
    bool experimental_peak_shoulder=false;
    int mode=2;
    float transfer_strength=1, color_strength=1;
} g_config;
float ActiveTransferStrength(const Config& c) { return c.transfer_strength; }
enum class BypassReason { None, Guard };
std::atomic<unsigned> g_config_generation{7};
std::atomic<unsigned long long> g_pipeline_stats_generation{12};
std::atomic<int> g_last_bypass_reason{0};
unsigned long long clock_ms=2000;
int g_chain_cs=0, held=0;
void EnterCriticalSection(int*) { ++held; }
void LeaveCriticalSection(int*) { --held; }
unsigned long long GetTickCount64() { return clock_ms; }
#include "nr_color_policy.inl"
PEAK_SHOULDER
struct Slot {
    DXGI_FORMAT result_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    NrColorPolicy diagnostic_color_policy{};
    float diagnostic_color_strength=-1;
    bool diagnostic_preserve_lighting=true;
};
struct { void* combine=reinterpret_cast<void*>(1); } g_codec;
struct { unsigned long long frame_id=99; } g_applied_nr_identity;
unsigned DispatchPolicy(Slot& slot,void* pipeline,const ColorContract& color_contract,
                        bool hdr_input,CodecColorMode color_mode,bool capture_delta) {
    const unsigned source_flags=0;
    DISPATCH
    return codec_flags;
}
constexpr bool kChineseUi=false;
const char* ui(const char* en,const char*) { return en; }
namespace ImGui {
    bool disabled=false, colour_disabled=false, lighting_disabled=false;
    bool try_edits=false;
    std::string text;
    void BeginDisabled(bool value) { disabled=value; }
    void EndDisabled() { disabled=false; }
    bool SliderFloat(const char*,float* value,float,float,const char*) {
        colour_disabled=disabled;
        if(try_edits && !disabled) { *value=.25f; return true; }
        return false;
    }
    bool Checkbox(const char*,bool* value) {
        lighting_disabled=disabled;
        if(try_edits && !disabled) { *value=!*value; return true; }
        return false;
    }
    template<class... A> void TextDisabled(const char* fmt,A... args) {
        char buffer[2048]; std::snprintf(buffer,sizeof(buffer),fmt,args...); text+=buffer;
    }
    template<class... A> void TextWrapped(const char* fmt,A... args) {
        TextDisabled(fmt,args...);
    }
}
void SettingsTooltip(const char*) {}
void Controls(Config& next,bool has_pending,bool& changed) {
    UI_BLOCK
}
unsigned checks=0,failures=0;
void check(bool ok,const char* label) {
    ++checks; if(!ok) { ++failures; if(failures<30) std::printf("FAIL: %s\n",label); }
}
int main() {
    ID3D12Resource exposure{};
    const float values[]={.5f,1,1+1.f/4096,2};
    const float requests[]={0,.25f,.5f,1};
    unsigned cases=0;
    for(int mode:{1,2}) for(bool hdr:{false,true})
    for(unsigned codec=0;codec<5;++codec) for(bool fp16:{false,true})
    for(bool pre_key:{false,true}) for(bool scale_key:{false,true})
    for(float pre:values) for(float scale:values) for(bool tex:{false,true})
    for(float requested:requests) for(bool lighting:{false,true}) {
        ColorContract c{}; c.pre_exposure=pre; c.exposure_scale=scale;
        c.has_pre_exposure=pre_key; c.has_exposure_scale=scale_key;
        c.exposure_texture=tex?&exposure:nullptr;
        const auto cm=static_cast<CodecColorMode>(codec);
        const bool unity=IsUnityExposureContract(c);
        const bool legacy_exception=mode==1 && hdr && codec==0 && fp16 && unity;
        const bool legacy_block=hdr && (codec==0 || codec==3) && !legacy_exception;
        const NrColorPolicy p=ResolveNrColorPolicy(mode,hdr,cm,fp16,unity,requested,lighting);
        check(p.requested_color_strength==requested &&
              p.requested_preserve_lighting==lighting,"requested settings unchanged");
        check(p.effective_color_strength==(legacy_block?0:requested) &&
              p.color_allowed==!legacy_block,"all valid contracts match previous colour decision");
        check(p.effective_preserve_lighting==(mode==2 && lighting),"lighting matches previous shader");
        check((p.color_reason==NrColorReason::UnverifiedHdr)==legacy_block,"restriction reason matches zero gate");
        if((++cases%127)==0) {
            g_config.mode=mode; g_config.color_strength=requested; g_config.preserve_lighting=lighting;
            Slot slot{}; slot.result_format=fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R11G11B10_FLOAT;
            auto flags=DispatchPolicy(slot,g_codec.combine,c,hdr,cm,true);
            check(((flags&2)!=0)==p.effective_preserve_lighting,"real dispatch lighting flag");
            check(slot.diagnostic_color_strength==p.effective_color_strength &&
                  slot.diagnostic_preserve_lighting==p.effective_preserve_lighting,
                  "real dispatch freezes effective diagnostic values");
            check(slot.diagnostic_color_policy.requested_preserve_lighting==lighting &&
                  g_recorded_color_policy.policy.color_reason==p.color_reason,
                  "same policy feeds diagnostics and UI snapshot");
        }
    }
    auto unknown=ResolveNrColorPolicy(0,true,CodecColorMode::Automatic,true,true,1,true);
    check(!unknown.color_allowed && unknown.color_reason==NrColorReason::Unknown,
          "absent mode is not a supported live contract");
    g_config={}; g_config.mode=1; g_config.preserve_lighting=true;
    ColorContract c{}; Slot slot{}; slot.result_format=DXGI_FORMAT_R11G11B10_FLOAT;
    DispatchPolicy(slot,g_codec.combine,c,true,CodecColorMode::Automatic,true);
    auto snapshot=SnapshotColorPolicy();
    check(snapshot.status==NrColorPolicyStatus::Current && !held,"coherent current snapshot unlocks");
    check(snapshot.policy.effective_color_strength==0 &&
          !snapshot.policy.effective_preserve_lighting,"Wuthering Waves HDR R11 post-SR remains bounded");
    // Encode/copy must not overwrite the actual HDR composition with a default SDR contract.
    DispatchPolicy(slot,nullptr,c,false,CodecColorMode::Automatic,false);
    check(SnapshotColorPolicy().policy.color_reason==NrColorReason::UnverifiedHdr,
          "non-Combine default contract cannot publish");
    check(slot.diagnostic_color_strength==0 && !slot.diagnostic_preserve_lighting,
          "unarmed dispatch cannot corrupt pending sample");
    auto status=[&](unsigned gen,unsigned long long feature,int mode,bool active,bool bypass,
                    unsigned long long now) {
        return ColorPolicyStatusFor(snapshot,gen,feature,mode,active,bypass,now);
    };
    check(status(7,12,1,true,false,4000)==NrColorPolicyStatus::Current,"two-second freshness boundary");
    check(status(7,12,1,true,false,4001)==NrColorPolicyStatus::Stale,"stale sample unknown");
    check(status(7,12,1,true,false,1999)==NrColorPolicyStatus::Stale,"clock reversal unknown");
    check(status(8,12,1,true,false,2000)==NrColorPolicyStatus::Pending,"pending configuration unknown");
    check(status(7,12,2,true,false,2000)==NrColorPolicyStatus::Pending,"mode switch unknown");
    check(status(7,13,1,true,false,2000)==NrColorPolicyStatus::ContractChanged,"new game contract unknown");
    check(status(7,12,1,false,false,2000)==NrColorPolicyStatus::Inactive,"disabled unknown");
    check(status(7,12,1,true,true,2000)==NrColorPolicyStatus::Bypassed,"bypass unknown");
    check(status(8,12,1,true,true,2000)==NrColorPolicyStatus::Bypassed,
          "current bypass outranks pending configuration");
    check(status(7,12,2,true,true,2000)==NrColorPolicyStatus::Bypassed,
          "current bypass outranks pending mode");
    check(status(7,13,1,true,true,2000)==NrColorPolicyStatus::Bypassed,
          "current bypass outranks historical contract");
    check(ColorPolicyStatusFor({},7,12,1,true,true,2000)==NrColorPolicyStatus::Bypassed,
          "current bypass is visible before any composition sample");
    check(status(8,13,2,false,true,2000)==NrColorPolicyStatus::Inactive,
          "master off remains inactive despite an old bypass reason");
    check(ColorPolicyStatusFor({},7,12,1,true,false,2000)==NrColorPolicyStatus::NoFrame,
          "startup does not assume SDR");
    Config next=g_config; bool changed=false;
    ImGui::try_edits=true; Controls(next,false,changed);
    check(ImGui::colour_disabled && ImGui::lighting_disabled && !changed &&
          next.color_strength==1 && next.preserve_lighting,"blocked controls retain both requests");
    check(ImGui::text.find("requested 1.00 / effective 0.00")!=std::string::npos &&
          ImGui::text.find("requested on / effective off")!=std::string::npos,
          "real controls show actual suppressed values");
    g_last_bypass_reason=static_cast<int>(BypassReason::Guard);
    next=g_config; next.mode=2; changed=true; ImGui::try_edits=false;
    ImGui::text.clear(); Controls(next,true,changed);
    check(ImGui::text.find("The current frame is bypassed")!=std::string::npos &&
          ImGui::text.find("Settings pending")==std::string::npos &&
          ImGui::text.find("Last recorded blend")==std::string::npos,
          "real pending controls cannot conceal a current bypass with historical values");
    check(next.color_strength==g_config.color_strength &&
          next.preserve_lighting==g_config.preserve_lighting &&
          g_recorded_color_policy.policy.requested_color_strength==1 &&
          g_recorded_color_policy.policy.effective_color_strength==0,
          "bypass display retains requests and the historical policy snapshot");
    next=g_config; changed=false; ImGui::try_edits=true; ImGui::text.clear();
    Controls(next,false,changed);
    check(changed && next.color_strength==.25f && !next.preserve_lighting &&
          ImGui::text.find("The current frame is bypassed")!=std::string::npos &&
          ImGui::text.find("Settings pending")==std::string::npos,
          "editing either control during bypass keeps bypass visible and saves new requests");
    g_last_bypass_reason=0; ImGui::try_edits=true;
    next.mode=2; changed=true; ImGui::text.clear(); Controls(next,false,changed);
    check(!ImGui::colour_disabled && !ImGui::lighting_disabled,
          "mode edit cannot inherit previous mode's disabled controls");
    check(ImGui::text.find("Settings pending")!=std::string::npos,"edited UI does not claim current values");
    g_recorded_color_policy={}; next=g_config; changed=false; ImGui::try_edits=false;
    ImGui::text.clear(); Controls(next,false,changed);
    check(!ImGui::colour_disabled && !ImGui::lighting_disabled,
          "unknown contract does not pretend a known restriction");
    g_config.mode=2; DispatchPolicy(slot,g_codec.combine,c,true,CodecColorMode::Automatic,true);
    check(SnapshotColorPolicy().policy.effective_preserve_lighting,
          "switch back to NR-before-SR restores saved lighting request");
    slot.result_format=DXGI_FORMAT_R16G16B16A16_FLOAT; g_config.mode=1;
    DispatchPolicy(slot,g_codec.combine,c,true,CodecColorMode::Automatic,true);
    check(SnapshotColorPolicy().policy.effective_color_strength==1,
          "supported FP16 contract restores saved colour request");
    for(auto reason:{NrColorReason::Unknown,NrColorReason::NonHdr,NrColorReason::LinearSdr,
                     NrColorReason::SrgbSdr,NrColorReason::ModelValueDomain,
                     NrColorReason::PostSrFp16UnitExposure,NrColorReason::UnverifiedHdr})
        check(std::strlen(NrColorReasonName(reason))>0,"stable export colour reason");
    std::printf("Color policy: %u contracts, %u checks, %u failures.\n",cases,checks,failures);
    return failures?1:0;
}
'''
for marker, replacement in {
    "COLOR_CONTRACT": definition(source, "struct ColorContract {", True),
    "UNITY_EXPOSURE": definition(source, "bool IsUnityExposureContract("),
    "CODEC_ENUM": definition(source, "enum class CodecColorMode", True),
    "PEAK_SHOULDER": definition(source, "bool UsePeakShoulder("),
    "DISPATCH": dispatch,
    "UI_BLOCK": ui_block,
}.items():
    harness = harness.replace(marker, replacement)
with tempfile.TemporaryDirectory(prefix="nr-color-policy-") as directory:
    cpp = Path(directory) / "policy.cpp"
    exe = Path(directory) / "policy.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([args.compiler, "-std=c++17", "-O2", "-static", "-Wall", "-Wextra",
                    "-I", str(root / "src"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

"""Real light helpers, extracted configuration transactions and Win32 INI IO.

No renderer, GPU, model or game is constructed. Runtime logging and bypass
publication are seams; normalization, history comparisons, apply/undo and
persistence execute production code.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def definition(text,marker,semicolon=False):
    start=text.index(marker); end=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end+int(semicolon)]

HARNESS=r'''
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#include "nr_light_style.h"
#include "nr_work_config.h"
#include "nr_f8_types.h"
enum class NrJitterMode:int {Legacy=0,GameInputPixel=1};
CHAIN_CONFIG
ChainConfig g_config;
CRITICAL_SECTION g_chain_cs;
wchar_t g_ini_path[MAX_PATH]={};
std::atomic<unsigned> g_config_generation{1},g_history_generation{1};
std::atomic<bool> g_enabled_fast{true},g_config_persistence_failed{false};
std::atomic<unsigned long> g_config_persistence_error{0};
std::atomic<unsigned long long> g_next_config_poll_tick{0},g_nr_successful_evaluations{0};
ULONGLONG g_config_read_tick=0;
namespace nr_f8 {std::atomic<bool> g_config_hold{false};}
void Log(const char*,...) {}
enum class BypassReason {WaitingForFeature1,MasterDisabled};
void SetBypassReason(BypassReason) {}
void RecordWorkRequestStatus(nr_work_config::Status) {}
unsigned writes=0,checks=0,failures=0;
std::wstring fail_key;
std::vector<std::wstring> write_keys;
void check(bool ok,const char *label) {
    ++checks;
    if (!ok) {++failures;std::printf("FAIL %s\n",label);}
}
BOOL TestProfileWrite(LPCWSTR app,LPCWSTR key,LPCWSTR value,LPCWSTR file) {
    if (key) {++writes;write_keys.emplace_back(key);}
    if (key && fail_key==key) {SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return ::WritePrivateProfileStringW(app,key,value,file);
}
READ_FLOAT
FINITE
NORMALIZE
SAME_CONFIG
SAME_HISTORY
ACTIVE_STRENGTH
TUNING_TYPE
TUNING
SAME_TUNING
IDENTITY_TYPE
STAGE_INFO_TYPE
JSON_BUFFER
CONFIG_JSON
FRAME_JSON
const char *kBuildIdentity="light-style-metadata-fixture";
STAGE_WRITE_BYTES
STAGE_WRITE_METADATA
unsigned LightFlags(const ChainConfig &config,int pipeline) {
    const auto &g_config=config;
    const struct {int combine=1,combine_v2=2;} g_codec;
    const unsigned source_flags=0;
LIGHT_GATE
LIGHT_FLAGS
        0u;
    return base_codec_flags;
}
#define WritePrivateProfileStringW TestProfileWrite
WRITE_INT
WRITE_FLOAT
SAVE_CONFIG
#undef WritePrivateProfileStringW
APPLY_CONFIG
enum class OverlayApplyResult {Applied,Busy,Conflict};
APPLY_OVERLAY
// The distinct full-picture/post-resolution reset is tested by test_picture_reset.
OverlayApplyResult ApplyPictureReset(ChainConfig,unsigned) {return OverlayApplyResult::Conflict;}
APPLY_LIGHT
RESTORE

ChainConfig LoadGroup() {
    ChainConfig next;
    next.color_strength=ReadIniFloat(L"ColorStrength",1.f);
LOAD_GROUP
    next.light_preset_color=ReadIniFloat(L"LightPresetColor",next.color_strength);
    NormalizeConfig(next);
    return next;
}
void Seed(unsigned light_mode=0) {
    g_config={};g_config.mode=1;g_config.enabled=false;g_config.depth_mode=1;
    g_config.style=2;g_config.intensity=.6f;g_config.work_mode=5;g_config.custom_work_scale=.8f;
    g_config.independent_chroma_composition=true;g_config.light_broad=.4f;
    g_config.light_control_mode=light_mode;
    g_config.color_strength=.37f;nr_light_style::MarkCustom(g_config);NormalizeConfig(g_config);
    g_config_generation=1;g_history_generation=1;nr_f8::g_config_hold=false;
    fail_key.clear();SaveConfig(g_config);writes=0;write_keys.clear();
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=2)return 2;
    wcscpy_s(g_ini_path,argv[1]);InitializeCriticalSection(&g_chain_cs);
    for(unsigned light_mode:{0u,1u}) for(unsigned preset=0;preset<3;++preset) {
        Seed(light_mode);const auto before=g_config;const auto tuning=CurrentNrTuningSignature();
        auto next=g_config;nr_light_style::Write(next,nr_light_style::Select(preset,next.color_strength,next.light_control_mode));
        nr_light_style::UndoState undo;
        check(ApplyLightStyleOverlay(next,1,false,false,undo)==OverlayApplyResult::Applied,"preset applied");
        check(g_config_generation==2 && g_history_generation==1,"one commit, no history reset");
        check(SameNrTuningSignature(tuning,CurrentNrTuningSignature()),"no model recreation signature");
        auto unrelated=g_config;nr_light_style::Write(unrelated,nr_light_style::Read(before));unrelated.perf_epoch=before.perf_epoch;
        check(SameConfig(unrelated,before),"preset write set excludes every unrelated field");
        const auto value=nr_light_style::Read(g_config);
        check(value.preset==preset && value.version==1 && value.preset_color==value.color,"resolved preset metadata");
        check(value.control_mode==light_mode,"all presets preserve the selected light control mode");
        check(value.color==(preset==1?.8f:.37f),"only Natural changes existing colour");
        if(preset==1)check(value.broad==1.f&&value.dark==.85f&&value.reflection==1.1f&&value.structure==1.05f&&value.glow==.3f,"Natural independent coefficients");
        else check(value.broad==1.f&&value.dark==1.f&&value.reflection==1.f&&value.structure==1.f&&value.glow==(preset==2?.7f:0.f),"neutral or less-glow coefficients");
        check(nr_light_style::SameSnapshot(value,nr_light_style::Read(LoadGroup())),"real INI restart preserves snapshot");
        const auto after=g_config;
        check(undo.Restore(next,2),"undo restores group snapshot into next");
        // Undo starts from the current config so its unrelated fields and perf epoch survive.
        next=after;check(undo.Restore(next,2),"undo can prepare once before commit");
        check(ApplyLightStyleOverlay(next,2,false,true,undo)==OverlayApplyResult::Applied,"undo atomic commit");
        check(!undo.valid&&nr_light_style::SameSnapshot(nr_light_style::Read(before),nr_light_style::Read(g_config)),"undo restores metadata and consumes receipt");
        unrelated=g_config;nr_light_style::Write(unrelated,nr_light_style::Read(after));unrelated.perf_epoch=after.perf_epoch;
        check(SameConfig(unrelated,after),"undo does not restore unrelated fields");
        check(g_history_generation==1,"undo preserves history");
    }
    Seed();nr_light_style::UndoState undo;
    auto next=g_config;nr_light_style::Write(next,nr_light_style::Select(1,next.color_strength,next.light_control_mode));
    nr_f8::g_config_hold=true;
    check(ApplyLightStyleOverlay(next,1,false,false,undo)==OverlayApplyResult::Busy,"capture rejects preset");
    check(g_config_generation==1&&writes==0&&!undo.valid,"busy does not publish or persist");
    nr_f8::g_config_hold=false;
    check(ApplyLightStyleOverlay(next,0,false,false,undo)==OverlayApplyResult::Conflict,"stale pending edit rejected");
    check(g_config_generation==1&&writes==0&&!undo.valid,"conflict leaves source unchanged");
    check(ApplyLightStyleOverlay(next,1,false,false,undo)==OverlayApplyResult::Applied,"fresh preset succeeds");
    auto other=g_config;other.intensity=1.7f;ApplyOverlayConfig(other,2);
    const auto latest=g_config;const auto later_writes=writes;
    next=g_config;check(undo.Restore(next,2),"old receipt can only prepare old expected generation");
    check(ApplyLightStyleOverlay(next,2,false,true,undo)==OverlayApplyResult::Conflict,"undo rejects subsequent generation");
    check(SameConfig(g_config,latest)&&writes==later_writes&&!undo.valid,"stale undo preserves newer model settings");

    Seed();next=g_config;nr_light_style::Write(next,nr_light_style::Select(1,next.color_strength,next.light_control_mode));
    ApplyLightStyleOverlay(next,1,false,false,undo);
    writes=0;write_keys.clear();const auto tuning=CurrentNrTuningSignature();
    for(unsigned preview:{1u,2u,0u}) {
        next=g_config;next.light_preview=preview;
        const auto expected=g_config_generation.load();
        check(ApplyLightStyleOverlay(next,expected,false,false,undo)==OverlayApplyResult::Applied,"preview uses normal transaction");
        check(writes==0&&g_history_generation==1&&SameNrTuningSignature(tuning,CurrentNrTuningSignature()),"preview writes no INI and resets no NR history");
        check(g_config.light_preset==1,"preview does not mark preset custom");
    }
    g_config.light_preview=2;check(LoadGroup().light_preview==2,"INI polling preserves process preview");
    g_config={};check(LoadGroup().light_preview==0,"new process starts outside preview");
    next=LoadGroup();next.color_strength=.53f;NormalizeConfig(next);
    check(next.light_preset==nr_light_style::Custom,"external colour edit marks Custom");
    nr_light_style::Write(next,nr_light_style::Select(0,next.color_strength,next.light_control_mode));
    next.light_broad=.5f;nr_light_style::MarkCustom(next);next.light_broad=1.f;NormalizeConfig(next);
    check(next.light_preset==nr_light_style::Custom,"manual edits stay Custom even when returned to preset values");
    next.light_control_mode=1;next.light_preview=2;nr_light_style::Reset(next);
    check(!nr_light_style::Active(next)&&next.color_strength==.53f&&next.light_control_mode==1,"group reset exits preview, keeps colour and control mode");
    next.light_control_mode=0;
    RestoreRecommendedPictureSettings(next);
    check(!nr_light_style::Active(next)&&next.color_strength==1.f&&next.light_preset==0&&next.light_control_mode==1,"full recommendation resets group neutral in whole light/dark mode");

    DeleteFileW(g_ini_path);WritePrivateProfileStringW(L"NRBeforeSR",L"ColorStrength",L"0.37",g_ini_path);
    g_config={};const auto old=LoadGroup();
    check(!nr_light_style::Active(old)&&old.color_strength==.37f&&old.light_preset==0&&old.light_preset_color==.37f,"old INI gets neutral group with existing colour");
    check(old.light_control_mode==0,"old INI missing LightControlMode preserves detail layers");
    for(const wchar_t *invalid:{L"99",L"-1"}) {
        WritePrivateProfileStringW(L"NRBeforeSR",L"LightControlMode",invalid,g_ini_path);
        check(LoadGroup().light_control_mode==0,"unknown control modes retain legacy semantics");
    }
    WritePrivateProfileStringW(L"NRBeforeSR",L"LightControlMode",L"1",g_ini_path);
    check(LoadGroup().light_control_mode==1,"explicit whole light/dark preference loads without migration");
    WritePrivateProfileStringW(L"NRBeforeSR",L"LightPreview",L"2",g_ini_path);
    check(LoadGroup().light_preview==0,"an INI preview key is ignored");
    WritePrivateProfileStringW(L"NRBeforeSR",L"LightStyleVersion",L"9",g_ini_path);
    WritePrivateProfileStringW(L"NRBeforeSR",L"LightReflection",L"1.4",g_ini_path);
    const auto future=LoadGroup();check(future.light_reflection==1.4f&&future.light_preset==3,"unknown preset version preserves bounded values as Custom");
    next=future;next.light_preview=99;next.light_glow=INFINITY;next.light_broad=-2;
    NormalizeConfig(next);check(next.light_preview==0&&next.light_glow==0&&next.light_broad==0,"invalid values bounded without activating preview");

    Seed(1);next=g_config;nr_light_style::Write(next,nr_light_style::Select(1,next.color_strength,next.light_control_mode));
    for(const wchar_t *key:{L"LightReflection",L"LightControlMode"}) {
        fail_key=key;
        check(!SaveConfig(next)&&g_config_persistence_failed,"failed snapshot write reported");
        check(GetPrivateProfileIntW(L"NRBeforeSR",L"LightStyleVersion",99,g_ini_path)==0,"partial snapshot cannot publish version");
        fail_key.clear();check(SaveConfig(next)&&!g_config_persistence_failed,"complete save recovers latch");
    }
    check(nr_light_style::SameSnapshot(nr_light_style::Read(next),nr_light_style::Read(LoadGroup())),"recovered INI snapshot complete");
    for(const auto &key:write_keys)check(key!=L"LightPreview","preview never has a persistence writer");

    Seed();nr_light_style::Reset(g_config);SaveConfig(g_config);writes=0;
    const auto neutral=g_config;const auto neutral_tuning=CurrentNrTuningSignature();
    next=g_config;next.light_control_mode=1;undo.Clear();
    check(!SameConfig(next,neutral)&&SameHistoryConfig(next,neutral),"control mode changes config without changing NR history contract");
    check(!nr_light_style::Active(next),"neutral whole light/dark selection alone does not activate D");
    nr_f8::g_config_hold=true;
    check(ApplyLightStyleOverlay(next,1,false,false,undo)==OverlayApplyResult::Busy&&writes==0&&SameConfig(g_config,neutral),"capture freeze rejects mode-only edit without mutation");
    nr_f8::g_config_hold=false;
    check(ApplyLightStyleOverlay(next,1,false,false,undo)==OverlayApplyResult::Applied,"mode-only edit uses normal transaction");
    check(g_config_generation==2&&g_history_generation==1&&SameNrTuningSignature(neutral_tuning,CurrentNrTuningSignature()),"mode-only edit neither resets nor recreates NR");
    check(LoadGroup().light_control_mode==1&&undo.Restore(next,2)&&next.light_control_mode==0,"mode-only edit persists and undo restores previous scope");
    check(ApplyLightStyleOverlay(next,2,false,true,undo)==OverlayApplyResult::Applied&&g_config.light_control_mode==0&&g_history_generation==1,"mode undo commits without history reset");
    for(unsigned scope:{0u,1u}) {
        ChainConfig config;config.light_control_mode=scope;
        check(LightFlags(config,1)==0&&LightFlags(config,2)==0,"neutral scope never sets either D shader bit");
        config.light_glow=.5f;
        const unsigned expected=262144u|(scope==1?524288u:0u);
        check(LightFlags(config,1)==expected&&LightFlags(config,2)==expected,"active supported pre/post carries only selected scope bit");
        check(LightFlags(config,0)==0,"unsupported pipeline never acquires D scope");
        config.mode=1;
        check(LightFlags(config,2)==0,"scope does not expand V2 post route admission");
    }
    {
        ChainConfig requested,actual;requested.light_control_mode=1;requested.light_glow=.5f;
        NrStageCaptureInfo f8_info;f8_info.light_supported=true;
        const unsigned base_codec_flags=LightFlags(requested,1);
        f8_info.codec_flags=base_codec_flags;
CAPTURE_MODE_BIND
        const auto encoded=FrameJson(f8_info,{},requested,actual);
        check(encoded.ok&&std::strstr(encoded.data,"\"lightControlModeRecorded\":1")&&std::strstr(encoded.data,"\"lightControlMode\":0"),"per-dispatch captured scope remains distinct from another config snapshot");
        f8_info.codec_flags=262144u;f8_info.light_control_mode=0;
        const auto old_record=FrameJson(f8_info,{},requested,actual);
        check(old_record.ok&&std::strstr(old_record.data,"\"lightControlModeRecorded\":0")&&std::strstr(old_record.data,"\"lightControlMode\":1"),"old recorded scope is not relabelled by a newer whole-mode request");
        f8_info.codec_flags=0;f8_info.light_control_mode=1;
        const auto neutral_record=FrameJson(f8_info,{},requested,actual);
        check(neutral_record.ok&&std::strstr(neutral_record.data,"\"lightControlModeRecorded\":-1"),"neutral dispatch does not claim a recorded scope");
        f8_info.codec_flags=262144u|524288u;f8_info.light_supported=false;
        const auto unavailable=FrameJson(f8_info,{},requested,actual);
        check(unavailable.ok&&std::strstr(unavailable.data,"\"lightControlModeRecorded\":-1"),"unsupported route does not claim a recorded scope");
        const std::wstring directory=std::wstring(g_ini_path)+L".stages";
        check(CreateDirectoryW(directory.c_str(),nullptr)!=FALSE,"fresh stage metadata directory created");
        f8_info.light_supported=true;
        check(WriteStageCaptureMetadata(directory.c_str(),f8_info,false),"actual stage metadata writer accepts recorded scope");
        const std::wstring path=directory+L"\\metadata.txt";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        char metadata[4096]{};DWORD read=0;
        const bool readable=file!=INVALID_HANDLE_VALUE&&ReadFile(file,metadata,sizeof(metadata)-1,&read,nullptr);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        check(readable&&std::strstr(metadata,"lightControlModeRecorded=1\n")&&std::strstr(metadata,"complete=0\n"),"persisted stage metadata distinguishes recorded scope from image completeness");
    }
    DeleteCriticalSection(&g_chain_cs);
    std::printf("Light style: %u checks, %u failures; real helpers/config/Win32 INI, no GPU\n",checks,failures);
    return failures?1:0;
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default=os.environ.get('CXX','clang++'))
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('Real Win32 INI fixture requires Windows')
    compiler=shutil.which(args.compiler)
    if not compiler:raise SystemExit('C++ compiler not found')
    core=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    group=(ROOT/'src/nr_light_style_ui.inl').read_text(encoding='utf-8')
    ui=(ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8')
    stream=(ROOT/'src/nr_f8_stream.inl').read_text(encoding='utf-8')
    stage=(ROOT/'src/nr_stage_capture.inl').read_text(encoding='utf-8')
    dispatch=definition(core,'void DispatchCodec(')
    assert 'const bool before_sr = !rr_guarded && !compatibility_post && next.mode == 2;' in ui
    assert 'DrawLightStyleControls(next,before_sr' in ui
    assert 'ApplyLightStyleOverlay(pending,pending_generation,' in ui
    assert 'if (color_follow_changed) nr_light_style::MarkCustom(next);' in ui
    assert 'next.mode=' not in group and 'independent_chroma_composition' not in group
    assert group.index('##nr-light-reflection')<group.index('ImGui::CollapsingHeader')
    assert group.index('##nr-light-broad')>group.index('ImGui::CollapsingHeader')
    assert 'NR 修改前后差' in group and '绝对线性差乘 4' in group
    replacements={
        'CHAIN_CONFIG':definition(core,'struct ChainConfig',True),
        'READ_FLOAT':definition(core,'float ReadIniFloat('),
        'FINITE':definition(core,'float FiniteOr('),
        'NORMALIZE':definition(core,'void NormalizeConfig('),
        'SAME_CONFIG':definition(core,'bool SameConfig('),
        'SAME_HISTORY':definition(core,'bool SameHistoryConfig('),
        'ACTIVE_STRENGTH':definition(core,'float ActiveTransferStrength('),
        'TUNING_TYPE':definition(core,'struct NrTuningSignature',True),
        'TUNING':definition(core,'NrTuningSignature CurrentNrTuningSignature('),
        'SAME_TUNING':definition(core,'bool SameNrTuningSignature('),
        'IDENTITY_TYPE':definition(core,'struct NrDiagnosticIdentity',True),
        'STAGE_INFO_TYPE':definition(core,'struct NrStageCaptureInfo',True),
        'JSON_BUFFER':definition(stream,'struct JsonBuffer',True),
        'CONFIG_JSON':definition(stream,'void ConfigJson('),
        'FRAME_JSON':definition(stream,'JsonBuffer FrameJson('),
        'STAGE_WRITE_BYTES':definition(stage,'bool WriteStageCaptureBytes('),
        'STAGE_WRITE_METADATA':definition(stage,'bool WriteStageCaptureMetadata('),
        'LIGHT_GATE':dispatch[dispatch.index('    const bool light_style_pre_supported'):dispatch.index('    // Preserve both legacy entry points')],
        'LIGHT_FLAGS':dispatch[dispatch.index('    const unsigned int base_codec_flags'):dispatch.index('        ((v2_combine && slot.edit_field_recorded')],
        'CAPTURE_MODE_BIND':next(line for line in dispatch.splitlines() if 'f8_info.light_control_mode=' in line),
        'WRITE_INT':definition(core,'bool WriteIniInt(const wchar_t *key, int value) {'),
        'WRITE_FLOAT':definition(core,'bool WriteIniFloat(const wchar_t *key, float value) {'),
        'SAVE_CONFIG':definition(core,'bool SaveConfig('),
        'APPLY_CONFIG':definition(core,'bool ApplyConfig('),
        'APPLY_OVERLAY':definition(core,'OverlayApplyResult ApplyOverlayConfig('),
        'APPLY_LIGHT':definition(group,'OverlayApplyResult ApplyLightStyleOverlay('),
        'RESTORE':(ROOT/'src/nr_recommended_picture.inl').read_text(encoding='utf-8'),
    }
    reload=definition(core,'void ReloadConfig(')
    replacements['LOAD_GROUP']=reload[reload.index('    next.light_broad ='):reload.index('    next.high_strength_protection =')]
    harness=HARNESS
    for token,value in replacements.items():harness=harness.replace('\n'+token+'\n','\n'+value+'\n')
    with tempfile.TemporaryDirectory(prefix='nr-light-style-') as temporary:
        directory=Path(temporary);source=directory/'light.cpp';exe=directory/'light.exe'
        source.write_text(harness,encoding='utf-8')
        subprocess.run([compiler,'-std=c++20','-O0','-Wall','-Wextra','-Werror','-static','-municode',
            '-I',str(ROOT/'src'),str(source),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(directory/'settings.ini')],check=True,timeout=20)

if __name__=='__main__':main()

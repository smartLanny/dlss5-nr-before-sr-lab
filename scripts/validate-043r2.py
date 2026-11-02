#!/usr/bin/env python3
"""Host checks for the r2 integration; GPU copying is tested separately on WARP."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def definition(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 0
    for at in range(brace, len(source)):
        if source[at] == '{': depth += 1
        elif source[at] == '}':
            depth -= 1
            if not depth: return source[start:at+1]
    raise ValueError('unclosed definition: ' + marker)

HARNESS = r'''
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "nr_feedback_text.h"
#include "nr_feedback_ticket.h"
#define NR_HAS_DETAIL_STABILITY 1
struct ChainConfig { int mode=2; float transfer_strength=1.2f, post_transfer_strength=0.35f; };
ACTIVE_STRENGTH
RESET_POLICY
namespace ImGui {
float observed=0; bool edit=false;
}
float FiniteOr(float value,float){return value;}
bool SettingsSliderFloat(const char*,const char*,float *value,float,float,const char*) {
    ImGui::observed=*value; if(ImGui::edit)*value=1.5f; return ImGui::edit;
}
unsigned checks=0;
void check(bool ok,const char *name){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
bool controls(ChainConfig &next){
    bool changed=false;
    const auto ui=[](const char *en,const char*){return en;};
UI_STRENGTH
    return changed;
}
int main(){
    ChainConfig next;
    next.mode=1;check(!controls(next),"post UI read does not edit");
    check(ImGui::observed==1.2f && next.post_transfer_strength==0.35f,"post UI uses actual unified strength and leaves dormant legacy field untouched until edit");
    next.mode=2;check(!controls(next) && ImGui::observed==1.2f,"pre UI shows pre strength");
    next.mode=1;ImGui::edit=true;check(controls(next),"edit is propagated");
    check(next.transfer_strength==1.5f && next.post_transfer_strength==1.5f,"explicit shared-slider edit updates both modes");
    for(unsigned warm:{0u,1u,3u})for(unsigned frames:{0u,1u,2u,8u}) {
        check(KeepNativePostSrImage(false,true,frames,warm),"reset suppresses post result even zero warmup");
        check(KeepNativePostSrImage(true,false,frames,warm),"identity retains original");
        check(KeepNativePostSrImage(false,false,frames,warm)==(frames<=warm),"warmup boundary");
    }
    char out[nr_feedback_ticket::kCapacity];
    const char *metadata="session=DLSS5_CORE_1_2_3\nbuild=test\nrequested.mode=1\nrequested.blend=0.35\n"
        "raw.private=C:\\Users\\SECRET\\raw.log\nartifact.addonSha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n";
    check(nr_feedback_ticket::Build(out,sizeof(out),metadata,false),"partial snapshot yields ticket");
    check(std::strstr(out,"DLSS5_CORE_1_2_3") && std::strstr(out,"requested.blend=0.35"),"ticket uses capture not UI values");
    check(!std::strstr(out,"SECRET") && !std::strstr(out,"raw.private"),"unlisted private fields omitted");
    check(std::strstr(out,"partial (still useful)")!=nullptr,"partial is not silently complete");
    check(!nr_feedback_ticket::Build(out,8,metadata,true) && !out[0],"small output fails closed");
    check(!nr_feedback_ticket::Build(out,sizeof(out),nullptr,true) && !out[0],"absent report clears result");
    check(nr_feedback_ticket::Build(out,sizeof(out),"session=DLSS5_CORE_8_9_10\nexe.leaf=C:\\Users\\SECRET\\x.exe\n",true),"fresh ticket");
    check(!std::strstr(out,"DLSS5_CORE_1_2_3") && !std::strstr(out,"SECRET"),"no stale session or path survives");
    nr_feedback_text::Summary summary;
    for(unsigned i=0;i<500;++i)summary.Push(std::string(100,'x'));
    summary.Push("Another ReShade instance exists. Aborting initialization.");
    summary.Push("DXGI_ERROR_DEVICE_REMOVED");
    for(unsigned i=0;i<2000;++i)summary.Push(std::string(100,'x'));
    const auto text=summary.Text(1,1,true);
    check(text.size()<nr_feedback_text::kSummaryLimit,"summary still bounded");
    check(text.find("Another ReShade instance")!=std::string::npos,"first duplicate-init survives later log noise");
    check(text.find("DXGI_ERROR_DEVICE_REMOVED")!=std::string::npos,"first device failure survives later noise");
    std::printf("PASS r2 production UI/policy/ticket/summary: %u checks\n",checks);
}
'''

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True)
    args = p.parse_args()
    source = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    ui = (ROOT/'src/nr_unified_settings.inl').read_text(encoding='utf-8-sig')
    core = (ROOT/'src/nr_feedback_core.inl').read_text(encoding='utf-8-sig')
    sequence = (ROOT/'src/nr_f8_sequence.inl').read_text(encoding='utf-8-sig')
    post = definition(source, 'PostSrSubstitution PrepareAfterSr(')
    assert 'SetResource(substitution.params, "Output"' not in post, 'native SR Output is still redirected'
    assert 'SetUInt(substitution.params, "DLSS.Output.Subrect' not in post
    assert 'slot.snapshots_native_output = true;' in source
    finish = definition(source, 'bool FinishAfterSr(')
    assert 'KeepNativePostSrImage(g_config.identity_use_original,' in finish
    assert 'slot.suppress_transfer_this_frame' in finish
    snapshot = definition(source, 'bool DispatchPostSrNativeSnapshot(')
    assert snapshot.index('CopyPostSrFp16Snapshot(') < snapshot.index('if (slot.snapshot_heap == nullptr)')
    assert 'const bool snapshot = contract != PostSrStorageContract::Fp16;' in definition(source,'bool SupportsPostSrStorage('), 'do not impose SRV flags on FP16 copy sources'
    assert 'g_submission[0] = 0' in definition(core,'bool Request(')
    assert 's.report' in definition(core,'DWORD WINAPI Worker(')
    assert 'nr_feedback_ticket::Build' in definition(core,'void Publish(')
    assert 'g_submission[' in core
    assert 'Copy core path' in sequence
    assert ui.count('BeginTabItem(')==6 and ui.count('EndTabItem(')==6 and 'nr-main-tabs' in ui
    assert ui.index('Picture###nr-picture') < ui.index('Models###nr-models') < ui.index('Lighting###nr-lighting') < ui.index('Experiments###nr-experiments') < ui.index('Developer###nr-developer') < ui.index('Feedback###nr-feedback-diagnostics')
    status_ui=definition(sequence,'void DrawCoreStatus(')
    release=status_ui.index('ReleaseSRWLockShared(&g_feedback_lock)')
    assert status_ui.index('WideToUtf8(g_feedback_last_zip,path,sizeof(path))') < release
    assert status_ui.index('const auto status=g_feedback_status.load()') < release
    assert status_ui.index('Copy core path') > release
    assert 'return config.transfer_strength;' in definition(source,'float ActiveTransferStrength(')

    start=ui.index('            float strength=ActiveTransferStrength(next);')
    stop=ui.index('            SettingsTooltip(ui("Scales final brightness/detail edits, not model strength.', start)
    text=HARNESS.replace('ACTIVE_STRENGTH',definition(source,'float ActiveTransferStrength('))
    text=text.replace('RESET_POLICY',definition((ROOT/'src/nr_post_sr_snapshot.inl').read_text(), 'constexpr bool KeepNativePostSrImage('))
    text=text.replace('UI_STRENGTH',ui[start:stop])
    with tempfile.TemporaryDirectory(prefix='nr-r2-') as temp:
        cpp, exe = Path(temp)/'test.cpp', Path(temp)/'test.exe'
        cpp.write_text(text,encoding='utf-8')
        subprocess.run([args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-static','-I',str(ROOT/'src'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('r2 integration source assertions passed; not a game/runtime test')

if __name__ == '__main__': main()

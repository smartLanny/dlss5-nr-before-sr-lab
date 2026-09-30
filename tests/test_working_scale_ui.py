"""Execute the production scale widget and overlay footer with a frame recorder.

No GPU or model fixtures. --legacy-ref executes the old widget to demonstrate
the actual fallback/display feedback loop before the fix.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 0
    for at in range(brace, len(source)):
        depth += (source[at] == '{') - (source[at] == '}')
        if depth == 0:
            return source[start:at + 1]
    raise ValueError(marker)


PRELUDE = r'''
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "nr_precision_ui.h"
#include "nr_processing_route.h"
#define NR_HAS_DETAIL_STABILITY 1
struct ChainConfig {
    nr_processing::Start processing_start=nr_processing::Start::After;
    unsigned long long processing_selection=1;
    unsigned compat_percent=100;
    int work_mode=5;float custom_work_scale=.75f;
};
ChainConfig saved{},pending{};
struct Snapshot {ChainConfig config; unsigned generation=1;
    unsigned base_width=1920,base_height=1080,work_width=1882,work_height=1058;} current;
unsigned pending_generation=0,apply_calls=0,post_calls=0;
bool has_pending=false,edit_conflict=false,pending_picture_reset=false,pending_light_undo=false;
int light_undo=0,model_undo=0;
namespace nr_model_controls {enum class Action {None};}
auto pending_model_action=nr_model_controls::Action::None;
enum class OverlayApplyResult {Applied,Busy,Conflict};
std::atomic<unsigned> g_config_generation{1};
int g_chain_cs=0;bool lock_race=false;
void EnterCriticalSection(int*) {if(lock_race){lock_race=false;++g_config_generation;}}
void LeaveCriticalSection(int*) {}
OverlayApplyResult ApplySettingsOverlay(const ChainConfig& value,unsigned expected,bool,bool,int,
    nr_model_controls::Action,int) {
    if(expected!=g_config_generation.load())return OverlayApplyResult::Conflict;
    ++apply_calls;saved=value;++g_config_generation;return OverlayApplyResult::Applied;
}
namespace nr_post_settings {
struct Options {bool rr=true;unsigned percent=98;};
Options value{};std::atomic<unsigned long long> revision{0};bool reject=false;
Options Read(){return value;}
bool Request(Options next){++post_calls;if(reject)return false;value=next;++revision;++g_config_generation;return true;}
}
namespace nr_processing_runtime {
nr_processing::Start actual=nr_processing::Start::After;bool fg=false;
bool FgRedirectRequested(){return fg;}
bool PresentSelected(){return actual==nr_processing::Start::Present;}
nr_processing::Start Effective(){return actual;}
}
namespace nr_compat_present {struct Observation {unsigned source_width=3840,source_height=2160,work_width=3840,work_height=2160;};}
namespace nr_compat_present_runtime {bool TrySnapshot(nr_compat_present::Observation&){return true;}}
std::atomic<unsigned> g_last_dlss_input_width{1920},g_last_dlss_input_height{1080},g_last_dlss_output_width{3840},g_last_dlss_output_height{2160};
std::atomic<bool> g_work_request_rejected{false};
bool item_active=false,item_activated=false,item_released=false,slider_changed=false;
float slider_value=0,displayed=0,slider_min=0;std::string notes,observations;
namespace ImGui {
bool IsItemActive(){return item_active;}
bool IsAnyItemActive(){return item_active;}
bool IsItemActivated(){return item_activated;}
bool IsItemDeactivatedAfterEdit(){return item_released;}
void TextDisabled(const char* format,...) {char text[1024];va_list args;va_start(args,format);
    std::vsnprintf(text,sizeof(text),format,args);va_end(args);observations+=text;}
}
bool SettingsSliderFloat(const char*,const char*,float* value,float minimum,float,const char*) {
    displayed=*value;slider_min=minimum;if(slider_changed)*value=slider_value;return slider_changed;
}
void SettingsTooltip(const char*){}
void SettingsNote(const char* s){notes+=s;}
const char* ui(const char* en,const char*){return en;}
void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
'''


def run(legacy_ref=None, require_fixed=False):
    source = (subprocess.check_output(['git', '-C', str(ROOT), 'show',
              legacy_ref + ':src/nr_settings_ui.inl']).decode('utf-8') if legacy_ref else
              (ROOT / 'src/nr_unified_settings.inl').read_text(encoding='utf-8'))
    if legacy_ref:
        start = source.index('    // BEGIN WORKING SCALE REQUEST') if '// BEGIN WORKING SCALE REQUEST' in source else source.index('    const bool compatibility_mode=!nr_processing_runtime::FgRedirectRequested()')
        block = source[start:source.index('    DrawUniformLayerControls(', start)]
    else:
        start=source.index('#if NR_HAS_DETAIL_STABILITY')
        block=source[start:source.index('            DrawUniformLayerCount(',start)]
        block+='\nconst bool before_sr=nr_processing_runtime::Effective()==nr_processing::Start::Before;\n'
    footer = source[source.rindex('    if (changed) {'):]
    footer = definition(footer, '    if (changed) {') + '\n' + definition(footer, '    if (has_pending &&')
    frame = r'''
void frame(bool active=false,bool edited=false,float percent=0,bool released=false,bool rr_guarded=false) {
    item_activated=active&&!item_active;item_active=active;slider_changed=edited;slider_value=percent;item_released=released;
    current.config=saved;current.generation=g_config_generation.load();
    if(has_pending&&pending_generation!=current.generation){has_pending=false;edit_conflict=true;}
    ChainConfig next=has_pending?pending:current.config;
    bool changed=false,apply_discrete_edit=false;
    [[maybe_unused]] const bool compatibility_post=false;
''' + block + '\n(void)before_sr;\n' + footer + '\n}\n'
    legacy = r'''
int main(){
    frame();check(displayed==98,"old selected Post starts at 98");
    nr_processing_runtime::actual=nr_processing::Start::Present;
    frame();check(displayed==100,"old actual fallback incorrectly selects Compat 100");
    nr_processing_runtime::actual=nr_processing::Start::After;
    frame(true,true,80);
    nr_processing_runtime::actual=nr_processing::Start::Present;
    frame(true);check(displayed==100,"old in-flight Post drag rebounds to Compat 100");
    check(nr_post_settings::Read().percent==98&&saved.compat_percent==100,"rebound is UI field switching, not committed Post reset");
    std::puts("REPRODUCED production widget: selected Post 98 -> fallback Compat 100; active Post 80 drag displays Compat 100");
}
'''
    tests = r'''
int main(){
    frame();check(displayed==98,"Post initial request");
    nr_processing_runtime::actual=nr_processing::Start::Present;
    observations.clear();frame();check(displayed==98,"fallback must not select another setting");
    check(observations.find("Editing: Post-SR request")!=std::string::npos,"explicit saved request label");
    check(observations.find("3840 x 2160 -> model 3840 x 2160")!=std::string::npos,"actual Present dimensions remain separate");
    frame(true,true,80);check(post_calls==0&&apply_calls==0,"no publication during drag");
    nr_processing_runtime::actual=nr_processing::Start::Before;
    frame(true);check(displayed==80,"runtime route change preserves draft");
    frame(false,false,0,true);check(post_calls==1&&nr_post_settings::Read().percent==80,"release publishes Post once");
    frame();check(post_calls==1&&displayed==80&&saved.compat_percent==100,"next frame stable, other field retained");

    saved.processing_start=nr_processing::Start::Before;++g_config_generation;
    nr_processing_runtime::actual=nr_processing::Start::Present;
    frame();check(displayed==75,"chosen Before remains Before across fallback");
    frame(true,true,66);frame(true);check(apply_calls==0&&displayed==66,"Before draft private");
    frame(false,false,0,true);check(apply_calls==1&&std::abs(saved.custom_work_scale-.66f)<.0001f,"Before footer commits once");
    frame();check(apply_calls==1,"Before no duplicate publication");

    saved.processing_start=nr_processing::Start::Present;++g_config_generation;
    nr_processing_runtime::fg=false;frame();check(displayed==100,"explicit Present edits compatibility");
    frame(true,true,90);frame(false,false,0,true);check(saved.compat_percent==90&&apply_calls==2,"compatibility release");
    nr_processing_runtime::fg=true;frame();check(displayed==80,"Present plus FG uses Post");
    frame(true,true,72);nr_processing_runtime::fg=false;frame(true);
    frame(false,false,0,true);check(post_calls==1&&saved.compat_percent==90,"FG change cancels without retargeting");
    check(!notes.empty(),"conflict is visible");
    saved.processing_start=nr_processing::Start::Before;++g_config_generation;
    frame(false,false,0,false,true);check(displayed==80,"RR forces Post request");
    frame(true,true,77,false,true);frame(false,false,0,true,true);
    check(post_calls==2&&nr_post_settings::Read().percent==77,"RR Post release");

    saved.processing_start=nr_processing::Start::After;++g_config_generation;
    frame(true,true,60);++g_config_generation;frame(true);frame(false,false,0,true);
    check(post_calls==2&&nr_post_settings::Read().percent==77,"external config update discards stale Post drag");
    frame(true,true,61);++nr_post_settings::revision;frame(true);frame(false,false,0,true);
    check(post_calls==2,"even same-value external Post revision cancels");
    frame(true,true,62);++saved.processing_selection;frame(true);frame(false,false,0,true);
    check(post_calls==2,"explicit route retry cancels old edit");
    frame(true,true,63);lock_race=true;frame(false,false,0,true);
    check(post_calls==2,"locked generation check rejects concurrent publication");
    nr_post_settings::reject=true;frame(true,true,64);frame(false,false,0,true);
    check(post_calls==3&&nr_post_settings::Read().percent==77,"rejected request not treated as applied");
    nr_post_settings::reject=false;frame();check(displayed==77,"rejected request refreshes saved value");
    frame(true,true,70);frame(false);frame();check(post_calls==3&&displayed==77,"lost/deactivated item without release discards draft");
    // The lower post bound must survive the actual widget's observation,
    // release, persistence field and later route changes without becoming 50.
    frame();check(slider_min==25,"Post slider starts at 25");
    frame(true,true,25);frame(false,false,0,true);frame();
    check(post_calls==4&&displayed==25&&nr_post_settings::Read().percent==25,"Post quarter scale commits and stays displayed");
    frame(true,true,24);frame(false,false,0,true);frame();
    check(post_calls==4&&displayed==25,"Post typed below-bound request rejected without publication");
    saved.processing_start=nr_processing::Start::Present;++g_config_generation;
    frame();check(slider_min==25,"Present slider starts at 25");
    frame(true,true,25);frame(false,false,0,true);frame();
    check(saved.compat_percent==25&&displayed==25,"Present quarter scale preserved after release");
    saved.processing_start=nr_processing::Start::Before;++g_config_generation;
    const auto before_calls=apply_calls;frame();check(slider_min==50&&displayed==66,"Before range and saved field preserved");
    frame(true,true,25);frame(false,false,0,true);frame();
    check(apply_calls==before_calls&&displayed==66&&saved.compat_percent==25,"Before rejects quarter scale and retains other route");
    frame(false,false,0,false,true);check(slider_min==25&&displayed==25,"RR uses saved quarter Post request even when Before selected");
    saved.processing_start=nr_processing::Start::Present;nr_processing_runtime::fg=true;++g_config_generation;
    frame();check(slider_min==25&&displayed==25,"FG redirected compatibility uses quarter Post request");
    std::puts("Production working-scale widget + overlay footer: fallback, drag/release, RR/FG, external config/revision, locked race and rejection passed");
}
'''
    # The canonical Windows build sets NR_LLVM_BIN; elsewhere use a host C++ compiler.
    if os.environ.get('NR_LLVM_BIN'):
        compiler = Path(os.environ['NR_LLVM_BIN']) / 'clang++.exe'
    else:
        found = shutil.which(os.environ.get('CXX') or 'clang++')
        if not found:
            raise SystemExit('Set NR_LLVM_BIN (Windows) or CXX/clang++ on PATH')
        compiler = Path(found)
    with tempfile.TemporaryDirectory(prefix='nr-working-scale-') as directory:
        work = Path(directory)
        cpp, exe = work / 'fixture.cpp', work / 'fixture.exe'
        cpp.write_text(PRELUDE + frame + (legacy if legacy_ref and not require_fixed else tests), encoding='utf-8')
        subprocess.run([str(compiler), '-std=c++20', '-O0', '-static', '-Wall', '-Wextra',
                        '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--legacy-ref')
    parser.add_argument('--require-fixed', action='store_true', help='Run fixed behavior checks against the chosen source, for a negative control')
    args = parser.parse_args()
    run(args.legacy_ref, args.require_fixed)

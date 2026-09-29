"""Exercise the production notice renderer and route selection with a CPU UI recorder.

This verifies text/state decisions and button handling, not real mouse input,
GPU execution, or the final Core DLL's layout.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'nt', 'The local fixture uses the Windows toolchain')
class SettingsNotices(unittest.TestCase):
    def test_notices_and_effective_route_actions(self):
        configured = os.environ.get('NR_LLVM_BIN')
        bins = sorted((ROOT.parent / 'dlss5-lab' / 'toolchains').glob('llvm-mingw-*/bin'))
        compiler = (Path(configured) if configured else bins[-1] if bins else Path('missing')) / 'clang++.exe'
        self.assertTrue(compiler.is_file(), 'Set NR_LLVM_BIN to the pinned LLVM-MinGW bin directory')
        ui = (ROOT / 'src' / 'nr_settings_ui.inl').read_text(encoding='utf-8')
        route = ui[ui.index('bool DrawProcessingModeBlocks('):ui.index('void DrawColorControls(')]
        # Compile the production function, not a second implementation of its decision logic.
        source = PRELUDE + '\n' + route + '\n' + MAIN
        with tempfile.TemporaryDirectory(prefix='nr-settings-notices-') as directory:
            work = Path(directory)
            cpp = work / 'notices.cpp'
            exe = work / 'notices.exe'
            cpp.write_text(source, encoding='utf-8')
            built = subprocess.run([str(compiler), '-std=c++17', '-O1', '-static',
                                    '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                                    str(cpp), '-o', str(exe)], capture_output=True, text=True, timeout=45)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, encoding='utf-8', timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('0 failures; production notices and route UI, CPU recorder only', run.stdout)
            print(run.stdout.strip())


PRELUDE = r'''
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "nr_processing_route.h"
#include "nr_compat_present_policy.hpp"
struct ImVec2 { float x, y; ImVec2(float a, float b) : x(a), y(b) {} };
struct Style { ImVec2 ItemSpacing{7,7}; };
enum { ImGuiCol_Button, ImGuiCol_Header };
struct ChainConfig { int mode=2;nr_processing::Start processing_start=nr_processing::Start::Before;unsigned long long processing_selection=0; };
namespace nr_processing_runtime {
std::atomic<unsigned long long> next_selection{1};
nr_processing::Start actual=nr_processing::Start::After;
nr_processing::Start Effective(){return actual;}
bool TrySnapshot(nr_processing::Snapshot& value){value={};value.current=actual;return true;}
bool FgRedirectRequested(){return false;}
}
namespace nr_compat_present_runtime {
bool TrySnapshot(nr_compat_present::Observation& value){value={};return true;}
}
struct ConfigPolicyMigrationStatus {
    bool visible=false,extra_changed=false,extra_disabled=false,jitter_changed=false;
    bool persisted=false,failed=false;
    unsigned long error=0;
};
enum class BypassReason { None };
struct CompatibilityFallbackSnapshot {
    bool recent=false,recorded=false;
    int requested_mode=2,effective_mode=1;
    unsigned cause=0;
    BypassReason failure=BypassReason::None;
    unsigned long long attempts=0,recordings=0;
};
bool chinese=false, dismiss_click=false;
std::vector<std::string> lines, hints;
std::vector<bool> highlights;
int click_index=-1, button_index=0, buttons=0, disabled_depth=0;
bool highlight=false;
unsigned checks=0, failures=0;
bool UseChineseUi() { return chinese; }
float SettingsContentWidth() { return 420; }
void SettingsTooltip(const char *text) { hints.emplace_back(text); }
void SettingsNote(const char *text) { lines.emplace_back(text); }
namespace ImGui {
Style style;
const Style &GetStyle() { return style; }
void TextWrapped(const char *format, ...) {
    char buffer[2048]{}; va_list args; va_start(args,format);
    std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
    lines.emplace_back(buffer);
}
void TextDisabled(const char *format, ...) {
    char buffer[2048]{};va_list args;va_start(args,format);
    std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);lines.emplace_back(buffer);
}
bool Button(const char *label) {
    ++buttons;
    return dismiss_click && std::strstr(label,"nr-migration-dismiss");
}
bool Button(const char *, ImVec2) {
    ++buttons; highlights.push_back(highlight);
    // Return a click even when disabled: production must also guard mutations.
    return button_index++ == click_index;
}
void PushID(const char *) {}
void PushID(int) {}
void PopID() {}
void SameLine() {}
void BeginDisabled() { ++disabled_depth; }
void EndDisabled() { --disabled_depth; }
int GetStyleColorVec4(int) { return 1; }
void PushStyleColor(int, int) { highlight=true; }
void PopStyleColor() { highlight=false; }
float GetFrameHeight() { return 22; }
float GetFontSize() { return 16; }
}
#include "nr_settings_notices_ui.inl"
void ResetUi(int click=-1) {
    lines.clear(); hints.clear(); highlights.clear(); dismiss_click=false;
    click_index=click; button_index=buttons=disabled_depth=0; highlight=false;
}
bool Has(const char *part) {
    for(const auto &line:lines) if(line.find(part)!=std::string::npos) return true;
    return false;
}
bool HasHint(const char *part) {
    for(const auto &hint:hints) if(hint.find(part)!=std::string::npos) return true;
    return false;
}
void Check(bool ok, const char *name) {
    ++checks; if(!ok) { ++failures; std::printf("FAIL %s\n",name); }
}
'''


MAIN = r'''
int main() {
    for(bool zh:{false,true}) {
        chinese=zh;
        bool dismissed=false;
        ConfigPolicyMigrationStatus migration{};
        ResetUi(); DrawConfigPolicyMigrationNotice(migration,dismissed);
        Check(lines.empty()&&buttons==0&&!dismissed,"no notice before a migration event");
        migration.visible=true; migration.persisted=true; migration.extra_changed=true;
        ResetUi(); DrawConfigPolicyMigrationNotice(migration,dismissed);
        Check(Has(zh?"已更新并保存":"updated and saved"),"committed migration shows completion");
        Check(Has(zh?"调低模型强度":"Lower Model strength"),"legacy attenuation gives a useful next action");
        Check(!Has(zh?"插件保持关闭":"plugin stays off"),"nonzero migration does not claim plugin disabled");
        Check(!Has(zh?"保存失败":"could not be saved"),"success does not claim persistence failure");
        migration.extra_disabled=true; migration.jitter_changed=true;
        ResetUi(); DrawConfigPolicyMigrationNotice(migration,dismissed);
        Check(Has(zh?"插件保持关闭":"plugin stays off")&&Has("F6"),"legacy zero preserves and explains disabled intent");
        Check(!Has(zh?"旧版低于 1":"below 1 was changed"),"zero uses its specific explanation once");
        Check(Has(zh?"已关闭向 NR 传递 Jitter":"NR jitter forwarding is now off"),"sampling migration is described only when changed");
        migration.persisted=false; migration.failed=true; migration.error=5;
        ResetUi(); DrawConfigPolicyMigrationNotice(migration,dismissed);
        Check(Has(zh?"保存失败":"could not be saved"),"failed persistence is visible");
        Check(Has("F8")&&Has(zh?"目录是否可写":"folder is writable"),"failure includes concrete recovery and feedback");
        Check(!Has(zh?"已更新并保存":"updated and saved"),"failed persistence cannot claim saved");
        migration.failed=false;
        ResetUi(); DrawConfigPolicyMigrationNotice(migration,dismissed);
        Check(Has(zh?"正在保存":"Saving the update"),"pending persistence cannot claim completion");
        Check(!Has(zh?"已更新并保存":"updated and saved"),"pending has no saved claim");
        const auto migration_before=migration;
        ResetUi(); dismiss_click=true; DrawConfigPolicyMigrationNotice(migration,dismissed);
        Check(dismissed,"acknowledgement hides the notice locally");
        Check(std::memcmp(&migration,&migration_before,sizeof(migration))==0,"render and dismissal do not mutate migration state");
        ResetUi(); DrawConfigPolicyMigrationNotice(migration,dismissed);
        Check(lines.empty()&&buttons==0,"acknowledgement survives subsequent draws");

        CompatibilityFallbackSnapshot fallback{};
        fallback.recorded=true;
        ResetUi(); DrawCompatibilityFallbackNotice(fallback);
        Check(lines.empty(),"historical recording without recent fallback stays quiet");
        fallback.recent=true;
        for(unsigned cause:{0u,1u,2u,3u,4u,5u,99u}) {
            fallback.cause=cause; fallback.recorded=false;
            const auto before=fallback;
            ResetUi(); DrawCompatibilityFallbackNotice(fallback);
            Check(Has(zh?"准备中":"preparing"),"attempt is not reported as NR processing");
            Check(!Has(zh?"有处理记录":"recent NR processing recorded"),"attempt has no recording claim");
            Check(HasHint("F8")&&HasHint(zh?"保留":"retained"),"fallback explains retained preference and feedback");
            const char *en[]={"compatibility requirements","image format","colour encoding","8-bit image","HDR image","cannot be read"};
            const char *cn[]={"兼容条件","格式暂不支持","颜色编码","画面的曝光组合","HDR 画面的曝光组合","无法读取"};
            const auto index=cause<=5?cause:0;
            Check(HasHint(zh?cn[index]:en[index]),"cause has a bounded readable explanation including unknown values");
            Check(std::memcmp(&fallback,&before,sizeof(fallback))==0,"notice does not mutate coherent route snapshot");
        }
        fallback.recorded=true;
        ResetUi(); DrawCompatibilityFallbackNotice(fallback);
        Check(Has(zh?"有处理记录":"recent NR processing recorded"),"matching recording has distinct wording");
        Check(!Has(zh?"准备中":"preparing"),"matching recording does not retain attempt wording");

        ChainConfig config;
        ResetUi(); Check(!DrawProcessingModeBlocks(config,false,false,1),"drawing effective post is read only");
        Check(config.mode==2&&highlights==std::vector<bool>({true,false,false}),"fallback preserves requested highlight and reports actual route separately");
        Check(Has("F8"),"fallback retains a concrete diagnostic action");
        ResetUi(1); Check(DrawProcessingModeBlocks(config,false,false,1)&&config.mode==1&&config.processing_start==nr_processing::Start::After,"clicking post saves a different starting preference");
        config.mode=2;config.processing_start=nr_processing::Start::Before;
        const auto before_selection=config.processing_selection;
        ResetUi(0); Check(DrawProcessingModeBlocks(config,false,false,1)&&config.mode==2&&config.processing_selection!=before_selection,"clicking the requested start explicitly retries without changing its saved position");
        config.mode=1;config.processing_start=nr_processing::Start::After;
        ResetUi(0); Check(DrawProcessingModeBlocks(config,false,false,1)&&config.mode==2,"normal pre selection remains editable");
        for(int clicked:{0,1,2}) {
            config={};
            ResetUi(clicked); Check(DrawProcessingModeBlocks(config,true,false,2)&&static_cast<int>(config.processing_start)==clicked,"RR observation leaves all three requested starts editable");
            Check(buttons==3&&disabled_depth==0&&nr_processing_runtime::actual==nr_processing::Start::After,"route click changes preference without fabricating actual execution");
            config={};
            ResetUi(clicked); Check(DrawProcessingModeBlocks(config,false,true,2)&&static_cast<int>(config.processing_start)==clicked,"automatic fallback does not lock explicit route selection");
            Check(config.mode==(clicked==1?1:2),"Present preference does not overwrite the legacy Mode adapter");
        }
    }
    std::printf("Settings UI: %u checks, %u failures; production notices and route UI, CPU recorder only\n",checks,failures);
    return failures?1:0;
}
'''


if __name__ == '__main__':
    unittest.main()

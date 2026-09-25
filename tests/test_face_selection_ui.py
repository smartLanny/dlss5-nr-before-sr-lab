"""Production selection lifecycle/geometry with real Win32 locks and API seams.

The companion ReShade host draws the actual UI. This fixture checks ownership,
deferred registration, cleanup and pixel mapping; it does not simulate clicks.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(text, marker, semicolon=False):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end + int(semicolon)]


HARNESS = r'''
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include "nr_face_capture_geometry.h"
#include "nr_face_diagnostic.h"
struct ImVec2 {float x=0,y=0;ImVec2()=default;ImVec2(float a,float b):x(a),y(b){}};
namespace nr_face_diagnostic_ui {
SELECTION
RECT
VIEWPORT
REGION
MOVE
}
unsigned checks=0,failures=0,registrations=0,unregistrations=0,focus_calls=0;
void check(bool ok,const char *label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
namespace reshade {
enum class addon_event {reshade_overlay};
namespace api {
enum class input_source {mouse};
struct effect_runtime {
    unsigned closes=0,opens=0,blocks=0;
    void block_input_next_frame(){++blocks;}
    bool open_overlay(bool open,input_source){open?++opens:++closes;return true;}
};
}
void (*registered_callback)(api::effect_runtime*)=nullptr;
template<addon_event> void register_event(void (*callback)(api::effect_runtime*)){
    check(!registered_callback,"no duplicate overlay registration");registered_callback=callback;++registrations;
}
template<addon_event> void unregister_event(void (*callback)(api::effect_runtime*)){
    check(registered_callback==callback,"only the matching callback is removed");registered_callback=nullptr;++unregistrations;
}
}
void DrawFaceRegionSelection(reshade::api::effect_runtime*){}
void FocusFaceDiagnosticPage(){++focus_calls;}
STATE
SRWLOCK g_face_selection_lock=SRWLOCK_INIT;
FaceSelectionState g_face_selection;
SNAPSHOT
STORE
QUEUE
FINISH
SERVICE
CLEANUP
int main(){
    reshade::api::effect_runtime a,b;
    nr_face_diagnostic_ui::Selection selected;selected.center_x=.7f;selected.center_y=.3f;
    for(unsigned i=0;i<100;++i)ServiceFaceDiagnosticSelection(&a);
    check(!registrations&&!unregistrations&&!a.opens&&!a.closes,"idle Presents create no overlay work");
    check(!QueueFaceRegionSelection(nullptr,selected),"null runtime cannot own selection");
    const auto before=SnapshotFaceRegionSelection();
    check(QueueFaceRegionSelection(&a,selected),"first selection queued");
    auto current=SnapshotFaceRegionSelection();const auto active_generation=current.generation;
    check(current.owner==&a&&current.requested&&!current.registered&&!registrations,"UI queue never mutates the callback list");
    check(!QueueFaceRegionSelection(&b,selected),"second runtime cannot steal selection");
    ServiceFaceDiagnosticSelection(&b);
    check(!registrations&&!b.closes,"unrelated runtime cannot activate selection");
    ServiceFaceDiagnosticSelection(&a);
    check(registrations==1&&a.closes==1&&a.blocks==1,"owner Present registers once, hides panel and blocks input");
    ServiceFaceDiagnosticSelection(&a);ServiceFaceDiagnosticSelection(&b);
    check(registrations==1&&a.closes==1&&!b.closes,"subsequent Presents do not re-register or close another panel");
    auto stale=selected;stale.center_x=.1f;
    StoreFaceRegionPreference(stale,before.generation);
    check(SnapshotFaceRegionSelection().selection.center_x==.7f,"stale panel write cannot overwrite selection");
    FinishFaceRegionSelection(&b,active_generation,stale,true);
    FinishFaceRegionSelection(&a,active_generation+1,stale,true);
    check(SnapshotFaceRegionSelection().requested,"wrong owner or generation cannot finish selection");
    FinishFaceRegionSelection(&a,active_generation,stale,true);
    current=SnapshotFaceRegionSelection();
    check(!current.requested&&current.registered&&current.restore_panel&&!unregistrations&&!focus_calls,
        "drawing completion defers unregister and reopening until Present");
    ServiceFaceDiagnosticSelection(&b);
    check(unregistrations==1&&!reshade::registered_callback&&!b.opens&&SnapshotFaceRegionSelection().restore_panel,
        "cleanup may retire callback on another Present but preserves target-specific reopening");
    ServiceFaceDiagnosticSelection(&a);ServiceFaceDiagnosticSelection(&a);
    check(a.opens==1&&focus_calls==1&&!SnapshotFaceRegionSelection().restore_panel,
        "owner diagnostic panel reopens exactly once");
    check(QueueFaceRegionSelection(&a,selected),"next selection can begin after cleanup");
    ServiceFaceDiagnosticSelection(&a);current=SnapshotFaceRegionSelection();
    CleanupFaceDiagnosticSelection(&b);
    check(SnapshotFaceRegionSelection().requested&&SnapshotFaceRegionSelection().registered,
        "other runtime destruction cannot cancel owner selection");
    CleanupFaceDiagnosticSelection(&a);
    check(!SnapshotFaceRegionSelection().requested&&!SnapshotFaceRegionSelection().registered&&
        !SnapshotFaceRegionSelection().owner&&unregistrations==2,"owner destruction removes active callback immediately");
    FinishFaceRegionSelection(&a,current.generation,selected,true);ServiceFaceDiagnosticSelection(&a);
    check(a.opens==1&&!SnapshotFaceRegionSelection().restore_panel,"late completion cannot revive destroyed runtime");
    check(QueueFaceRegionSelection(&a,selected),"pending selection can be queued");
    CleanupFaceDiagnosticSelection(&a);ServiceFaceDiagnosticSelection(&a);
    check(registrations==2&&unregistrations==2,"destroying pending selection creates no event registration");
    check(QueueFaceRegionSelection(&a,selected),"final selection queued");ServiceFaceDiagnosticSelection(&a);
    CleanupFaceDiagnosticSelection(nullptr);CleanupFaceDiagnosticSelection(nullptr);
    check(registrations==3&&unregistrations==3&&!reshade::registered_callback,"module cleanup is complete and idempotent");

    nr_face_capture_geometry::View g;g.valid=true;g.source_width=2560;g.source_height=1440;
    g.display_width=g.viewport_width=3840;g.display_height=g.viewport_height=2160;
    auto viewport=nr_face_diagnostic_ui::Viewport(g,{100,50},{2020,1130});
    auto rect=nr_face_diagnostic_ui::Region(g,viewport,.75f,.25f);
    check(rect.valid&&rect.x==1348.f&&rect.y==128.f&&rect.width==384.f&&rect.height==384.f,
        "source pixels map to the actual shifted ImGui viewport");
    rect=nr_face_diagnostic_ui::Region(g,viewport,.5f,.5f);
    const float centre_x=rect.x+rect.width*.5f,centre_y=rect.y+rect.height*.5f;
    g.source_width=1280;g.source_height=720;
    rect=nr_face_diagnostic_ui::Region(g,viewport,.5f,.5f);
    check(rect.width==768.f&&rect.height==768.f&&rect.x+rect.width*.5f==centre_x&&rect.y+rect.height*.5f==centre_y,
        "DRS changes fixed-source-pixel box size without moving the normalized centre");
    nr_face_diagnostic_ui::MoveTo(selected,viewport,{1540,320});
    check(selected.center_x==.75f&&selected.center_y==.25f,"clicks use displayed viewport position and scale");
    nr_face_diagnostic_ui::MoveTo(selected,viewport,{-100,5000});
    check(selected.center_x==0.f&&selected.center_y==1.f,"out-of-view drag remains bounded");
    nr_face_diagnostic_ui::MoveTo(selected,viewport,{std::numeric_limits<float>::quiet_NaN(),40});
    check(selected.center_x==0.f&&selected.center_y==1.f,"invalid cursor observation does not corrupt selection");
    g.valid=false;viewport=nr_face_diagnostic_ui::Viewport(g,{0,0},{1920,1080});
    check(!viewport.valid&&!nr_face_diagnostic_ui::Region(g,viewport,.5f,.5f).valid,
        "unverified source mapping cannot produce a believable preview box");
    std::printf("Face selection UI: %u checks, %u failures; production logic, Win32 locks, API seams, no clicks\n",checks,failures);
    return failures?1:0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'clang++'))
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if os.name != 'nt' or not compiler:
        raise SystemExit('Requires Windows and a C++ compiler')
    selection = (ROOT/'src/nr_face_region_selection.inl').read_text(encoding='utf-8')
    panel = (ROOT/'src/nr_face_diagnostic_ui.inl').read_text(encoding='utf-8')
    service = definition(selection, 'void ServiceFaceDiagnosticSelection(')
    draw = selection[selection.rindex('void DrawFaceRegionSelection('):]
    assert 'register_event<' not in draw and 'unregister_event<' not in draw
    assert service.count('reshade::register_event<') == 1
    assert service.count('reshade::unregister_event<') == 1
    replacements = {
        'SELECTION': definition(panel, 'struct Selection {', True),
        'RECT': definition(panel, 'struct ScreenRect {', True),
        'VIEWPORT': definition(panel, 'ScreenRect Viewport('),
        'REGION': definition(panel, 'ScreenRect Region('),
        'MOVE': definition(panel, 'void MoveTo('),
        'STATE': definition(selection, 'struct FaceSelectionState {', True),
        'SNAPSHOT': definition(selection, 'FaceSelectionState SnapshotFaceRegionSelection('),
        'STORE': definition(selection, 'void StoreFaceRegionPreference('),
        'QUEUE': definition(selection, 'bool QueueFaceRegionSelection('),
        'FINISH': definition(selection, 'void FinishFaceRegionSelection('),
        'SERVICE': service,
        'CLEANUP': definition(selection, 'void CleanupFaceDiagnosticSelection('),
    }
    harness = HARNESS
    for token, value in replacements.items():
        harness = harness.replace('\n'+token+'\n', '\n'+value+'\n')
    with tempfile.TemporaryDirectory(prefix='nr-face-selection-') as temporary:
        directory = Path(temporary)
        source, binary = directory/'test.cpp', directory/'test.exe'
        source.write_text(harness, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-static',
                        '-I', str(ROOT/'src'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == '__main__':
    main()

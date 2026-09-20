#!/usr/bin/env python3
"""Compile and execute the actual generated HDR checkbox with a CPU UI recorder."""
import argparse
import importlib.util
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('colour_overlay', ROOT/'scripts/patch-colour-lab.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)
# Run the real transform against the current settings file before extracting
# its generated block. This fails on missing/duplicate integration anchors.
original = (ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8')
generated = overlay.patch_settings_ui(original)
block = overlay.patch_settings_ui(overlay.LEGACY_HDR_UI)
assert generated.count(block) == 1
source = r'''
#include "nr_colour_lab_policy.h"
#include <cassert>
#include <cstdio>
struct Config { int colour_lab_mode; bool allow_unverified_hdr_color; };
bool disabled=false, shown=false, clicked=false; int notes=0;
namespace ImGui {
void BeginDisabled(bool value){assert(!disabled);disabled=value;}
void EndDisabled(){disabled=false;}
bool Checkbox(const char*,bool* value){shown=*value;if(disabled||!clicked)return false;*value=!*value;return true;}
}
void SettingsTooltip(const char*){}
void SettingsNote(const char*){++notes;}
void Draw(Config& next,bool& changed,bool zh){
const auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
@BLOCK@
}
int main(){
for(int language=0;language<2;++language)for(int mode=-1;mode<=3;++mode)
for(int saved=0;saved<2;++saved)for(int click=0;click<2;++click){
    Config c{mode,saved!=0};bool changed=false;notes=0;clicked=click!=0;
    Draw(c,changed,language!=0);
    assert(shown==nr_colour_lab::AllowUnverified(mode,saved!=0));
    assert(!disabled);
    assert(changed==(mode==0&&click!=0));
    assert(c.allow_unverified_hdr_color==((mode==0&&click)?!saved:saved!=0));
    assert(notes==(mode==0?0:1)); // Always visible, no hover required.
}
std::puts("PASS 40 generated HDR UI transactions: policy display, Legacy-only edit, saved value preservation, visible override note, both languages");
}
'''.replace('@BLOCK@', block)
args.output.mkdir(parents=True, exist_ok=False)
cpp = args.output/'colour_admission_ui.cpp'
exe = args.output/'colour_admission_ui.exe'
cpp.write_text(source, encoding='utf-8')
(args.output/'generated-settings-ui.inl').write_text(generated, encoding='utf-8')
subprocess.run([str(args.compiler), '-std=c++17', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
                str(cpp), '-I'+str(ROOT/'src'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)

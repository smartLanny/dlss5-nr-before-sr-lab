"""Add same-call pre-NR/native-SR O capture to a generated Core only."""
import argparse
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
MODULES=['nr_native_output_capture.h','nr_native_output_capture_state.inl',
         'nr_native_output_capture_export.inl','nr_native_output_capture_runtime.inl']
INPUTS=('scripts/patch-native-output-capture.py','src/nr_stage_capture.inl')+tuple('src/'+n for n in MODULES)

def once(text, old, new):
    if text.count(old)!=1:
        raise ValueError('Native-output capture anchor changed: '+old[:120])
    return text.replace(old,new,1)

def patch(text):
    if 'NR_HAS_NATIVE_OUTPUT_CAPTURE' in text:
        raise ValueError('Native-output capture already patched')
    text=once(text,'#include <windows.h>','#include <windows.h>\n#include "nr_native_output_capture.h"')
    text=once(text,'struct NrStageCapture {','#define NR_HAS_NATIVE_OUTPUT_CAPTURE 1\n#include "nr_native_output_capture_state.inl"\nstruct NrStageCapture {\n    NrNativeOutputCapture native_output{};')
    text=once(text,'#include "nr_source_frame_runtime.inl"','#include "nr_source_frame_runtime.inl"\n#include "nr_native_output_capture_runtime.inl"')
    text=once(text,'    NrRecoveryCommandObserved();\n    Substitution before_sr{};',
              '    NrRecoveryCommandObserved();\n    PrimeNativeOutputCapture(before_state.command_list);\n    Substitution before_sr{};')
    anchor='    NrSlot* timed=before_sr.active?before_sr.timing_slot:after_sr.active?after_sr.slot:nullptr;'
    text=once(text,anchor,anchor+'\n    auto* native_output_capture=ArmNativeOutputCapture(before_state.command_list,params,feature,before_sr.active?before_sr.timing_slot:nullptr);')
    anchor='    const bool used_before_sr = before_sr.active;'
    text=once(text,anchor,'    FinishNativeOutputCapture(before_state.command_list,params,result,native_output_capture);\n'+anchor)
    return text

def apply(stage:Path,inputs,helpers):
    core=stage/'core';source=core/'src/nr_before_sr.cpp'
    result=patch(source.read_text(encoding='utf-8-sig'))
    # Validate all pinned input lookups before any output mutation.
    modules={name:inputs['src/'+name] for name in MODULES+['nr_stage_capture.inl']}
    source.write_text(result,encoding='utf-8',newline='\n')
    for name,data in modules.items():(core/'src'/name).write_bytes(data)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core-root',type=Path,required=True)
    a=p.parse_args();core=a.core_root.resolve(strict=True)
    if core==ROOT: p.error('generated Core only; do not patch source checkout')
    source=core/'src/nr_before_sr.cpp'
    result=patch(source.read_text(encoding='utf-8-sig'))
    modules={name:(ROOT/'src'/name).read_bytes() for name in MODULES}
    modules['nr_stage_capture.inl']=(ROOT/'src/nr_stage_capture.inl').read_bytes()
    source.write_text(result,encoding='utf-8')
    for name,data in modules.items():(core/'src'/name).write_bytes(data)

if __name__=='__main__':main()

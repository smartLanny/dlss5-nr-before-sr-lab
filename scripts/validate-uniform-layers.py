#!/usr/bin/env python3
"""Exercise the actual INI migration and Feature18 tuning consumers on Windows."""
import argparse
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def function(source, signature):
    start=source.index(signature)
    end=source.index('\n}',start)+2
    return source[start:end]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True)
    ap.add_argument('--output',type=Path,default=ROOT/'build/uniform-layers')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    src=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
    fixture=r'''
#define NOMINMAX
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include "nr_layer_config.h"
enum class NrJitterMode { Legacy,GameInputPixel };
struct ChainConfig : nr_layers::Model {
    nr_layers::Auxiliary nr_layers{}; bool nr_uniform_migrated=false;
    unsigned nr_passes=1,nr_full_passes=3,nr_second_numerator=1,nr_second_denominator=2;
    bool nr_full_reference=false; NrJitterMode jitter_mode=NrJitterMode::Legacy;
    unsigned nr_input_filter=0;
};
ChainConfig g_config;
wchar_t g_ini_path[32768]{};
void Log(const char*,...){}
bool WriteIniInt(const wchar_t*,int);
bool WriteIniFloat(const wchar_t*,float);
'''
    for signature in ('float ReadIniFloat(', 'bool WriteIniInt(const wchar_t *key, int value) {',
                      'bool WriteIniFloat(const wchar_t *key, float value) {',
                      'void ReadUniformLayers(', 'bool SaveUniformLayers(',
                      'struct NrTuningSignature {', 'NrTuningSignature CurrentNrTuningSignature(',
                      'bool SameNrTuningSignature('):
        part=function(src,signature)
        fixture+='\n'+part+(';' if signature.startswith('struct ') else '')+'\n'
    fixture+=r'''
unsigned checks=0;
void Check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
int main(){try{
    wchar_t temporary[32768]{};Check(GetTempPathW(32768,temporary)!=0,"temporary directory available");
    Check(GetTempFileNameW(temporary,L"unr",0,g_ini_path)!=0,"isolated INI created");
    ChainConfig old;old.nr_passes=2;old.nr_full_reference=true;old.nr_full_passes=5;
    old.intensity=1.7f;old.local_tone=.37f;old.style=2;ReadUniformLayers(old);
    Check(nr_layers::Active(old.nr_layers)==5&&old.nr_uniform_migrated,"old mixed-size five-layer config migrates explicitly");
    for(unsigned i=1;i<5;++i)Check(nr_layers::Read(old,i)==nr_layers::Primary(old),"old active layers preserve tuned parameters");
    old.nr_layers[0].intensity=.35f;old.nr_layers[1].local_tone=.91f;
    old.nr_layers[2].auto_mask=true;old.nr_layers[2].skin_structure=.75f;
    old.nr_layers[3].style=1;old.nr_layers[0].enabled=false;
    Check(SaveUniformLayers(old),"all new layer records saved");
    ChainConfig loaded;ReadUniformLayers(loaded);
    Check(loaded.nr_layers==old.nr_layers,"all floats and disabled descendants roundtrip exactly");
    Check(nr_layers::Active(loaded.nr_layers)==1,"disabled layer blocks downstream on reload");
    loaded.nr_layers[0].enabled=true;g_config=loaded;
    const auto primary=CurrentNrTuningSignature(0);
    Check(CurrentNrTuningSignature(1).intensity==.35f,"Create consumes layer 2 intensity");
    Check(CurrentNrTuningSignature(2).local_tone==.91f,"Create consumes layer 3 local tone");
    Check(CurrentNrTuningSignature(3).skin_structure==.75f,"Create consumes layer 4 mask and skin");
    Check(CurrentNrTuningSignature(4).style==1,"Create consumes layer 5 style");
    g_config.nr_layers[3].intensity=1.99f;
    Check(SameNrTuningSignature(primary,CurrentNrTuningSignature(0)),"auxiliary edit cannot mutate primary Create signature");
    g_config.nr_layers[2].auto_mask=false;
    Check(CurrentNrTuningSignature(3).skin_structure==-1.f,"disabled model mask uses the actual sentinel");
    Check(g_config.nr_layers[2].skin_structure==.75f,"disabled mask preserves saved skin preference");
    Check(DeleteFileW(g_ini_path)!=0,"isolated INI removed");
    std::cout<<"PASS "<<checks<<" actual INI and Feature18 tuning checks\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(g_ini_path[0])DeleteFileW(g_ini_path);return 1;}}
'''
    path=args.output/'uniform-runtime.cpp';path.write_text(fixture,encoding='utf-8')
    binary=args.output/'uniform-runtime.exe'
    subprocess.run([args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-static',
                    '-I'+str(ROOT/'src'),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

if __name__=='__main__':main()

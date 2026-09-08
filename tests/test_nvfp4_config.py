"""Compile actual configuration and temporal-signature functions, no GPU."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def definition(source, marker, structure=False):
    start = source.index(marker)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end + int(structure)]

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
parts = ['#include <algorithm>\n#include <cmath>\n#include <cstdio>\n#include <cstdlib>\n'
         'enum class NrJitterMode { Legacy, GameInputPixel };\n']
parts += [definition(source, 'struct ChainConfig {', True), 'ChainConfig g_config;']
parts += [definition(source, marker) for marker in (
    'float FiniteOr(', 'bool SameHistoryConfig(', 'void NormalizeConfig(', 'bool SameConfig(')]
parts += [definition(source, 'struct NrTuningSignature {', True)]
parts += [definition(source, marker) for marker in (
    'NrTuningSignature CurrentNrTuningSignature(', 'bool SameNrTuningSignature(')]
parts += [r'''
void Check(bool ok,const char *why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
    ChainConfig base,edit=base;
    Check(!base.nr_precision_mode&&!base.nr_precision_diagnostic&&!base.gpu_timing,"native defaults");
    edit.nr_precision_mode=1;NormalizeConfig(edit);
    Check(edit.nr_precision_mode==1&&!SameConfig(base,edit)&&!SameHistoryConfig(base,edit),"Auto changes configuration and history");
    g_config=base;const auto old=CurrentNrTuningSignature();g_config=edit;
    Check(!SameNrTuningSignature(old,CurrentNrTuningSignature()),"precision switch recreates temporal feature");
    edit=base;edit.nr_precision_diagnostic=true;
    Check(!SameConfig(base,edit)&&!SameHistoryConfig(base,edit),"diagnostic change is a fresh history experiment");
    g_config=edit;Check(!SameNrTuningSignature(old,CurrentNrTuningSignature()),"diagnostic change updates Create signature");
    edit=base;edit.gpu_timing=true;
    Check(!SameConfig(base,edit)&&SameHistoryConfig(base,edit),"timing toggle does not rebuild model history");
    edit.nr_precision_mode=2;NormalizeConfig(edit);Check(edit.nr_precision_mode==0,"unknown precision falls back to Off");
    edit.nr_precision_mode=~0u;NormalizeConfig(edit);Check(edit.nr_precision_mode==0,"negative/overflow INI precision falls back to Off");
    std::puts("PASS production precision defaults, normalization, config and Create/history signatures");
}
''']
with tempfile.TemporaryDirectory(prefix='nvfp4-config-') as tmp:
    path = Path(tmp)
    fixture, binary = path / 'config.cpp', path / 'config.exe'
    fixture.write_text('\n'.join(parts), encoding='utf-8')
    subprocess.run([args.compiler, '-std=c++20', '-O2', '-static', '-Wall', '-Wextra', '-Werror', str(fixture), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

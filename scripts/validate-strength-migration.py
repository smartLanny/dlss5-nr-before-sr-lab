"""Run the actual ReloadConfig INI prefix against real Win32 profile files."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def definition(text, marker, semicolon=False):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end + int(semicolon)]

p = argparse.ArgumentParser()
p.add_argument('--compiler', required=True)
p.add_argument('--source', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
s = (a.source or root / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
reload = definition(s, 'void ReloadConfig(')
prefix = reload[reload.index('    ChainConfig next = {};'):reload.index('    next.max_luma_ratio =')]
helpers = ''.join(definition(s, marker) for marker in [
    'float ReadIniFloat(', 'int MigrateConfigMode(',
    'bool WriteIniInt(const wchar_t *key, int value) {',
    'bool WriteIniFloat(const wchar_t *key, float value) {'])
if 'float ReadConfiguredTransferStrength(' in s:
    helpers += definition(s, 'float ReadConfiguredTransferStrength(')
harness = r'''
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iterator>
enum class NrJitterMode : int { Legacy=0, GameInputPixel=1 };
CONFIG
wchar_t g_ini_path[MAX_PATH]{};
std::atomic<unsigned long> g_config_persistence_error{0};
std::atomic<bool> g_config_persistence_failed{false};
template<class... A> void Log(const char*, A...) {}
HELPERS
ChainConfig Load() { PREFIX return next; }
int failures=0, checks=0;
void check(bool ok,const char* label) { ++checks; if(!ok) { ++failures; std::printf("FAIL: %s\n",label); } }
void put(const wchar_t* key,const wchar_t* value) { check(WritePrivateProfileStringW(L"NRBeforeSR",key,value,g_ini_path)!=0,"fixture write"); }
void fixture(int version,int mode,const wchar_t* pre,const wchar_t* post,const wchar_t* strength_version=nullptr) {
    DeleteFileW(g_ini_path); put(L"ConfigVersion",version==4?L"4":L"0");
    put(L"Mode",mode==1?L"1":L"2");
    if(pre) put(L"TransferStrength",pre); if(post) put(L"PostTransferStrength",post);
    if(strength_version) put(L"StrengthConfigVersion",strength_version);
}
int main() {
    wchar_t dir[MAX_PATH]{}; GetTempPathW(MAX_PATH,dir);
    GetTempFileNameW(dir,L"nrm",0,g_ini_path);
    for(int version : {0,4}) for(int mode : {1,2}) {
        fixture(version,mode,mode==2?L"0":L"1",mode==1?L"0":L"1");
        auto c=Load(); check(c.transfer_strength==0,"legacy active mode preserves zero");
        check(c.post_transfer_strength==0,"canonical mirror preserves zero");
        check(c.mode==(version<4?2:mode),"mode migration remains independent");
        check(GetPrivateProfileIntW(L"NRBeforeSR",L"StrengthConfigVersion",0,g_ini_path)==1,"independent marker persisted");
        c=Load(); check(c.transfer_strength==0,"restart retains migrated zero after mode migration");
    }
    fixture(4,1,L"0",L"1",L"1"); check(Load().transfer_strength==0,"canonical zero wins over stale post field");
    fixture(4,1,L"1.6",L"0.3",L"1"); check(Load().transfer_strength==1.6f,"canonical wins regardless of mode");
    fixture(4,1,L"1.6",L"0.3"); check(Load().transfer_strength==.3f,"existing version3 post value is legacy");
    fixture(4,2,L"0.3",L"1.6"); check(Load().transfer_strength==.3f,"existing version3 pre value is legacy");
    fixture(4,1,L"0",nullptr); check(Load().transfer_strength==0,"missing active field falls back to existing valid zero");
    fixture(4,2,L"NaN",L"0"); check(Load().transfer_strength==0,"invalid active field falls back to valid zero");
    fixture(4,2,nullptr,nullptr); check(Load().transfer_strength==1.2f,"new installation default strength");
    check(Load().color_strength==.5f,"missing colour uses new installation default");
    put(L"ColorStrength",L"0"); check(Load().color_strength==0,"saved zero colour is preserved");
    DeleteFileW(g_ini_path);
    std::printf("actual Win32 INI strength migration: %d checks / %d failures\n",checks,failures);
    return failures?1:0;
}
'''.replace('CONFIG', definition(s, 'struct ChainConfig', True)).replace('HELPERS', helpers).replace('PREFIX', prefix)
with tempfile.TemporaryDirectory(prefix='nr-strength-ini-') as td:
    cpp, exe = Path(td)/'migration.cpp', Path(td)/'migration.exe'
    cpp.write_text(harness,encoding='utf-8')
    subprocess.run([a.compiler,'-std=c++20','-O0','-static',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

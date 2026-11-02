"""Production network settings and layer identity on a CPU host.

The storage adapter is an in-memory key store, not Win32 persistence or a GPU.
Read/write helpers, actual ChainConfig/comparisons and the picture reset execute
production code. No NVIDIA network adoption or game acceptance is inferred.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker):
    start = source.index(marker)
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor]


class NetworkSettings(unittest.TestCase):
    def test_production_persistence_layer_identity_and_resets(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.skipTest('A C++20 host compiler is required')
        core = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        source = r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "nr_layer_config.h"
#include "nr_processing_route.h"
#include "nr_colour_restore_policy.h"
#include "nr_colour_preferences.h"
#include "nr_unified_ui_policy.h"
enum class NrJitterMode : int {Legacy=0, GameInputPixel=1};
CHAIN_CONFIG
SAME_CONFIG
SAME_HISTORY
#include "nr_recommended_picture.inl"

using Storage=std::map<std::wstring,std::wstring>;
void load(ChainConfig& config,const Storage& storage) {
    nr_network_settings::Load(config,[&](const wchar_t* key,int fallback) {
        const auto found=storage.find(key);
        return found==storage.end()?fallback:nr_network_settings::Parse(found->second.c_str(),fallback);
    });
}
void save(const ChainConfig& config,Storage& storage) {
    nr_network_settings::Save(config,[&](const wchar_t* key,int value) {storage[key]=std::to_wstring(value);});
}
int main() {
    using namespace nr_network_policy;
    using nr_network_settings::Parse;
    using nr_network_settings::InvalidText;
    assert(Parse(L"",RuntimeDefault)==RuntimeDefault);
    assert(Parse(L" \t",Inherit)==Inherit);
    assert(Parse(L" +2 ",Inherit)==Optimized);
    assert(Parse(L"-1",0)==Inherit);
    assert(Parse(L"99",0)==99);
    assert(Parse(L"-2",0)==-2);
    for(const auto* text:{L"2x",L"1.5",L"4294967296",L"-2147483649",L"999999999999999999999999"})
        assert(Parse(text,0)==InvalidText);
    assert(Parse(L"2147483647",0)==2147483647);

    // Missing fields never reinterpret the historical NRPreset or Style.
    Storage old{{L"NRPreset",L"2"},{L"Style",L"2"},{L"OtherUserKey",L"keep"}};
    ChainConfig fresh;fresh.style=2;fresh.intensity=.71f;load(fresh,old);
    assert(fresh.nr_network_preset==RuntimeDefault&&fresh.nr_primary_network_preset==Inherit);
    for(unsigned i=0;i<nr_layers::Count;++i)assert(nr_layers::RequestedNetwork(fresh,i)==RuntimeDefault);
    assert(fresh.style==2&&fresh.intensity==.71f);

    // All five layer requests, including inactive/future/invalid IDs, roundtrip.
    ChainConfig tuned=fresh;tuned.nr_network_preset=Original;tuned.nr_primary_network_preset=Optimized;
    const int overrides[]={Inherit,RuntimeDefault,Original,99};
    for(unsigned i=0;i<4;++i)tuned.nr_layers[i].network_preset=overrides[i];
    nr_unified_ui::SetLayers(tuned,5);
    const auto before_disable=tuned;
    nr_unified_ui::SetLayers(tuned,1);save(tuned,old);
    ChainConfig reloaded;load(reloaded,old);
    assert(reloaded.nr_network_preset==Original&&reloaded.nr_primary_network_preset==Optimized);
    for(unsigned i=0;i<4;++i)assert(reloaded.nr_layers[i].network_preset==overrides[i]);
    assert(old.at(L"OtherUserKey")==L"keep"&&old.at(L"Style")==L"2");
    nr_unified_ui::SetLayers(tuned,5);
    assert(tuned.nr_layers==before_disable.nr_layers);
    auto invalid_model=nr_layers::Read(tuned,4);nr_layers::Normalize(invalid_model);
    assert(invalid_model.network_preset==99);
    old[L"Layer3NetworkPreset"]=L"2trailing";load(reloaded,old);save(reloaded,old);
    assert(reloaded.nr_layers[1].network_preset==InvalidText);
    assert(old.at(L"Layer3NetworkPreset")==std::to_wstring(InvalidText));

    // A storage failure is observed at the failing key without changing the
    // live configuration; subsequent records are still passed to the caller.
    const auto unchanged=tuned;std::vector<std::wstring> attempted;std::wstring first_failure;
    nr_network_settings::Save(tuned,[&](const wchar_t* key,int) {
        attempted.emplace_back(key);
        if(std::wstring(key)==L"Layer3NetworkPreset"&&first_failure.empty())first_failure=key;
    });
    assert(attempted.size()==6&&attempted.back()==L"Layer5NetworkPreset");
    assert(first_failure==L"Layer3NetworkPreset"&&SameConfig(tuned,unchanged));

    ChainConfig base;nr_unified_ui::SetLayers(base,5);base.nr_network_preset=Original;
    // Saving inherit as the same explicit network changes intent, not tuning.
    auto changed=base;changed.nr_primary_network_preset=Original;
    assert(!SameConfig(base,changed)&&SameHistoryConfig(base,changed));
    assert(nr_layers::FirstChanged(base,changed)==nr_layers::Count);
    changed.nr_layers[2].network_preset=Original;
    assert(nr_layers::FirstChanged(base,changed)==nr_layers::Count);
    // Changing global affects only layers actually following it.
    base.nr_primary_network_preset=Original;base.nr_layers[0].network_preset=Original;
    changed=base;changed.nr_network_preset=Optimized;
    assert(nr_layers::FirstChanged(base,changed)==2&&SameHistoryConfig(base,changed));
    for(auto& layer:base.nr_layers)layer.network_preset=Original;
    changed=base;changed.nr_network_preset=Optimized;
    assert(!SameConfig(base,changed)&&nr_layers::FirstChanged(base,changed)==nr_layers::Count);
    // Every layer is independently selectable; Style is never a network alias.
    for(unsigned layer=0;layer<nr_layers::Count;++layer) {
        changed=base;
        if(layer)changed.nr_layers[layer-1].network_preset=Optimized;
        else changed.nr_primary_network_preset=Optimized;
        assert(nr_layers::FirstChanged(base,changed)==layer);
        for(unsigned other=0;other<nr_layers::Count;++other) {
            assert(nr_layers::RequestedNetwork(changed,other)==(other==layer?Optimized:Original));
            assert(nr_layers::Read(changed,other).style==nr_layers::Read(base,other).style);
        }
    }
    // Output-only colour edits do not alter any model network or layer epoch.
    changed=base;changed.color_strength=.2f;changed.transfer_strength=2;
    assert(nr_layers::FirstChanged(base,changed)==nr_layers::Count&&SameHistoryConfig(base,changed));

    // A local reset follows the global request without altering another layer.
    changed=base;changed.nr_network_preset=Optimized;
    const auto global=changed.nr_network_preset;
    static_cast<nr_layers::Model&>(changed.nr_layers[2])={};
    assert(changed.nr_network_preset==global&&nr_layers::RequestedNetwork(changed,3)==Optimized);
    assert(changed.nr_primary_network_preset==Original&&changed.nr_layers[1].network_preset==Original);
    // Explicit picture restore resets all network overrides while preserving
    // the master switch, shortcut and compatibility preferences.
    changed.enabled=true;changed.master_virtual_key=120;changed.compat_percent=53;
    RestoreRecommendedPictureSettings(changed);
    assert(changed.nr_network_preset==RuntimeDefault&&changed.nr_primary_network_preset==Inherit);
    for(unsigned i=0;i<nr_layers::Count;++i)assert(nr_layers::Read(changed,i).network_preset==Inherit);
    assert(changed.enabled&&changed.master_virtual_key==120&&changed.compat_percent==53);
    std::cout<<"PASS network parsing, persistence, all-layer identity, equivalent requests, disabled-layer retention and actual picture reset\n";
}
'''
        source = source.replace('CHAIN_CONFIG', definition(core, 'struct ChainConfig {') + ';')
        source = source.replace('SAME_CONFIG', definition(core, 'bool SameConfig('))
        source = source.replace('SAME_HISTORY', definition(core, 'bool SameHistoryConfig('))
        with tempfile.TemporaryDirectory(prefix='nr-network-settings-') as folder:
            cpp = Path(folder) / 'network.cpp'
            exe = Path(folder) / ('network.exe' if os.name == 'nt' else 'network')
            cpp.write_text(source, encoding='utf-8')
            compiled = subprocess.run([compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                                       '-I', str(ROOT / 'src'), str(cpp), '-o', str(exe)],
                                      capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('PASS network parsing', ran.stdout)

    def test_actual_loader_save_and_feedback_wiring(self):
        core = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        self.assertLess(core.index('    ReadUniformLayers(next);'), core.index('    nr_network_settings::Load(next,'))
        self.assertIn('nr_network_settings::Parse(value,fallback)', core)
        self.assertIn('length>=std::size(value)-1?nr_network_settings::InvalidText:', core)
        saved = definition(core, 'bool SaveConfig(')
        self.assertIn('nr_network_settings::Save(config,', saved)
        self.assertIn('record_write(WriteIniInt(key,value));', saved)
        feedback = (ROOT / 'src/nr_feedback_picture.inl').read_text(encoding='utf-8')
        for field in ('requested.network.global', 'networkOverride', 'networkResolved'):
            self.assertIn(field, feedback)
        self.assertIn('requested values are not proof of executed network', feedback)
        ini = (ROOT / 'config/nr_before_sr.ini').read_text(encoding='utf-8-sig')
        self.assertIn('NRNetworkPreset=0', ini)
        for layer in range(1, 6):
            self.assertIn(f'Layer{layer}NetworkPreset=-1', ini)


if __name__ == '__main__':
    unittest.main()

"""Actual CarrierDeviceOf with the pinned ReShade unwrapped-object contract."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tests.native_bridge.test_carrier_observer_failure import function


@unittest.skipUnless(os.environ.get('CARRIER_COMPLETION_SOURCE') and os.environ.get('CXX'),
                     'requires generated source and Windows compiler')
class DeviceIdentity(unittest.TestCase):
    def test_raw_proxy_foreign_and_missing_device(self):
        source = (Path(os.environ['CARRIER_COMPLETION_SOURCE']) /
                  'carrier/src/dlss5-bridge.cpp').read_text(encoding='utf-8')
        actual = function(source, 'static bool CarrierDeviceOf(')
        harness = r'''
#include <windows.h>
#include <cstdint>
#include <cstdio>
struct ID3D11Device {
    unsigned refs = 1;
    ID3D11Device *original = nullptr;
    unsigned AddRef() { return ++refs; }
    unsigned Release() { return --refs; }
    HRESULT QueryInterface(const GUID &iid, void **out) {
        static const GUID contract = {0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
        *out = nullptr;
        if (!IsEqualGUID(iid, contract) || !original) return E_NOINTERFACE;
        original->AddRef(); *out = original; return S_OK;
    }
};
struct ID3D11DeviceContext {
    ID3D11Device *device;
    void GetDevice(ID3D11Device **out) { *out = device; if (device) device->AddRef(); }
};
// ACTUAL
int main() {
    ID3D11Device raw, foreign, proxy; proxy.original = &raw;
    ID3D11DeviceContext contexts[] = {{&raw},{&proxy},{&foreign},{nullptr}};
    std::uintptr_t identity = 0;
    for (unsigned i=0;i<4;++i) {
        bool ok = CarrierDeviceOf(&contexts[i], &identity);
        if (ok != (i!=3)) return 1;
        if (i<2 && identity != reinterpret_cast<std::uintptr_t>(&raw)) return 2;
        if (i==2 && identity != reinterpret_cast<std::uintptr_t>(&foreign)) return 3;
        if (raw.refs!=1 || proxy.refs!=1 || foreign.refs!=1) return 4;
    }
    if (CarrierDeviceOf(nullptr,&identity) || CarrierDeviceOf(&contexts[0],nullptr)) return 5;
    std::puts("PASS actual device identity: raw, ReShade proxy, unrelated and missing; balanced COM references");
}
'''.replace('// ACTUAL', actual)
        with tempfile.TemporaryDirectory(prefix='carrier-identity-') as tmp:
            cpp, exe = Path(tmp)/'identity.cpp', Path(tmp)/'identity.exe'
            cpp.write_text(harness, encoding='utf-8')
            subprocess.run([os.environ['CXX'],'-std=c++17','-O2',str(cpp),'-o',str(exe)],check=True)
            result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main(verbosity=2)

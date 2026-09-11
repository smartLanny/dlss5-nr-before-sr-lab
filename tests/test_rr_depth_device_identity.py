"""CPU contract test for the RR depth D3D12 device identity seam.

The test extracts the two production helpers from ``nr_rr_depth_adapter.inl``
and runs them against a small COM double.  It does not create a D3D12 device,
call WARP, or execute the RR shader.  The double models the public ReShade
``IID_UnwrappedObject`` contract, COM's canonical ``IID_IUnknown`` fallback,
HRESULT failures, and reference ownership so a pointer-only regression cannot
silently pass.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ADAPTER = ROOT / "src" / "nr_rr_depth_adapter.inl"


def definition(source: str, marker: str) -> str:
    """Extract one balanced C++ function definition beginning at *marker*."""

    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def compiler() -> Path:
    configured = os.environ.get("RR_DEPTH_TEST_CXX") or os.environ.get("CXX")
    candidates: list[str] = []
    if configured:
        candidates.append(configured)
    toolchain = os.environ.get("DLSS5_LLVM_MINGW_ROOT")
    if toolchain:
        candidates.append(
            str(Path(toolchain) / "bin" / "x86_64-w64-mingw32-clang++.exe")
        )
    candidates.extend(("clang++", "g++", "c++"))
    for candidate in candidates:
        path = Path(candidate)
        resolved = path if path.is_file() else shutil.which(candidate)
        if resolved:
            return Path(resolved).resolve()
    raise unittest.SkipTest(
        "No C++ compiler found; set RR_DEPTH_TEST_CXX to clang++ or g++."
    )


HARNESS = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

using HRESULT = std::int32_t;
using ULONG = unsigned long;

constexpr HRESULT S_OK = 0;
constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
constexpr HRESULT E_NOINTERFACE = static_cast<HRESULT>(0x80004002u);
constexpr HRESULT E_POINTER = static_cast<HRESULT>(0x80004003u);
constexpr bool SUCCEEDED(HRESULT hr) { return hr >= 0; }
constexpr bool FAILED(HRESULT hr) { return hr < 0; }

struct GUID {
    std::uint32_t Data1;
    std::uint16_t Data2;
    std::uint16_t Data3;
    std::uint8_t Data4[8];
};

constexpr GUID IID_IUnknown = {
    0x00000000u, 0x0000u, 0x0000u,
    {0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u}};

constexpr bool operator==(const GUID &left, const GUID &right) {
    if (left.Data1 != right.Data1 || left.Data2 != right.Data2 ||
        left.Data3 != right.Data3)
        return false;
    for (unsigned i = 0; i != 8; ++i)
        if (left.Data4[i] != right.Data4[i]) return false;
    return true;
}

struct FakeDevice;
struct IUnknown;
using QueryInterfaceFn = HRESULT (*)(IUnknown *, const GUID &, void **);
using AddRefFn = ULONG (*)(IUnknown *);
using ReleaseFn = ULONG (*)(IUnknown *);

struct IUnknownVtbl {
    QueryInterfaceFn QueryInterface;
    AddRefFn AddRef;
    ReleaseFn Release;
};

struct IUnknown {
    const IUnknownVtbl *lpVtbl;
    FakeDevice *owner;
};

// The production helper only requires the COM identity surface.  Treat the
// D3D12 device pointer as the first interface of this fake object, just as a
// real ID3D12Device pointer can be passed to IUnknown::QueryInterface.
using ID3D12Device = IUnknown;

struct FakeDevice {
    IUnknown iface{};
    FakeDevice *unwrapped = nullptr;
    FakeDevice *canonical = nullptr;
    HRESULT unwrapped_hr = E_NOINTERFACE;
    HRESULT canonical_hr = S_OK;
    bool unwrapped_returns_null = false;
    bool canonical_returns_null = false;
    unsigned refs = 1;
    unsigned add_refs = 0;
    unsigned releases = 0;
    unsigned queries = 0;
};

static const GUID kIidUnwrappedObject = {
    0x7f2c9a11u, 0x3b4eu, 0x4d6au,
    {0x81u, 0x2fu, 0x5eu, 0x9cu, 0xd3u, 0x7au, 0x1bu, 0x42u}};

ULONG AddRefObject(IUnknown *object) {
    ++object->owner->add_refs;
    return ++object->owner->refs;
}

ULONG ReleaseObject(IUnknown *object) {
    FakeDevice &device = *object->owner;
    ++device.releases;
    if (device.refs == 0) return 0;
    return --device.refs;
}

HRESULT QueryObject(IUnknown *object, const GUID &iid, void **out) {
    FakeDevice &device = *object->owner;
    ++device.queries;
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == kIidUnwrappedObject) {
        if (device.unwrapped_hr != S_OK) return device.unwrapped_hr;
        if (device.unwrapped_returns_null) return S_OK;
        if (!device.unwrapped) return E_NOINTERFACE;
        device.unwrapped->iface.lpVtbl->AddRef(&device.unwrapped->iface);
        *out = &device.unwrapped->iface;
        return S_OK;
    }
    if (iid == IID_IUnknown) {
        if (device.canonical_hr != S_OK) return device.canonical_hr;
        if (device.canonical_returns_null) return S_OK;
        FakeDevice *identity = device.canonical ? device.canonical : &device;
        identity->iface.lpVtbl->AddRef(&identity->iface);
        *out = &identity->iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

const IUnknownVtbl kFakeVtbl = {QueryObject, AddRefObject, ReleaseObject};

void Init(FakeDevice &device) {
    device.iface.lpVtbl = &kFakeVtbl;
    device.iface.owner = &device;
    device.unwrapped = nullptr;
    device.canonical = &device;
    device.unwrapped_hr = E_NOINTERFACE;
    device.canonical_hr = S_OK;
    device.unwrapped_returns_null = false;
    device.canonical_returns_null = false;
    device.refs = 1;
    device.add_refs = 0;
    device.releases = 0;
    device.queries = 0;
}

HRESULT IUnknown_QueryInterface(IUnknown *object, const GUID &iid, void **out) {
    return object && object->lpVtbl && object->lpVtbl->QueryInterface
        ? object->lpVtbl->QueryInterface(object, iid, out)
        : E_POINTER;
}

template <typename T>
void ReleaseCom(T *&object) {
    if (!object) return;
    object->lpVtbl->Release(object);
    object = nullptr;
}

std::atomic<unsigned long long> g_rr_depth_proxy_matches{0};

@GUID@
@RESOLVE@
@SAME@

unsigned failures = 0;

void Check(bool condition, const char *label) {
    if (!condition) {
        std::printf("FAIL %s\n", label);
        ++failures;
    }
}

void CheckBalanced(const char *label, const FakeDevice *const *devices,
                   unsigned count) {
    for (unsigned i = 0; i != count; ++i) {
        const FakeDevice &device = *devices[i];
        Check(device.refs == 1, label);
        Check(device.add_refs == device.releases, label);
    }
}

bool LegacyPointerSame(ID3D12Device *first, ID3D12Device *second) {
    return first == second;
}

void CheckSame(const char *label, FakeDevice &first, FakeDevice &second,
               bool expected) {
    const FakeDevice *devices[] = {&first, &second};
    const bool actual = IsSameRrDepthDevice(&first.iface, &second.iface);
    Check(actual == expected, label);
    CheckBalanced(label, devices, 2);
}

int main() {
    Check(kRrDepthUnwrappedObject == kIidUnwrappedObject,
          "production uses pinned ReShade IID_UnwrappedObject");

    FakeDevice raw_a, raw_b, raw_c, proxy_a, proxy_b, alias;
    Init(raw_a); Init(raw_b); Init(raw_c); Init(proxy_a); Init(proxy_b); Init(alias);

    // Raw identity: equal pointers are the cheap fast path; different raw
    // devices must remain different after canonical IUnknown resolution.
    CheckSame("raw same", raw_a, raw_a, true);
    CheckSame("raw different", raw_a, raw_b, false);

    // ReShade proxy -> native device.  This is the dev12 counterexample to the
    // old source_device == command_device test: the pointers differ but their
    // unwrapped canonical identity is the same.
    proxy_a.unwrapped = &raw_a;
    proxy_a.unwrapped_hr = S_OK;
    CheckSame("proxy and raw same device", proxy_a, raw_a, true);
    Check(!LegacyPointerSame(&proxy_a.iface, &raw_a.iface),
          "old pointer-only guard rejects proxy/raw pair");

    proxy_b.unwrapped = &raw_b;
    proxy_b.unwrapped_hr = S_OK;
    CheckSame("proxy and raw different device", proxy_a, raw_b, false);
    CheckSame("second proxy different device", proxy_a, proxy_b, false);

    // Two independent ReShade proxies both unwrap to one native device.
    proxy_b.unwrapped = &raw_a;
    CheckSame("two proxies share raw device", proxy_a, proxy_b, true);
    Check(g_rr_depth_proxy_matches.load() >= 2,
          "proxy match diagnostic counts successful unwrapped matches");

    // IID_UnwrappedObject is optional.  E_NOINTERFACE must fall back to the
    // canonical IUnknown identity, including a legal interface-pointer alias.
    FakeDevice alias_proxy_a, alias_proxy_b;
    Init(alias_proxy_a); Init(alias_proxy_b);
    alias_proxy_a.canonical = &alias;
    alias_proxy_b.canonical = &alias;
    alias_proxy_a.unwrapped_hr = E_NOINTERFACE;
    alias_proxy_b.unwrapped_hr = E_NOINTERFACE;
    CheckSame("non-ReShade IUnknown alias same", alias_proxy_a, alias_proxy_b, true);
    Check(!LegacyPointerSame(&alias_proxy_a.iface, &alias_proxy_b.iface),
          "IUnknown alias is not pointer identity");

    alias_proxy_b.canonical = &raw_c;
    CheckSame("IUnknown alias different", alias_proxy_a, alias_proxy_b, false);

    // Any HRESULT other than E_NOINTERFACE is an unproven identity and must
    // reject; S_OK with a null output is equally invalid.
    proxy_a.unwrapped = &raw_a;
    proxy_a.unwrapped_hr = E_FAIL;
    CheckSame("unwrapped QI non-E_NOINTERFACE failure", proxy_a, raw_a, false);
    proxy_a.unwrapped_hr = S_OK;
    proxy_a.unwrapped_returns_null = true;
    CheckSame("unwrapped QI S_OK null", proxy_a, raw_a, false);
    proxy_a.unwrapped_returns_null = false;

    // A canonical IUnknown query failure cannot be treated as a device match.
    proxy_a.unwrapped_hr = S_OK;
    raw_a.canonical_hr = E_FAIL;
    CheckSame("canonical IUnknown failure", proxy_a, raw_a, false);
    raw_a.canonical_hr = S_OK;
    raw_a.canonical_returns_null = true;
    CheckSame("canonical IUnknown S_OK null", proxy_a, raw_a, false);
    raw_a.canonical_returns_null = false;

    // Nulls are never an identity, and all successful QIs above must have
    // released their returned interfaces before the helper returns.
    Check(!IsSameRrDepthDevice(nullptr, &raw_a.iface), "null first rejected");
    Check(!IsSameRrDepthDevice(&raw_a.iface, nullptr), "null second rejected");
    Check(!IsSameRrDepthDevice(nullptr, nullptr), "two nulls rejected");
    const FakeDevice *all[] = {&raw_a, &raw_b, &raw_c, &proxy_a, &proxy_b,
                               &alias, &alias_proxy_a, &alias_proxy_b};
    CheckBalanced("all COM references balanced", all,
                  static_cast<unsigned>(sizeof(all) / sizeof(all[0])));

    std::printf("RR depth device identity: %s (%u failures)\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
'''


class RrDepthDeviceIdentity(unittest.TestCase):
    def test_production_identity_helpers_with_com_double(self) -> None:
        source = ADAPTER.read_text(encoding="utf-8-sig")
        try:
            guid = source[source.index("constexpr GUID kRrDepthUnwrappedObject"):
                           source.index(";", source.index("constexpr GUID kRrDepthUnwrappedObject")) + 1]
            resolve = definition(source, "bool ResolveRrDepthDeviceIdentity(")
            same = definition(source, "bool IsSameRrDepthDevice(")
        except ValueError as error:
            self.fail(f"Required production device identity seam missing: {error}")

        harness = HARNESS.replace("@GUID@", guid)
        harness = harness.replace("@RESOLVE@", resolve)
        harness = harness.replace("@SAME@", same)
        cxx = compiler()
        with tempfile.TemporaryDirectory(prefix="rr-depth-device-identity-") as temporary:
            temp = Path(temporary)
            cpp = temp / "device_identity.cpp"
            exe = temp / ("device_identity.exe" if os.name == "nt" else "device_identity")
            cpp.write_text(harness, encoding="utf-8")
            command = [
                str(cxx),
                "-std=c++20",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(cpp),
                "-o",
                str(exe),
            ]
            subprocess.run(command, cwd=ROOT, check=True, timeout=60)
            completed = subprocess.run(
                [str(exe)],
                cwd=ROOT,
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            if completed.stdout:
                print(completed.stdout, end="")
            if completed.stderr:
                print(completed.stderr, end="")
            self.assertEqual(completed.returncode, 0,
                             completed.stdout + completed.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)

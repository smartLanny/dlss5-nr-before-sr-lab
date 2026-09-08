"""Regression checks for carrier registry admission against the real header."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("CXX"), "requires the LLVM Windows compiler in CXX")
class CarrierRegistryAdmission(unittest.TestCase):
    def test_real_header_at_o0_and_o2(self):
        root = Path(__file__).resolve().parents[2]
        harness = r'''
#include "src/interop/nr_carrier_activation_runtime.h"

#include <cstdint>
#include <cstdio>

using namespace nr_carrier_activation;

int main() {
    unsigned failures = 0;
#define CHECK(condition) do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

    constexpr std::uintptr_t device = 0x11;
    constexpr std::uintptr_t foreign_device = 0x22;
    constexpr std::uintptr_t swapchain = 0x33;
    constexpr std::uintptr_t handle = 0x44;

    // Empty device slots are not a registered device identity.
    Registry empty;
    CHECK(!empty.AddD3d11Device(0));
    CHECK(!empty.AddSwapchain(swapchain, 0, false));
    CHECK(empty.Created(handle, 0, 7, true) == 0);
    CHECK(!empty.Allows(handle, 0));

    // A snapshot is invalid while its same-device swapchain is resizing or gone.
    Registry lifecycle;
    CHECK(lifecycle.AddD3d11Device(device));
    CHECK(lifecycle.AddSwapchain(swapchain, device, false));
    CHECK(lifecycle.Created(handle, device, 7, true) != 0);
    FeatureRecord snapshot{};
    CHECK(lifecycle.Allows(handle, device, &snapshot));
    CHECK(lifecycle.StillCurrent(snapshot));
    CHECK(!lifecycle.RemoveSwapchain(swapchain, device, true));
    CHECK(!lifecycle.StillCurrent(snapshot));
    CHECK(!lifecycle.Allows(handle, device));
    CHECK(lifecycle.AddSwapchain(swapchain, device, true));
    CHECK(lifecycle.Allows(handle, device));
    CHECK(lifecycle.RemoveSwapchain(swapchain, device, false));
    CHECK(!lifecycle.StillCurrent(snapshot));
    snapshot.flags_known = false;
    CHECK(!lifecycle.StillCurrent(snapshot));

    // A failed Create on an unregistered device retires an old same-address record.
    Registry reused_address;
    CHECK(reused_address.AddD3d11Device(device));
    CHECK(reused_address.AddSwapchain(swapchain, device, false));
    CHECK(reused_address.Created(handle, device, 9, true) != 0);
    FeatureRecord old{};
    CHECK(reused_address.Allows(handle, device, &old));
    CHECK(reused_address.Created(handle, foreign_device, 11, true) == 0);
    CHECK(!reused_address.StillCurrent(old));
    CHECK(!reused_address.Allows(handle, device));

#undef CHECK
    if (failures != 0) {
        std::fprintf(stderr, "%u registry admission checks failed\n", failures);
        return 1;
    }
    std::puts("carrier registry admission: passed");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="carrier-registry-admission-") as tmp:
            tmp_path = Path(tmp)
            source = tmp_path / "registry_admission.cpp"
            source.write_text(harness, encoding="utf-8")
            failures = []
            for optimization in ("-O0", "-O2"):
                exe = tmp_path / ("registry_admission" + optimization[1:] + ".exe")
                subprocess.run(
                    [os.environ["CXX"], "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     optimization, "-I", str(root), str(source), "-o", str(exe)],
                    check=True,
                    capture_output=True,
                    text=True,
                )
                result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
                if result.returncode != 0 or "carrier registry admission: passed" not in result.stdout:
                    failures.append(f"{optimization}: {result.stdout}{result.stderr}")
            if failures:
                self.fail("\n".join(failures))


if __name__ == "__main__":
    unittest.main(verbosity=2)

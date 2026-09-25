"""Compile actual RR production includes against portable API test doubles.

This is a control-flow/F8 regression, not a Windows add-on build or GPU test.
Run: python -m unittest discover -s tests/multi_api -p test_rr_post_diagnostics.py -v
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RrPostDiagnosticsTest(unittest.TestCase):
    def test_production_boundaries_and_f8(self):
        # CXX is an executable name/path, without embedded flags.
        requested = os.environ.get("CXX")
        compiler = shutil.which(requested) if requested else (
            shutil.which("clang++") or shutil.which("g++")
        )
        if not compiler:
            self.skipTest("clang++/g++ unavailable; portable RR test was not run")
        with tempfile.TemporaryDirectory(prefix="nr-rr-diagnostic-") as directory:
            source = ROOT / "tests/multi_api/rr_post_diagnostics_harness.cpp"
            # The feedback include is extracted from the current production
            # boundary. Keep this CPU fixture's new observability symbols
            # explicit here; they are recording stubs, never runtime logic.
            stubs = r'''
#include <atomic>
std::atomic<bool> g_compatibility_post_observed{false};
bool IsCompatibilityPostObserved() { return g_compatibility_post_observed.load(); }
bool IsRayReconstructionRouteObserved() { return false; }
int EffectivePictureMode(int requested_mode) {
    return IsRayReconstructionRouteObserved() ||
        (requested_mode==2 && IsCompatibilityPostObserved()) ? 1 : requested_mode;
}
std::atomic<unsigned long long> g_compat_fallback_attempts{0}, g_compat_fallback_recorded{0};
std::atomic<int> g_compat_fallback_reason{0};
std::atomic<unsigned> g_compat_fallback_cause{0};
std::atomic<unsigned long long> g_native_command_records{0}, g_native_command_retirements{0}, g_native_command_record_failures{0};
std::atomic<unsigned long long> g_rr_depth_conversions{0}, g_rr_depth_declines{0};
'''
            fixture = Path(directory) / "rr_post_diagnostics_fixture.cpp"
            source_text = source.read_text(encoding="utf-8").replace(
                "../../src/", (ROOT / "src").as_posix() + "/")
            # RR now shares the real sticky processing Session. Keep that
            # implementation intact and supply only its config/source inputs;
            # each existing case is an independent explicit route selection.
            stubs += '\n#include <cmath>\n#include "' + (ROOT / "src/nr_processing_route.h").as_posix() + '"\n'
            stubs += '#include "' + (ROOT / "src/nr_fg_presentation.h").as_posix() + '"\n'
            stubs += 'nr_fg_presentation::Policy g_fg_presentation_policy;\n'
            stubs += 'namespace nr_compat_present_runtime { bool InlineDrainReady() { return true; } bool NativeNrDrained() { return true; } }\n'
            config = 'struct ChainConfig { bool enabled=true, identity_use_original=false, color_protection=true; float post_transfer_strength=1; int mode=2; };'
            self.assertEqual(source_text.count(config), 1)
            source_text = source_text.replace(config, config[:-2] + '''
    float transfer_strength=1;
    nr_processing::Start processing_start=nr_processing::Start::Before;
    unsigned long long processing_selection=1;
};''')
            rr_include = '#include "' + (ROOT / "src/interop/nr_rr_post_runtime.inl").as_posix() + '"'
            self.assertEqual(source_text.count(rr_include), 1)
            source_text = source_text.replace(rr_include, '''
namespace nr_source_runtime { unsigned long long CurrentRetryToken() { return 1; } }
#include "''' + (ROOT / "src/nr_processing_runtime.inl").as_posix() + '"\n' + rr_include)
            reset = 'g_config={}; g_config.mode=requested_mode;'
            self.assertEqual(source_text.count(reset), 1)
            source_text = source_text.replace(reset, reset + '''
            g_config.processing_start=nr_processing::FromLegacy(requested_mode);
            g_config.processing_selection=nr_processing_runtime::next_selection.fetch_add(1);
''')
            fixture.write_text(stubs + source_text, encoding="utf-8")
            for optimization in ("-O0", "-O2"):
                suffix = ".exe" if os.name == "nt" else ""
                exe = Path(directory) / ("rr-test-" + optimization[1:] + suffix)
                command = [compiler, "-std=c++17", optimization, "-Wall", "-Wextra", "-Werror"]
                # The pinned Windows GNU toolchain is intentionally not installed
                # into PATH; keep the portable fixture self-contained there.
                if os.name == "nt":
                    command.append("-static")
                result = subprocess.run(
                    command + [str(fixture), "-o", str(exe)],
                    capture_output=True, text=True, timeout=60,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run(
                    [str(exe)], capture_output=True, text=True, timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("PASS 52", result.stdout)


if __name__ == "__main__":
    unittest.main()

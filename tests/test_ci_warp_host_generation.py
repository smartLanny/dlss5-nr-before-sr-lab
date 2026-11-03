"""Exercise real WARP host generators up to their native compiler boundary.

These are source-assembly tests, not Windows compilation or GPU evidence.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = ("DLSS5_TEST_WARP_DLL", "DLSS5_TEST_WARP_SHA256",
               "DLSS5_TEST_WARP_EVIDENCE_DIR")


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CompilerBoundary(Exception):
    pass


class HostGeneration(unittest.TestCase):
    def setUp(self):
        self.environment = patch.dict(os.environ, {key: value for key, value in os.environ.items()
                                                   if key not in ENVIRONMENT}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def temporal(self, directory, hardware):
        module = load("validate-temporal-quality-sequence")
        output = directory / "result"
        output.mkdir()
        cache = directory / "sdk-cache"
        cache.mkdir()
        (cache / "build.json").write_text('{"sdk_objects": []}', encoding="utf-8")
        args = SimpleNamespace(hardware=hardware, core=None, sdk_cache=cache,
                               sdk=directory / "not-used-sdk", compiler="not-invoked",
                               build_only=True)
        receipt = {"commands": []}
        with patch.object(module, "run", side_effect=CompilerBoundary) as compiler:
            with self.assertRaises(CompilerBoundary):
                module.quality_run(args, output, receipt)
        self.assertEqual(compiler.call_count, 1)
        self.assertEqual(compiler.call_args.args[2], "compile")
        self.assertFalse(receipt["passed"])
        self.assertEqual(receipt["phase"], "ready-to-compile")
        self.assertFalse((output / "quality.exe").exists())
        return output, receipt

    def test_actual_temporal_warp_host_freezes_and_identifies_helper(self):
        original = (ROOT / "experiments/final_residual/amd_of_gpu_probe.cpp").read_bytes()
        with tempfile.TemporaryDirectory() as temporary:
            output, receipt = self.temporal(Path(temporary), False)
            frozen = output / "source"
            helper = (frozen / "ci_warp_runtime.h").read_bytes()
            self.assertEqual(helper, (ROOT / "tests/ci_warp_runtime.h").read_bytes())
            self.assertEqual(receipt["warp_runtime_helper_sha256"], hashlib.sha256(helper).hexdigest())
            self.assertEqual(receipt["source_origin"]["ci_warp_runtime.h"], "repository-test-runtime")
            host = (frozen / "amd_of_gpu_probe.cpp").read_text(encoding="utf-8")
            self.assertEqual(host.count('#include "ci_warp_runtime.h"'), 1)
            anchors = ("nr_test_warp::Prepare()", "CreateDXGIFactory1(",
                       "factory->EnumWarpAdapter(", "D3D12CreateDevice(software.Get()",
                       'nr_test_warp::Verify("temporal-quality")')
            indices = [host.index(anchor) for anchor in anchors]
            self.assertEqual(indices, sorted(indices))
            self.assertNotIn("for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> adapter;", host)
            self.assertEqual(json.loads((output / "result.json").read_text())["phase"], "ready-to-compile")
        self.assertEqual((ROOT / "experiments/final_residual/amd_of_gpu_probe.cpp").read_bytes(), original)

    def test_unconfigured_hardware_host_remains_original(self):
        with tempfile.TemporaryDirectory() as temporary:
            output, receipt = self.temporal(Path(temporary), True)
            self.assertEqual((output / "source/amd_of_gpu_probe.cpp").read_bytes(),
                             (ROOT / "experiments/final_residual/amd_of_gpu_probe.cpp").read_bytes())
            self.assertFalse((output / "source/ci_warp_runtime.h").exists())
            self.assertIsNone(receipt["warp_runtime_helper_sha256"])

    def test_any_configured_variable_rejects_hardware_before_generation(self):
        module = load("validate-temporal-quality-sequence")
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "must-not-be-created"
            for name in ENVIRONMENT:
                for value in ("", "configured"):
                    with self.subTest(name=name, value=value), patch.dict(os.environ, {name: value}):
                        with self.assertRaisesRegex(ValueError, "Pinned CI WARP forbids --hardware"):
                            module.quality_run(SimpleNamespace(hardware=True), output, {})
                        self.assertFalse(output.exists())

    def test_actual_source_transport_freezes_helper_beside_generated_host(self):
        module = load("validate-source-transport")
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "transport"
            arguments = ["validate-source-transport", "--compiler", "not-invoked", "--output", str(output)]
            with patch("sys.argv", arguments), patch.object(module.subprocess, "run", side_effect=CompilerBoundary) as compiler:
                with self.assertRaises(CompilerBoundary):
                    module.main()
            self.assertEqual(compiler.call_count, 1)
            self.assertEqual((output / "ci_warp_runtime.h").read_bytes(),
                             (ROOT / "tests/ci_warp_runtime.h").read_bytes())
            host = (output / "source_transport_warp.cpp").read_text(encoding="utf-8")
            self.assertEqual(host.count('#include "ci_warp_runtime.h"'), 1)
            self.assertIn('nr_test_warp::Verify("source-transport")', host)
            self.assertNotIn("// @ACTUAL_", host)
            self.assertFalse((output / "source_transport_warp.exe").exists())


if __name__ == "__main__":
    unittest.main()

import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("bridge_045", ROOT / "scripts/prepare-native-bridge-045.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Source045(unittest.TestCase):
    def setUp(self):
        # The canonical integration explicitly adapts the historical library
        # to reviewed fix2 source; retain its old standalone pins unchanged.
        version = patch.object(m, 'VERSION', 'beta0.4.5-fix2')
        version.start()
        self.addCleanup(version.stop)

    def test_unique_block_boundaries(self):
        self.assertEqual(m.between("aSTARTxSTOPb", "START", "STOP", "Y"), "aYSTOPb")
        for text in ("none", "STARTSTARTSTOP", "STOPSTART"):
            with self.assertRaises(ValueError): m.between(text, "START", "STOP", "Y")

    def test_real_current_core(self):
        core = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8")
        result = m.patch_core(core)
        self.assertIn(m.EXPERIMENT, result)
        self.assertNotIn('"' + m.VERSION + '"', result)
        self.assertEqual(result.count('    NrDx11DeploymentPoll(false);'), 1)
        self.assertEqual(result.count('    NrRenderControlPoll();'), 1)
        self.assertIn('interop/nr_dx11_deployment.inl', result)
        with self.assertRaises(ValueError): m.patch_core(result)

    def test_real_current_ui(self):
        text = (ROOT / "src/nr_settings_ui.inl").read_text(encoding="utf-8")
        result = m.patch_ui(text)
        self.assertEqual(result.count("DrawRenderControl();"), 1)
        self.assertEqual(result.count("DrawDx11DeploymentStatus();"), 1)
        self.assertIn('ImGui::BeginChild("nr-live-status"', result)
        self.assertIn("RestoreRecommendedPictureSettings", result)
        self.assertIn("maximum_strength = 4.0f", result)
        self.assertNotIn("Switch the game to Direct3D 12", result)
        self.assertLess(result.index("const bool collecting = nr_f8::IsActive()"), result.index("DrawRenderControl();"))

    def test_build_and_protected_state(self):
        build = (ROOT / "scripts/build.ps1").read_text(encoding="utf-8")
        patched = m.patch_build(build)
        self.assertEqual(patched.count("-" + m.EXPERIMENT + "-"), 2)
        self.assertIn("$SourceDirty = '1'", patched)
        self.assertEqual(m.legacy.CORE_BLOBS["src/nr_command_state.inl"], "6bedfcc809cac7e600658085048a190c55279850")
        code = (ROOT / "scripts/prepare-native-bridge-045.py").read_text(encoding="utf-8")
        self.assertNotIn("ExperimentalNullPsoCarrier=1", code)
        self.assertNotIn("ngx_loader=0", code)
        self.assertIn('"vendor_loader_modified": False', code)

    def test_no_duplicate_ngx_hook_in_inventory(self):
        text = (ROOT / "src/interop/nr_dx11_deployment.inl").read_text(encoding="utf-8")
        for forbidden in ("MH_CreateHook", "LoadLibrary", "GetProcAddress", "SetOptions"):
            self.assertNotIn(forbidden, text)
        self.assertIn("next.complete = GetLastError() == ERROR_NO_MORE_FILES", text)
        self.assertIn("duplicate_carriers", text)

    @unittest.skipUnless(os.environ.get("PINNED_CARRIER_ROOT"), "full pinned source is required, not a fabricated snippet")
    def test_actual_pinned_carrier_patch(self):
        source = Path(os.environ["PINNED_CARRIER_ROOT"]) / "src/bridge.inc"
        text = source.read_text(encoding="utf-8")
        # Validate the Git blob using raw bytes (not CRLF-normalized text).
        import subprocess
        raw = subprocess.check_output(["git", "-C", str(source.parents[1]), "show", "HEAD:src/bridge.inc"])
        self.assertEqual(m.legacy.blob(raw), m.legacy.UPSTREAM_BLOBS["src/bridge.inc"])
        result = m.patch_bridge(text)
        self.assertEqual(result.count("const auto nr_frame_plan = nr_mirror_rect::Resolve"), 1)
        self.assertEqual(result.count("nr_mirror_rect::CopyOutput<ID3D11DeviceContext"), 1)
        self.assertIn("gw = nr_frame_plan.render.w; gh = nr_frame_plan.render.h;", result)
        self.assertIn("const bool partial = nr_frame_plan.partial_output", result)
        self.assertIn("c.unwrap = 0; c.unwrap_list = 0; c.ngx_loader = 1", result)
        self.assertIn("if (!g_bridge.mv_converted) ctx->CopyResource", result)
        self.assertIn("nr_bridge_baseline::Bind(list)", result)
        # Resource/sync functions are not replaced by this geometry experiment.
        for marker in ("static UINT64 EndCommands()", "static bool BridgeDeviceLost()", "static void BridgeRescueStalledQueue()"):
            self.assertEqual(result.count(marker), text.count(marker))
        with self.assertRaises(ValueError): m.patch_bridge(result)

if __name__ == "__main__":
    unittest.main()

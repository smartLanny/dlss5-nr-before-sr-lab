import importlib.util
import pathlib
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('r3_prepare', ROOT/'scripts/prepare-native-bridge-r3.py')
r3=importlib.util.module_from_spec(spec);spec.loader.exec_module(r3)
class R3Tests(unittest.TestCase):
    def test_actual_r3_core_and_ui(self):
        source=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        result=r3.patch_core(source)
        self.assertIn('NrRenderControlPoll();',result)
        self.assertIn('interop/nr_render_control.inl',result)
        self.assertNotIn('"beta0.4.3r3"',result)
        with self.assertRaises(ValueError): r3.patch_core(result)
        ui=r3.patch_ui((ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8'))
        self.assertEqual(ui.count('DrawRenderControl();'),1)
        self.assertLess(ui.index('const bool collecting = nr_f8::IsActive()'),ui.index('DrawRenderControl();'))
    def test_guard_not_weakened(self):
        script=(ROOT/'scripts/prepare-native-bridge-r3.py').read_text(encoding='utf-8')
        self.assertIn('"src/nr_command_state.inl": "922f0cff',script)
        self.assertNotIn('ExperimentalNullPsoCarrier=1',script)
        baseline=(ROOT/'src/interop/nr_bridge_baseline.h').read_text()
        self.assertIn('list->SetPipelineState(pso)',baseline)
        self.assertIn('list->SetComputeRootSignature(root)',baseline)
        self.assertNotIn('->Dispatch(',baseline)
        self.assertNotIn('Unwrap',baseline)
    def test_no_eval_extent_override(self):
        policy=(ROOT/'src/interop/nr_render_scale_policy.h').read_text()
        self.assertNotIn('Set("Width"',policy)
        self.assertNotIn('Set("Height"',policy)
        self.assertNotIn('Set("DLSS.Render.Subrect',policy)
        self.assertIn('p.Set("OutWidth"',policy)
        self.assertIn('p.Set("OutHeight"',policy)
    def test_runtime_gate_and_original(self):
        code=(ROOT/'src/interop/nr_render_control.inl').read_text(encoding='utf-8')
        self.assertIn('const auto result = original(p);',code)
        self.assertIn('result != kNgxSuccess',code)
        self.assertIn('!nr_f8::IsActive()',code)
        self.assertIn('g_live_rr_handles.load',code)
        self.assertNotIn('MH_ALL_HOOKS',code)
        self.assertNotIn('LoadLibrary',code)
        self.assertIn('before == request.load',code)
if __name__=='__main__':unittest.main()

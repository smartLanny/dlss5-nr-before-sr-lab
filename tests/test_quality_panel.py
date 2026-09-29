"""Tests for quality1 decisions and the real exporter command routing.
These are host/source tests; no Windows, ImGui rendering or GPU claim.
"""
from pathlib import Path
import ast
import importlib.util
import tempfile
import types
import unittest
ROOT=Path(__file__).resolve().parents[1]

def load(path):
    spec=importlib.util.spec_from_file_location('quality_edge_patch',path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

class QualityTests(unittest.TestCase):
    def test_existing_preferences_not_silently_migrated(self):
        policy=(ROOT/'src/nr_colour_restore_policy.h').read_text(encoding='utf-8')
        self.assertIn('kDefaultMode = 0',policy)
        self.assertIn('value == 1 || value == 2 ? value : 0',policy)
        loader=(ROOT/'scripts/patch-colour-restore.py').read_text(encoding='utf-8')
        self.assertIn('L"ColourRestoreMode", nr_colour_restore::kDefaultMode',loader)
        self.assertNotIn('nr_quality_recipe::Select',loader)
    def test_ui_warnings_visible_while_experiments_collapsed(self):
        s=(ROOT/'src/nr_quality_ui.inl').read_text(encoding='utf-8')
        cut=s.index('ImGui::CollapsingHeader(')
        for term in ('RGB recovery is active: owner tests reported added contour halos', 'ZERO-EDIT TEST ACTIVE', 'N2 NOT applied', 'The 99% / 100% colour difference remains unresolved'):
            self.assertLess(s.index(term),cut)
        for term in ('next.colour_restore_mode = recovery;', 'next.colour_edge_recovery = edge;', 'next.colour_neutral_probe = neutralProbe;'):
            self.assertGreater(s.index(term),cut)
        for forbidden in ('ApplyConfig(', 'WritePrivateProfile', 'g_config.', 'CreateFeature', 'EvaluateFeature'):
            self.assertNotIn(forbidden,s)
        self.assertIn('sample.config_generation == generation',s)
    def test_recipes_touch_only_existing_six_selectors(self):
        s=(ROOT/'src/nr_quality_recipe.h').read_text()
        import re
        written=set(re.findall(r'c\.(\w+)\s*=(?!=)',s))
        self.assertEqual(written,{'colour_lab_mode','colour_restore_mode','colour_input_curve','colour_scaled_proxy_mode','colour_edge_recovery','colour_neutral_probe'})
    def test_previous_ui_required_and_secondary_group_preserved(self):
        patch=load(ROOT/'scripts/patch-colour-edge.py')
        def once(s,a,b,label):
            if s.count(a)!=1:raise ValueError(label)
            return s.replace(a,b,1)
        helpers=types.SimpleNamespace(once=once)
        with self.assertRaises(ValueError):patch.patch_ui('wrong version',helpers,'new-ui')
        old='void DrawColourLabControls( next.colour_neutral_probe sample.policy.recorded_neutral_probe RequestStageCapture()'
        self.assertEqual(patch.patch_ui(old,helpers,'new-ui'),'new-ui')
        anchor='    DrawColourLabControls(next, generation, has_pending, changed, apply_discrete_edit);'
        source=anchor+'\n// all original artistic controls remain\n'
        result=patch.patch_light_group(source,helpers)
        self.assertTrue(result.endswith('// all original artistic controls remain\n'))
        self.assertNotIn(anchor,result) # The only recipe entry now lives on the common picture page.
        patch_source=(ROOT/'scripts/patch-colour-edge.py').read_text(encoding='utf-8')
        self.assertIn('DrawColourLabControls(next, current.generation, has_pending, changed, apply_discrete_edit);',patch_source)
        self.assertLess(result.index('next.light_preview != 0u'),result.index('CollapsingHeader('))
        self.assertIn('quality-preview-off', result)
    def test_real_exporter_routing_and_old_negative_control(self):
        path=ROOT/'scripts/prepare-colour4-edge.py'
        tree=ast.parse(path.read_text(encoding='utf-8'))
        outer=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='prepare')
        check=next(n for n in outer.body if isinstance(n,ast.FunctionDef) and n.name=='check')
        calls=[];ns={'sys':types.SimpleNamespace(executable='python'),'subprocess':types.SimpleNamespace(run=lambda cmd,**kw:calls.append(cmd)),'core':ROOT}
        exec(compile(ast.Module(body=[check],type_ignores=[]),str(path),'exec'),ns)
        ns['check']('test_colour_stage.py',Path('/tmp/complete-stage'))
        self.assertNotIn('--repo',calls[-1]);self.assertIn('--generated',calls[-1])
        ns['check']('test_colour_restore.py',Path('/tmp/complete-stage'))
        self.assertIn('--repo',calls[-1]);self.assertIn('--generated',calls[-1])
        # The actual inherited parser rejects the old combined invocation.
        old=ast.parse((ROOT/'tests/test_colour_stage.py').read_text(encoding='utf-8'))
        main=next(n for n in old.body if isinstance(n,ast.If) and '__name__' in ast.unparse(n.test))
        prefix=[]
        for n in main.body:
            if isinstance(n,ast.Assign) and 'parse_known_args' in ast.unparse(n):break
            prefix.append(n)
        env={'argparse':__import__('argparse') ,'Path':Path}
        exec(compile(ast.Module(body=prefix,type_ignores=[]),'actual_stage_parser','exec'),env)
        parser=env['p']
        with self.assertRaises(SystemExit):parser.parse_known_args(['--repo','x','--generated','y'])
    def test_input_manifest_and_syntax(self):
        p=load(ROOT/'scripts/patch-colour-edge.py')
        for path in ('src/nr_quality_recipe.h','src/nr_quality_ui.inl'):self.assertIn(path,p.INPUTS)
        gen=(ROOT/'scripts/prepare-colour4-edge.py').read_text()
        self.assertIn("'default_restore_mode':0",gen)
        for path in ('scripts/patch-colour-edge.py','scripts/prepare-colour4-edge.py'):
            compile((ROOT/path).read_text(),path,'exec')

if __name__=='__main__':unittest.main()

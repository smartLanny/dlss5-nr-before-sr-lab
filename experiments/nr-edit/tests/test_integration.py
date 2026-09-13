#!/usr/bin/env python3
"""Checks against real generated product sources, not a tiny mirrored fixture."""
from pathlib import Path
import argparse,importlib.util,json,sys,tempfile,unittest
PACKAGE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PACKAGE/'tools'))
import stage_edit_integration as stage
parser=argparse.ArgumentParser();parser.add_argument('--generated-root',type=Path,required=True)
a,rest=parser.parse_known_args();ROOT=a.generated_root.resolve(strict=True)
class Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.core=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
  cls.build=(ROOT/'scripts/build.ps1').read_text(encoding='utf-8-sig')
  cls.patched=stage.patch_core(cls.core)
 def test_version_is_not_replaced_by_old_d16(self):
  import re
  for name in ['kAddonVersion']:
   pattern=r'constexpr const char \*'+name+r' = "([^"\n]+)";'
   self.assertEqual(re.search(pattern,self.core)[1],re.search(pattern,self.patched)[1])
  self.assertIn('-edit-v4";',self.patched)
 def test_current_local_controls_survive(self):
  for name in ['descriptor_low_overhead','present_mv_compensation']:
   self.assertEqual(self.core.count(name),self.patched.count(name))
 def test_all_private_hook_bypasses_exist(self):
  self.assertEqual(self.patched.count('if(nr_edit_core::PrivateSrCall())return '),10)
 def test_existing_nested_powershell_params_survive(self):
  b=stage.patch_build(self.build,'a'*40)
  self.assertEqual(b.count('[switch]$SkipUnchangedNrChain'),1)
  self.assertEqual(b.count('function New-NrSourceDefinesResponse'),1)
  self.assertIn("-replace '\\.addon64$', '-edit-v4.addon64'",b)
 def test_duplicate_overlay_rejected(self):
  with self.assertRaises(ValueError):stage.patch_core(self.patched)
 def test_ambiguous_source_anchor_rejected(self):
  with self.assertRaises(ValueError):stage.patch_core(self.core+'\nnamespace {\n}\n')
 def test_invalid_sha_does_not_create_candidate(self):
  with tempfile.TemporaryDirectory() as t:
   out=Path(t)/'out'
   with self.assertRaises(ValueError):stage.stage(ROOT,out,'not-a-commit')
   self.assertFalse(out.exists())
 def test_safe_stage_preserves_all_unrelated_files(self):
  with tempfile.TemporaryDirectory() as t:
   out=Path(t)/'out';m=stage.stage(ROOT,out,'a'*40)
   self.assertEqual(set(m['modified_files']),{'src/nr_before_sr.cpp','src/nr_settings_ui.inl','src/nr_processing_runtime.inl','scripts/build.ps1','src/interop/nr_rr_post_runtime.inl'})
   self.assertGreater(len(m['preserved_files']),180)
   for rel in m['preserved_files']:self.assertEqual(stage.digest(ROOT/rel),stage.digest(out/rel),rel)
   original=(out/'edit-integration-manifest.json').read_bytes()
   with self.assertRaises(FileExistsError):stage.stage(ROOT,out,'a'*40)
   self.assertEqual(original,(out/'edit-integration-manifest.json').read_bytes())
if __name__=='__main__':unittest.main(argv=[sys.argv[0],*rest])

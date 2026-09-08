"""CPU-only installation/restore fixtures executed by Windows PowerShell 5.1."""
from __future__ import annotations
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
INSTALL = ROOT / 'scripts/install-nvfp4-preview.ps1'
PACKAGE = ROOT / 'scripts/package-nvfp4-preview.py'
PS51 = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
# The Python process may inherit PowerShell 7's module search path from Codex.
# A clean PS5.1 launch must resolve its own built-in Utility cmdlets.
PS51_ENV = {k:v for k,v in os.environ.items() if k.lower() != 'psmodulepath'}
OLD_ZH = 'DLSS5-AI渲染超分版-beta0.5-dev14-旧版本.addon64'
OLD_EN = 'DLSS5-AI-Rendering-Upscaling-beta0.5-dev14-test.en.addon64'
NEW_ZH = 'DLSS5-AI渲染超分版-beta0.5-nvfp4-dev1-源码预览.addon64'
OTHER = '另一个独立工具.addon64'

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write(path: Path, value: bytes | str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(value if isinstance(value, bytes) else value.encode('utf-8'))
    return path

class InstallFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nvfp4-install-cpu-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root/'中文 游戏 [测试]'; self.game.mkdir()
        self.package = self.root/'预览 包'; self.payload=self.package/'core-preview'
        self.original = {
            OLD_ZH:b'ORIGINAL Chinese Core\0\x80', OLD_EN:b'ORIGINAL English Core',
            'nrchain_nvngx.dll':b'ORIGINAL bridge\0\xff',
            'nr_before_sr.ini':'[个人配置]\nModel=1.2\nShortcut=等号\n'.encode('utf-8'),
            OTHER:b'UNRELATED add-on must survive',
        }
        for name,data in self.original.items(): write(self.game/name,data)
        write(self.payload/NEW_ZH,b'NEW inert fake Core; never load this file')
        write(self.payload/'nrchain_nvngx.dll',b'NEW inert fake bridge')
        write(self.payload/'nr_before_sr.ini',b'[NR]\nPrecision=Off\n')
        self.manifest = {'schema':1,'source_commit':'a'*40,'default_precision':'original FP8','auto_qualified':False,
                         'files':{f'core-preview/{p.name}':digest(p) for p in self.payload.iterdir()}}
        write(self.package/'package-manifest.json',json.dumps(self.manifest,ensure_ascii=False))

    def run_helper(self,restore=False):
        argv=[str(PS51),'-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(INSTALL),
              '-GameDirectory',str(self.game),'-PackageDirectory',str(self.package)]
        if restore:argv+=['-Restore']
        return subprocess.run(argv,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30,env=PS51_ENV)

    def assert_ok(self,result):
        self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr)

    def assert_original(self):
        for name,data in self.original.items():self.assertEqual((self.game/name).read_bytes(),data,name)

    def test_ps51_chinese_regex_restore_and_personal_config(self):
        version=subprocess.check_output([str(PS51),'-NoProfile','-Command','$PSVersionTable.PSVersion.ToString()'],text=True,env=PS51_ENV).strip()
        self.assertTrue(version.startswith('5.1.'),version)
        self.assert_ok(self.run_helper())
        self.assertFalse((self.game/OLD_ZH).exists(),'Old Chinese Core was not matched/removed by the PS5.1 regex')
        self.assertFalse((self.game/OLD_EN).exists())
        self.assertEqual((self.game/OTHER).read_bytes(),self.original[OTHER])
        self.assertEqual((self.game/'nr_before_sr.ini').read_bytes(),self.original['nr_before_sr.ini'])
        self.assertEqual((self.game/NEW_ZH).read_bytes(),(self.payload/NEW_ZH).read_bytes())
        self.assert_ok(self.run_helper(restore=True))
        self.assert_original();self.assertFalse((self.game/NEW_ZH).exists())
        self.assertFalse((self.game/'.nr-nvfp4-preview-install.json').exists())

    def test_new_install_configuration_removed_on_restore(self):
        (self.game/'nr_before_sr.ini').unlink();del self.original['nr_before_sr.ini']
        self.assert_ok(self.run_helper())
        self.assertEqual((self.game/'nr_before_sr.ini').read_bytes(),(self.payload/'nr_before_sr.ini').read_bytes())
        self.assert_ok(self.run_helper(restore=True));self.assert_original()
        self.assertFalse((self.game/'nr_before_sr.ini').exists())

    def test_changed_bridge_refuses_restore_before_any_mutation(self):
        self.assert_ok(self.run_helper())
        write(self.game/'nrchain_nvngx.dll',b'LATER USER UPDATE')
        before={p.name:p.read_bytes() for p in self.game.iterdir() if p.is_file()}
        result=self.run_helper(restore=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('later update',result.stdout.lower()+result.stderr.lower())
        self.assertEqual(before,{p.name:p.read_bytes() for p in self.game.iterdir() if p.is_file()})

    def test_tampered_package_refuses_before_core_move(self):
        write(self.payload/NEW_ZH,b'CHANGED after manifest')
        result=self.run_helper();self.assertNotEqual(result.returncode,0)
        self.assertIn('hash mismatch',result.stdout.lower()+result.stderr.lower())
        self.assert_original();self.assertFalse((self.game/NEW_ZH).exists())
        self.assertFalse((self.game/'.nr-nvfp4-preview-install.json').exists())

    def test_backup_corruption_refuses_restore(self):
        self.assert_ok(self.run_helper())
        receipt=json.loads((self.game/'.nr-nvfp4-preview-install.json').read_text(encoding='utf-8-sig'))
        entry=next(e for e in receipt['entries'] if Path(e['target']).name=='nrchain_nvngx.dll')
        write(Path(entry['backup']),b'CORRUPTED BACKUP')
        result=self.run_helper(restore=True);self.assertNotEqual(result.returncode,0)
        self.assertIn('backup hash mismatch',result.stdout.lower()+result.stderr.lower())
        self.assertTrue((self.game/'.nr-nvfp4-preview-install.json').exists())
        self.assertTrue((self.game/NEW_ZH).exists())

    def test_missing_backup_refuses_false_restoration(self):
        self.assert_ok(self.run_helper())
        receipt=json.loads((self.game/'.nr-nvfp4-preview-install.json').read_text(encoding='utf-8-sig'))
        entry=next(e for e in receipt['entries'] if Path(e['target']).name=='nrchain_nvngx.dll')
        Path(entry['backup']).unlink()
        result=self.run_helper(restore=True)
        self.assertNotEqual(result.returncode,0,'Restore must not claim success while the original bridge is unrecoverable')
        self.assertTrue((self.game/'.nr-nvfp4-preview-install.json').exists())

    def test_second_install_requires_restore(self):
        self.assert_ok(self.run_helper())
        result=self.run_helper();self.assertNotEqual(result.returncode,0)
        self.assertIn('receipt already exists',result.stdout.lower()+result.stderr.lower())

if __name__=='__main__':unittest.main(verbosity=2)

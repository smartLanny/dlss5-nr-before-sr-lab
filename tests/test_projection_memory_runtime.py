"""Actual Streamline adapter boundaries and on-demand Windows DXGI accounting."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_rr_depth_device_identity import compiler

ROOT=Path(__file__).resolve().parents[1]

class RuntimeBoundaries(unittest.TestCase):
    def test_actual_runtime_contracts(self):
        if os.name!='nt':
            self.skipTest('Windows contracts require Windows')
        with tempfile.TemporaryDirectory(prefix='projection-memory-') as tmp:
            for name in ('rr_projection_source','nr_fg_streamline_test','process_memory'):
                with self.subTest(name=name):
                    exe=Path(tmp)/(name+'.exe')
                    subprocess.run([str(compiler()),'-std=c++17','-O2','-static','-Wall','-Wextra','-Werror',
                        str(ROOT/'tests'/f'{name}.cpp'),'-o',str(exe),'-ldxgi','-ldxguid'],check=True)
                    subprocess.run([str(exe)],check=True)

if __name__=='__main__':unittest.main()

"""Negative controls on real geometry helpers, not shader/GPU execution."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = (root / 'geometry.hpp').read_text()
cases = [
    ('missing-x-scale', 'unit * float(g.source.width) / float(g.flow.width)', 'unit', 'pixel unit X restored'),
    ('wrong-half-rounding', 'source.width / 2 + source.width % 2', 'source.width / 2', 'actual flow width'),
    ('wrong-nv-units', '1.0f / 32.0f : 1.0f', '1.0f : 1.0f', 'pixel unit X restored'),
]
with tempfile.TemporaryDirectory(prefix='nr-geometry-') as tmp:
    work = Path(tmp)
    for name in ['policy.hpp', 'shader_source.hpp', 'test_geometry.cpp']:
        (work / name).write_bytes((root / name).read_bytes())
    for name, old, new, failure in cases:
        assert source.count(old) == 1, name
        (work / 'geometry.hpp').write_text(source.replace(old,new))
        exe = work / (name + '.exe')
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',
                        str(work/'test_geometry.cpp'),'-o',str(exe)], check=True,
                       capture_output=True, text=True)
        result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
        assert result.returncode == 1 and f'FAIL: {failure}' in result.stderr, (name,result)
        print('PASS negative control',name,': intended geometry assertion detected')
print('3 geometry defects detected; no GPU/shader execution')

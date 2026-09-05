"""Validate our newly built r2 update packages without extracting or executing ZIP contents."""
from pathlib import Path
import argparse
import zipfile

ROOT=Path(__file__).resolve().parents[1]
SCRIPTS=('Collect-Feedback.cmd','collect-feedback.ps1','Collect-Crash-Feedback.cmd',
         'collect-crash-feedback.ps1','feedback-report.py','parse-core-feedback.py','parse-f8-bundle.py')

def validate(directory: Path) -> None:
    packages=sorted(directory.glob('*.zip'))
    if len(packages)!=2:
        raise ValueError('exactly two language-specific update packages are required')
    languages=set()
    for path in packages:
        if 'beta0.4.3r2' not in path.name:
            raise ValueError('update archive does not identify r2')
        with zipfile.ZipFile(path) as archive:
            names=archive.namelist()
            if len(names)!=len(set(names)) or any('/' in n or '\\' in n or '\x00' in n for n in names):
                raise ValueError('duplicate, nested or invalid package member')
            addons=[n for n in names if n.endswith('.addon64')]
            if len(addons)!=1 or 'beta0.4.3r2' not in addons[0]:
                raise ValueError('update must contain exactly one r2 addon')
            en=addons[0].endswith('.en.addon64')
            languages.add(en)
            note='Instructions.txt' if en else '说明.txt'
            if set(names)!=set(SCRIPTS)|{addons[0],note}:
                raise ValueError('missing dependency or unexpected payload; update must not replace runtime/config')
            if archive.testzip() is not None:
                raise ValueError('package CRC error')
            if archive.read(addons[0])!=(ROOT/'build'/addons[0]).read_bytes():
                raise ValueError('packaged addon differs from this build')
            for name in SCRIPTS:
                if archive.read(name)!=(ROOT/'scripts'/name).read_bytes():
                    raise ValueError('packaged analysis/rescue source differs: '+name)
            raw=archive.read(note)
            if not raw.startswith(b'\xef\xbb\xbf') or b'\r\n' not in raw or 'beta0.4.3r2' not in raw.decode('utf-8-sig'):
                raise ValueError('instructions need r2 identity and Windows-readable UTF-8 BOM/newlines')
            print('PASS r2 language update: one exact addon, seven report/rescue files, instructions, no config/runtime overwrite')
    if languages!={False,True}:
        raise ValueError('one English and one Chinese package required')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    validate(p.parse_args().directory)

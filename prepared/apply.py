from pathlib import Path
p=Path(__file__).with_name('apply-v1.py')
s=p.read_text(encoding='utf-8')
old="& $binary '--pristine-null' '''"
assert s.count(old)==1
s=s.replace(old,"& $binary '--pristine-null'\n'''")
exec(compile(s,str(p),'exec'))

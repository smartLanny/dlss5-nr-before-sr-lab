from pathlib import Path
p=Path(__file__).with_name('finish-045.py')
s=p.read_text(encoding='utf-8')
s=s.replace('std::clamp(FiniteOr(strength, 0.0f), 0.0f, 2.0f)', 'std::isfinite(strength) ? std::clamp(strength, 0.0f, 2.0f) : 0.0f')
marker="  git('add','.')"
assert s.count(marker)==1
extra="""  p='docs/releases/beta0.4.5.md'
  write(p,read(p).replace('不是 DX11 或 0.5 分支','不含 DX11 或 0.5 分支'))
  p='.github/workflows/beta042-validate.yml'
  text=read(p)
  text=text.replace('          python3 scripts/validate-recommended-picture.py','          python3 scripts/validate-recommended-picture.py\\n          python3 scripts/validate-045-color.py\\n          python3 scripts/validate-045-color.py --negative-control')
  write(p,text)
  subprocess.run(['python3','scripts/validate-045-color.py'],cwd=root,check=True)
  subprocess.run(['python3','scripts/validate-045-color.py','--negative-control'],cwd=root,check=True)
"""
s=s.replace(marker,extra+marker)
exec(compile(s,str(p),'exec'))

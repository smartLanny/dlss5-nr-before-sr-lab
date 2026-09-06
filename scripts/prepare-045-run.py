"""Preparation-only corrections; never included in the product tree."""
from pathlib import Path
source = Path(__file__).with_name('prepare-045.py').read_text(encoding='utf-8')
old = "        git('add','.')"
assert source.count(old) == 1
source = source.replace(old, "        # Main was initially checked out under old text attributes; restore its raw evidence now.\n        git('restore','--source',BASE,'--staged','--worktree','--','bug-inbox')\n" + old)
old = "entries.append({'path':path,'mode':'100644','type':'blob','sha':sha})"
assert source.count(old) == 1
source = source.replace(old, "entries.append({'path':path,'mode':git('ls-files','-s','--',path).split()[0],'type':'blob','sha':sha})")
exec(compile(source, str(Path(__file__).with_name('prepare-045.py')), 'exec'))

from pathlib import Path
p=Path(__file__).with_name('finish-v1.py')
exec(compile(p.read_text(encoding='utf-8'),str(p),'exec'))
p=r/'docs/fixes/BETA045_FOLLOWUP.md'
s=p.read_text(encoding='utf-8')
s=s.replace('This branch builds on the published beta0.4.5, PR #117.', 'This D3D12 branch builds on the published beta0.4.5, PR #117.')
s+='''\n## Public API contracts\nMicrosoft documents RTV/DSV CPU descriptors as reusable immediately after\nOMSetRenderTargets. This is why a copied raw application handle is insufficient:\nhttps://learn.microsoft.com/en-us/windows/win32/direct3d12/non-shader-visible-descriptor-heaps\nGetAsyncKeyState documents bit 15 as physical-down and bit 0 as unreliable;\nshort unobserved taps remain best effort, not a promised lossless key stream:\nhttps://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate\n'''
p.write_text(s,encoding='utf-8',newline='\n')
subprocess.run(['git','diff','--check'],cwd=r,check=True)

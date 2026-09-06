from pathlib import Path
p=Path(__file__).with_name('apply-v1.py')
s=p.read_text(encoding='utf-8')
old="& $binary '--pristine-null' '''"
assert s.count(old)==1
s=s.replace(old,"& $binary '--pristine-null'\n'''")
exec(compile(s,str(p),'exec'))
p=r/'src/nr_command_pool_observation.inl';s=p.read_text(encoding='utf-8')
old='''    if (record.state == nullptr) {
        unsigned mask=0;
        if (!CommandFamilyInterfaces(native,mask) || mask!=family->interface_mask) return;
'''
new='''    if (record.state == nullptr ||
        (record.hooks_ready && family->null_guard && !record.state->null_guard_covered)) {
        unsigned mask=0;
        if (!CommandFamilyInterfaces(native,mask) || mask!=family->interface_mask) return;
    }
    if (record.state == nullptr) {
'''
assert s.count(old)==1;s=s.replace(old,new)
old='    static const GUID *const ids[] ='
new='''    // Identical public IIDs to InstallNativeCommandStateHooks; older SDKs
    // lack the named constants even when the runtime implements these APIs.
    static constexpr GUID list8={0xee936ef9,0x599d,0x4d28,{0x93,0x8e,0x23,0xc4,0xad,0x05,0xce,0x51}};
    static constexpr GUID list9={0x34ed2808,0xffe6,0x4c2b,{0xb1,0x1a,0xca,0xbd,0x2b,0x0c,0x59,0xe1}};
    static const GUID *const ids[] ='''
assert s.count(old)==1;s=s.replace(old,new).replace('&IID_ID3D12GraphicsCommandList8','&list8').replace('&IID_ID3D12GraphicsCommandList9','&list9')
p.write_text(s,encoding='utf-8',newline='\n')
p=r/'tests/command_pool_native.inl';s=p.read_text(encoding='utf-8')
old='            record->meta_contract.device_identity=identity;'
new=old+'''
            for (auto &family:g_observed_command_families) {
                if(family.device_identity==identity && family.vtable==lists[i]->lpVtbl) {
                    const unsigned mask=family.interface_mask;family.interface_mask^=128;
                    OnCommandStateBeforeReset(&owners[i]);
                    Check(!record->state,"different extended interface coverage rejected");
                    family.interface_mask=mask;
                }
            }'''
assert s.count(old)==1;p.write_text(s.replace(old,new),encoding='utf-8',newline='\n')
subprocess.run(['git','diff','--check'],cwd=r,check=True)

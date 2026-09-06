from pathlib import Path
p=Path(__file__).with_name('fix-045-native.py')
s=p.read_text(encoding='utf-8')
s=s.replace('constexpr FLOAT kDefaultBlendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};\n','')
s=s.replace('values != nullptr ? values : kDefaultBlendFactor','CommandStateBlendFactorValues(values)')
marker=" path.write_text(s.replace(old,new),newline='\\n')"
assert s.count(marker)==1
replacement=""" modified=s.replace(old,new)
 helper='''const FLOAT *CommandStateBlendFactorValues(const FLOAT values[4]) {
    static constexpr FLOAT defaults[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    return values != nullptr ? values : defaults;
}

'''
 assert modified.count('bool CanAutoRestoreRasterNull(')==1
 modified=modified.replace('bool CanAutoRestoreRasterNull(',helper+'bool CanAutoRestoreRasterNull(')
 path.write_text(modified,newline='\\n')
 path=root/'tests/blend_only_gpu.inl';s=path.read_text()
 assert 'gpu.list->OMSetBlendFactor(null_default?nullptr:expected);' in s
 s=s.replace('gpu.list->OMSetBlendFactor(null_default?nullptr:expected);',
     '// Native raw-NULL hook is exercised by pristine_null_native.inl; this\\n    // synthetic producer uses the same production canonicalizer before WARP.\\n    gpu.list->OMSetBlendFactor(CommandStateBlendFactorValues(null_default?nullptr:expected));')
 s=s.replace('void RunBlendOnly(Gpu &gpu, bool null_default, bool inject) {','void RunBlendOnly(Gpu &gpu, bool null_default, bool inject) {\\n    std::setvbuf(stdout,nullptr,_IONBF,0);')
 path.write_text(s,newline='\\n')"""
s=s.replace(marker,replacement)
s=s.replace("paths=['src/nr_command_state.inl','tests/pristine_null_native.inl','docs/releases/beta0.4.5.md']", "paths=['src/nr_command_state.inl','tests/pristine_null_native.inl','tests/blend_only_gpu.inl','docs/releases/beta0.4.5.md']")
exec(compile(s,str(p),'exec'))

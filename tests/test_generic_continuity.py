"""Verify optional continuous Generic contracts and exercise the production math.

The two new HLSL sections and real lighting/detail sampling helpers are compiled
exactly through a minimal C++ adapter. Six deliberately broken variants must fail on the specific reproducer,
not merely fail to compile. This is CPU/source evidence, not GPU validation.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def marked(source: str, name: str) -> str:
    begin, end = f'// {name}_BEGIN', f'// {name}_END'
    assert source.count(begin) == source.count(end) == 1, name
    return source.split(begin, 1)[1].split(end, 1)[0]


def definition(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def source_contracts(core: str) -> None:
    assert 'unsigned generic_algorithm=0;' in core
    same = definition(core, 'bool SameConfig(')
    history = definition(core, 'bool SameHistoryConfig(')
    signature = definition(core, 'NrTuningSignature CurrentNrTuningSignature(')
    assert 'a.generic_algorithm==b.generic_algorithm' in same
    assert 'generic_algorithm' not in history + signature
    assert 'L"GenericResidualAlgorithm",0,g_ini_path' in core
    assert 'WriteIniInt(L"GenericResidualAlgorithm"' in core
    assert 'GenericResidualAlgorithm=0' in (ROOT/'config/nr_before_sr.ini').read_text()
    dispatch = definition(core, 'void DispatchCodec(')
    assert 'color_mode == CodecColorMode::LinearSdr || color_mode == CodecColorMode::SrgbEncodedSdr' in dispatch
    assert 'constants.region_flags=generic_selection.flags;' in dispatch
    assert dispatch.index('constants.region_flags=generic_selection.flags;') < dispatch.index('constants.region_flags |= reconstruction.flags;')
    for entry in ('CombineCS', 'CombineV2CS'):
        body = definition(core, f'void {entry}(')
        assert body.count('GenericControlLinearV2(') == 1
        assert 'genericNeuralLuma = controlled.lumaLinear;' in body
        assert 'continuousGeneric ? genericNeuralLuma : dot(neural, kLuma)' in body
        assert 'ColourLabRawEditRequested(), continuousGeneric,' in body
        assert 'genericNeuralLuma - dot(proxy, kLuma)' in body
        assert 'storedOriginal.a' in body
        assert 'GenericControlLinearV2' not in body[:body.index('ColourLabRawEditRequested()')]
        assert body.count('ModelLightingMeanV2(') == 1
    pre = definition(core, 'void CombineV2CS(')
    assert 'rawColourLumaLog = continuousGeneric' in pre
    assert 'float3 chromaLog = clamp(logRgbRatio - logRatio.xxx,' in pre
    assert 'diagnosticDelta = max(diagnosticDelta, abs(genericNeuralLuma - dot(proxy, kLuma)))' in pre
    post = definition(core, 'void CombineCS(')
    assert 'abs(genericNeuralLuma - dot(proxy, kLuma)) <= 0.000001' in post
    for name in ('float ModelLumaRatioV2(', 'float2 ModelLightingTapV2('):
        body=definition(core,name)
        assert body.count('LoadProxyLinearV2(')==body.count('LoadNeuralLinearV2(')==1
        assert body.count('GenericNeuralLumaV2(proxy, neural)')==1
    taps=definition(core,'float GenericNeuralLumaV2(')
    assert 'if (!GenericContinuousRequestedV2()) return dot(neural, kLuma);' in taps
    assert 'GenericControlLumaV2(' in taps and 'GenericControlLinearV2(' not in taps
    assert 'GenericLinearToLabV2(' not in taps
    assert 'nl=max(GenericNeuralLumaV2(p,n),1.0/4096.0)' in definition(core,'void WriteCombinedV2(')
    for entry in ('BuildEditFieldCS', 'BuildColorEditFieldCS'):
        body = definition(core, f'void {entry}(')
        assert 'ApplyGenericStoredV1(' in body and 'GenericControlLinearV2(' not in body
    present=(ROOT/'src/nr_compat_present_models.inl').read_text()
    assert 'nr_generic::Select(g_config,combine,!hdr,false,false)' in present
    assert 'k.region_flags=generic_selection.flags;' in present
    assert 'RecordGenericPolicy(generic_selection,true)' in present
    assert core.count('// NR_RECONSTRUCTION_MATH_BEGIN') == 1
    assert core.count('float3 ColourLabPreparedReference(') == 1
    print('source contracts passed: config, no model/history signature, native/Present gates, raw/field boundaries, independent luma consumers, alpha writes')


def main() -> None:
    compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('No C++20 compiler available; CPU compilation was not performed.')
    core=(ROOT/'src/nr_before_sr.cpp').read_text()
    source_contracts(core)
    controls=marked((ROOT/'src/nr_generic_controls.hlsli').read_text(), 'NR_GENERIC_CONTROLS_MATH')
    restore=marked((ROOT/'src/nr_colour_restore.hlsli').read_text(), 'NR_COLOUR_RESTORE_MATH')
    assert controls == marked(core, 'NR_GENERIC_CONTROLS_MATH'), 'production control embedding differs'
    assert restore == marked(core, 'NR_COLOUR_RESTORE_MATH'), 'production restore embedding differs'
    legacy = definition(core, 'float3 GenericHslV1(') + '\n' + definition(core, 'float3 GenericRgbV1(')
    legacy += '''
float3 GenericLegacyColourV1(float3 original,float3 candidate,float colour) {
    float3 a=GenericHslV1(encode(original)),b=GenericHslV1(encode(candidate));
    b.y=saturate(a.y+(b.y-a.y)*colour);
    return decode(GenericRgbV1(b));
}
'''
    sampled_names=('bool GenericContinuousRequestedV2(', 'float GenericNeuralLumaV2(',
        'float ModelLumaRatioV2(', 'float2 ModelLightingTapV2(',
        'float ModelLightingMeanV2(', 'float4 DetailDecisionV2(')
    sampling='\n'.join(definition(core,name) for name in sampled_names)
    common=(legacy+'\n'+controls+'\n'+restore+'\n'+sampling).replace('[unroll]', '').replace('[branch]', '')
    coupled='''float deltaLuma = separateLuma ? modelLumaDelta * inverseSourceScale
                                 : dot(sourceDelta, kLuma);'''
    assert common.count(coupled) == 1
    direction='    result.colorLinear = candidate;'
    assert common.count(direction) == 1
    hue='        result.colorLinear = GenericLabToLinearV2(mixed) + correction;'
    assert common.count(hue) == 1
    colour_gate='    outValue.valid = true;'
    assert common.count(colour_gate) == 1
    variants=[('production',common,None),
        ('revert-full-rgb-direction',common.replace(direction,
            '    float deltaY = dot(candidate,kLuma)-dot(original,kLuma);\n    result.colorLinear = original + (candidate-original)*(deltaY>0 ? bright : deltaY<0 ? dark : 1);'),
            'bright-dark zero crossing continuous'),
        ('revert-grey-hue',common.replace(hue,
            '        result.colorLinear = GenericLegacyColourV1(original,candidate,colour);\n        (void)mixed; (void)correction;'),
            'grey-axis continuous'),
        ('couple-rgb-terminal-luma',common.replace(coupled,'float deltaLuma = dot(sourceDelta, kLuma);'),
            'RGB terminal preserves brightness with colour zero'),
        ('restore-luma-only-noedit-gate',common.replace(colour_gate,
            colour_gate+'\n    if (separateLuma && modelLumaDelta == 0.0f) return outValue;'),
            'RGB terminal keeps colour-only edits')]
    for name,marker,expected in (
        ('revert-raw-lighting-taps','float2 ModelLightingTapV2(',
         'LightingLock removes uniform controlled drift'),
        ('revert-raw-detail-taps','float ModelLumaRatioV2(',
         'detail terminal matches precontrolled model field')):
        body=definition(common,marker)
        assert body.count('GenericNeuralLumaV2(proxy, neural)')==1
        broken=body.replace('GenericNeuralLumaV2(proxy, neural)','dot(neural, kLuma)')
        variants.append((name,common.replace(body,broken),expected))
    with tempfile.TemporaryDirectory(prefix='nr-generic-continuity-') as temp:
        build=Path(temp)
        for name,math,expected_failure in variants:
            (build/'generic_continuity.generated.hpp').write_text(math)
            binary=build/(name+('.exe' if os.name=='nt' else ''))
            command=[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                     '-I',str(build),str(ROOT/'tests/generic_continuity_math.cpp'),'-o',str(binary)]
            if os.name=='nt': command.append('-static')
            subprocess.run(command,check=True,timeout=120)
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=60)
            output=result.stdout+result.stderr
            if expected_failure is None:
                assert result.returncode == 0, output
            else:
                assert result.returncode == 41 and expected_failure in output, (name,result.returncode,output)
            print(name+': '+output.strip())
        pages=(ROOT/'src/nr_unified_pages.inl').read_text()
        ui=(ROOT/'tests/generic_continuity_ui.cpp.in').read_text()
        ui=ui.replace('NR_UI_HELPERS',definition(pages,'const char* NrUi(')+'\n'+definition(pages,'bool NrCombo('))
        ui=ui.replace('NR_GENERIC_UI',marked(pages,'NR_GENERIC_CONTROLS_UI'))
        (build/'generic_ui.cpp').write_text(ui)
        binary=build/('generic-ui'+('.exe' if os.name=='nt' else ''))
        command=[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror',
                 '-I',str(ROOT/'src'),str(build/'generic_ui.cpp'),'-o',str(binary)]
        if os.name=='nt': command.append('-static')
        subprocess.run(command,check=True,timeout=120)
        subprocess.run([str(binary)],check=True,timeout=60)


if __name__=='__main__':
    main()

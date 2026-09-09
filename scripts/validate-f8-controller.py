"""Compile and run the current production F8 coordinator seam.

The harness is assembled from the current nr_f8_sequence.inl and the current
ChainConfig/NormalizeConfig definitions.  Only OS, config, GPU-grid and
packaging services are stubbed; the coordinator state machine and sequence
writer remain verbatim production code.  This deliberately does not exercise
the older nr_f8_runtime.inl model.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import struct
import zlib
import zipfile
import json
import importlib.util


def definition(source: str, marker: str, trailing_semicolon: bool = False) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(trailing_semicolon)]


def enum_or_struct(source: str, marker: str) -> str:
    return definition(source, marker, True)


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--source", type=Path)
parser.add_argument("--gpu", action="store_true")
parser.add_argument("--negative-control", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (args.source or root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
sequence = (root / "src/nr_f8_sequence.inl").read_text(encoding="utf-8-sig")

reload = definition(source, "void ReloadConfig(")
reload_prefix = reload[:reload.index("    // Snapshot existence")]
parts = {
    "RELOAD_ENTRY": reload_prefix,
    "CONFIG_SNAPSHOT": definition(source, "ChainConfig SnapshotConfig("),
    "OVERLAY_SNAPSHOT_TYPE": enum_or_struct(source, "struct OverlayConfigSnapshot"),
    "OVERLAY_SNAPSHOT": definition(source, "OverlayConfigSnapshot SnapshotOverlayConfig("),
    "CHAIN_CONFIG": enum_or_struct(source, "struct ChainConfig"),
    "TUNING_SIGNATURE": enum_or_struct(source, "struct NrTuningSignature"),
    "DIAGNOSTIC_IDENTITY": enum_or_struct(source, "struct NrDiagnosticIdentity"),
    "STAGE_INFO": enum_or_struct(source, "struct NrStageCaptureInfo"),
    "FINITE_OR": definition(source, "float FiniteOr("),
    "NORMALIZE_CONFIG": definition(source, "void NormalizeConfig("),
    "SAME_CONFIG": definition(source, "bool SameConfig("),
    "ACTIVE_TRANSFER": definition(source, "float ActiveTransferStrength("),
    "CAPTURE_GRID": definition((root / "src/nr_stage_capture.inl").read_text(encoding="utf-8-sig"), "bool ComputeStageCaptureGrid("),
    "PRODUCTION_SEQUENCE": sequence[:sequence.index("void F8Tooltip(")] + "\n} // namespace nr_f8\n",
}
harness = (root / "tests/f8_controller_harness.cpp.in").read_text(encoding="utf-8")
face_tests = (root / "tests/f8_face_diagnostic.inl").read_text(encoding="utf-8")
harness = harness.replace("int main(", face_tests + "\nint main(", 1)
# The legacy template models the archive service only. F7 must explicitly ask
# for that archive; F8's real single-TXT path is exercised by validate-core-feedback.
legacy_request = "bool Request(){return true;}"
assert harness.count(legacy_request) == 1
harness = harness.replace(legacy_request, "bool Request(bool archive = false){return archive;}")
for marker, value in parts.items():
    harness = harness.replace(marker, value)
if args.gpu:
    harness = harness.replace(face_tests, "").replace("    TestFaceDiagnostic();", "")
    gpu = (root / "tests/codec_gpu_harness.cpp.in").read_text(encoding="utf-8")
    gpu = gpu[:gpu.index("struct RunResult")]
    start = source.index("constexpr UINT kCodecSrvCount")
    end = source.index(";", source.index("constexpr UINT kCodecRootConstantCount",start))+1
    gpu = gpu.replace("// @PRODUCTION_DESCRIPTOR_CONSTANTS@",source[start:end])
    start=source.index('const char kCodecShader[] = R"HLSL(')
    end=source.index(')HLSL";',start)+len(')HLSL";')
    gpu=gpu.replace("// @PRODUCTION_SHADER@",source[start:end])
    gpu=gpu.replace("// @PRODUCTION_CONSTANTS@",enum_or_struct(source,"struct CodecConstants"))
    harness=harness.replace("namespace reshade {",gpu+"\nnamespace reshade {",1)
    start=harness.index("struct ID3D12Resource {");end=harness.index("struct FeedbackSession",start)
    wrappers=(root/"tests/f8_gpu_wrappers.h").read_text(encoding="utf-8")
    harness=harness[:start]+wrappers+"\n"+harness[end:]
    start=harness.index("void DeliverFrame(");end=harness.index("void FinishOff(",start)
    harness=harness[:start]+"void DeliverFrame(unsigned long long,unsigned=3840,unsigned=2160,bool=false) {}\n"+harness[end:]
    start=harness.index("void TestFailuresAndLateDelivery(");end=harness.index("void TestBudgetAndLegacyFailure(",start)
    harness=harness[:start]+harness[end:]
    start=harness.index("int main(")
    harness=harness[:start]+(root/"tests/f8_gpu_integration.inl").read_text(encoding="utf-8")+"\n"+harness[start:]
    harness=harness.replace("TestDefaultsAndPictures();TestContinuous(3840,2160);TestContinuous(3840,2400);TestShareMatrix();TestCurrentRrRoute();TestPartialAndRestore();TestFailuresAndLateDelivery();TestBudgetAndLegacyFailure();","TestGpuPipeline();")
out = root / ("test-output/f8-gpu-v4" if args.gpu else "test-output/f8-controller-v4")
out.mkdir(parents=True, exist_ok=True)
cpp, exe = out / "controller.cpp", out / "controller.exe"
cpp.write_text(harness, encoding="utf-8")
# A complete 4K PNG supplied at the runtime screenshot boundary. The archive
# must retain these exact bytes; this does not emulate a real game renderer.
def png_chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
row = b'\0' + bytes((x * 17) % 256 for x in range(3840 * 3))
native_png = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB',3840,2160,8,2,0,0,0)) + png_chunk(b'IDAT',zlib.compress(row * 2160)) + png_chunk(b'IEND',b'')
(out/'native-display.png').write_bytes(native_png)
subprocess.run([args.compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-static", "-I", str(root / "src"), str(cpp), "-o", str(exe)]+(["-ld3d12","-ldxgi","-ld3dcompiler","-ldxguid"] if args.gpu else []), check=True)
subprocess.run([str(exe), str(out)], check=True)
latest = max(out.glob('run-*'), key=lambda p:p.stat().st_mtime_ns)
for groups in (1,7):
    for skin in (0,2):
        with zipfile.ZipFile(latest/f'fullscreen-{groups}-{skin}.zip') as archive:
            images=[n for n in archive.namelist() if n.startswith('groups/') and n.endswith('/display.png')]
            assert len(images)==groups and not any(n.endswith('.f8c') for n in archive.namelist())
            for i in range(groups):
                assert archive.read(f'groups/{i}/display.png') == native_png
                settings=json.loads(archive.read(f'groups/{i}/settings.json'))
                assert settings['requested']['skin']==skin and settings['actual']['skin']==skin
                assert settings['actual']['autoMask']==1
print('Full-screen archives: 16 complete 3840 x 2160 PNGs byte-identical; face 0/2 recorded; no raw ROI planes PASS')
if not args.gpu:
    with zipfile.ZipFile(latest/'face-diagnostic.zip') as archive:
        for i in range(9):
            assert archive.read(f'groups/{i}/display.png') == native_png
            settings=json.loads(archive.read(f'groups/{i}/settings.json'))
            assert settings['faceDiagnostic']['roiTarget']==1 and settings['faceDiagnostic']['roiX']==1549 and settings['faceDiagnostic']['roiY']==83
            assert settings['requested']==settings['actual'] and settings['complete']==1
        assert archive.read('core/feedback.txt') == b'fixture-only=true\n'
        capture_info = archive.read('capture-info.txt').decode('utf-8')
        assert 'faceDiagnostic.selectionSourceWidth=2228\n' in capture_info
        assert 'faceDiagnostic.selectionSourceHeight=1256\n' in capture_info
        manifest=json.loads(archive.read('manifest.json'))
        assert manifest['input_rejections']==0 and len(manifest['entries'])<=64
        assert any(e['name']=='core/paired-archive' and e['status']=='hashed' and len(e['sha256'])==64 for e in manifest['entries'])
    spec=importlib.util.spec_from_file_location('f8_independent',root/'scripts/parse-f8-bundle.py')
    reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
    analysis=reader.inspect_bundle(latest/'face-diagnostic.zip')
    assert analysis['face_diagnostic']['complete'] and analysis['manifest_verified']
    (latest/'face-diagnostic-analysis.json').write_text(json.dumps(analysis,indent=2)+'\n',encoding='utf-8')
    print('Face diagnostic: nine full 3840 x 2160 PNGs byte-identical, nine off-center 512 ROIs, actual tuning, paired core hashes and independent parser PASS')
if args.negative_control:
    assert not args.gpu, 'Controller mutations must not run the GPU fixture'
    mutations={
        'screenshot-before-gpu': ('(!face || g.frames[0].valid)', 'true'),
        'ignore-applied-tuning': ('!nr_face_diagnostic::MatchesApplied(g.requested,info.identity.tuning)', 'false'),
        'ignore-geometry-change': ('s->face_geometry_set && !SameFaceGeometry(s->face_geometry,info)', 'false'),
        'ignore-selected-source-extent': ('!s->face_geometry_set && s->options.expected_source_width != 0', 'false'),
        'omit-face-restore': ('if(s->options.comparison!=Comparison::Current)ApplyConfig(s->original', 'if(s->options.comparison!=Comparison::Current && s->options.comparison!=Comparison::FaceDiagnostic)ApplyConfig(s->original'),
        'skip-display-budget': ('!nr_face_diagnostic::AdmitDisplay(s->display_width,s->display_height)?', 'false?'),
    }
    results=[]
    for name,(old,new) in mutations.items():
        assert harness.count(old)==1,(name,harness.count(old))
        bad_cpp,bad_exe=out/f'negative-{name}.cpp',out/f'negative-{name}.exe'
        bad_cpp.write_text(harness.replace(old,new),encoding='utf-8')
        subprocess.run([args.compiler,'-std=c++20','-O0','-static','-I',str(root/'src'),str(bad_cpp),'-o',str(bad_exe)],check=True)
        run=subprocess.run([str(bad_exe),str(out)],capture_output=True,text=True)
        assert run.returncode==1 and 'FAIL:' in run.stderr,(name,run.returncode,run.stderr)
        results.append({'name':name,'compiled':True,'exit_code':run.returncode,'assertion':run.stderr.strip()})
    (latest/'face-negative-controls.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
    print(f'Face diagnostic: {len(mutations)} compiled behavioral negative controls rejected PASS')
print("F8 controller: production Present/callback/readback/stream/restore/ZIP PASS; " + ("real WARP shader/ring/fence, synthetic NR input" if args.gpu else "GPU/NGX are seams"))

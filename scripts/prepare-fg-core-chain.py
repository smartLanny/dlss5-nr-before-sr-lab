"""Patch a clean, pinned external renderer for opt-in Core NR before native SR/FG.

No SDK source/binary is copied into this repository. No build, deploy or run.
The independent sample tree must be pristine at every target path.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess


def digest(data):
    return hashlib.sha256(data).hexdigest()


SDK_CMAKE = '''# Explicit existing SDK cache; no SDK download or source import.
if(NOT STREAMLINE_SDK_ROOT OR NOT EXISTS "${STREAMLINE_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "Pass STREAMLINE_SDK_ROOT for the fixed existing SDK cache")
endif()
add_subdirectory("${STREAMLINE_SDK_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/sdk")
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sample', type=Path, required=True)
    ap.add_argument('--manifest', type=Path, required=True)
    a = ap.parse_args()
    scripts = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('fg_sample', scripts/'prepare-fg-sample.py')
    fg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fg)
    head = subprocess.check_output(['git', '-C', str(a.sample), 'rev-parse', 'HEAD'], text=True).strip()
    if head != fg.SAMPLE or a.manifest.exists() or not a.manifest.parent.is_dir():
        raise ValueError('Require the fixed sample and a new manifest')
    patches = {}
    for name in ('src/SLWrapper.cpp', 'src/main.cpp', 'src/StreamlineSample.cpp', 'donut/src/app/DeviceManager.cpp'):
        original = subprocess.check_output(['git', '-C', str(a.sample), 'show', 'HEAD:'+name])
        current = (a.sample/name).read_bytes()
        text = original.decode().replace('\r\n', '\n')
        if current.decode().replace('\r\n', '\n') != text:
            raise ValueError('Preserve prior sample work; use a clean external tree: '+name)
        patched = fg.transform(name, text)
        if name == 'src/StreamlineSample.cpp':
            patched = fg.once(patched, '#include "StreamlineSample.h"',
                              '#include "StreamlineSample.h"\n#include "nr-fg-sample/core-chain.h"')
            patched = fg.once(patched, 'void StreamlineSample::RenderScene(nvrhi::IFramebuffer* framebuffer)\n{',
                              'void StreamlineSample::RenderScene(nvrhi::IFramebuffer* framebuffer)\n{\n    nr_fg_sample::Get().Init(GetDevice(), this);')
            patched = fg.once(patched, 'StreamlineSample::~StreamlineSample()\n{',
                              'StreamlineSample::~StreamlineSample()\n{\n    nr_fg_sample::Get().Shutdown(this);')
            patched = fg.once(patched, '\n    NVWrapper::Get().TagResources_General(m_CommandList,',
                              '''
    nr_fg_sample::Get().BeforeTags(m_CommandList,
        {m_RenderTargets->Depth, m_RenderTargets->MotionVectors, m_RenderTargets->PreUIColor});
    NVWrapper::Get().TagResources_General(m_CommandList,''')
            patched = fg.once(patched, '\n    NVWrapper::Get().TagResources_DLSS_NIS(m_CommandList,',
                              '''
    nr_fg_sample::Get().BeforeTags(m_CommandList,
        {m_RenderTargets->HdrColor, m_RenderTargets->AAResolvedColor});
    NVWrapper::Get().TagResources_DLSS_NIS(m_CommandList,''')
            patched = fg.once(patched, '            NVWrapper::Get().EvaluateDLSS(m_CommandList);', '''            // One renderer-owned base frame; before SR, UI and the existing FG presenter.
            auto nrJitter = std::dynamic_pointer_cast<PlanarView, IView>(m_View)->GetPixelOffset();
            nr_fg_sample::Get().Record(m_CommandList, m_RenderTargets->HdrColor,
                m_RenderTargets->Depth, m_RenderTargets->MotionVectors,
                unsigned(m_RenderingRectSize.x), unsigned(m_RenderingRectSize.y), GetFrameIndex(),
                nrJitter.x, nrJitter.y, needNewPasses || !m_PreviousViewsValid, m_View->IsReverseDepth());
            NVWrapper::Get().EvaluateDLSS(m_CommandList);''')
            patched = fg.once(patched, '''    GetDevice()->executeCommandList(m_CommandList);

    // CLEANUP''', '''    GetDevice()->executeCommandList(m_CommandList);
    nr_fg_sample::Get().Submitted();

    // CLEANUP''')
        patches[name] = (original, patched.encode())
    owned = {'core-chain.h': scripts/'fg-sample-core-chain.h'}
    for name in ('nr_external_provider_producer.h', 'nr_external_provider_abi.h', 'nr_external_provider_completion.h'):
        owned[name] = scripts.parent/'src'/'interop'/name
    destination = a.sample/'src/nr-fg-sample'
    sdk_cmake_path = a.sample/'streamline/CMakeLists.txt'
    if destination.exists() or sdk_cmake_path.exists():
        raise ValueError('Owned adapter stage must not exist')
    payloads = {name: source.read_bytes() for name, source in owned.items()}
    # All validation/read checks precede the first mutation.
    destination.mkdir()
    for name, content in payloads.items():
        (destination/name).write_bytes(content)
    for name, (_, content) in patches.items():
        (a.sample/name).write_bytes(content)
    sdk_cmake_path.write_text(SDK_CMAKE, encoding='utf-8', newline='\n')
    source_commit = subprocess.check_output(['git', '-C', str(scripts.parent), 'rev-parse', 'HEAD'], text=True).strip()
    source_dirty = bool(subprocess.check_output(['git', '-C', str(scripts.parent), 'status', '--porcelain']))
    manifest = {'sample_commit': head, 'adapter_commit': source_commit, 'adapter_dirty': source_dirty,
                'core_path_environment': 'NR_FG_SAMPLE_CORE', 'nr_opt_in': True,
                'single_view_only': True, 'completion': 'serial actual graphics queue fence',
                'game_runtime': False, 'compiled': False,
                'sdk_cmake_sha256': digest(SDK_CMAKE.encode()),
                'patches': {name: {'upstream_sha256': digest(raw), 'patched_sha256': digest(out)}
                            for name, (raw, out) in patches.items()},
                'owned_headers': {name: digest(content) for name, content in payloads.items()}}
    a.manifest.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print('Prepared Core -> sample SR -> existing FG path; no runtime result implied.')


if __name__ == '__main__':
    main()

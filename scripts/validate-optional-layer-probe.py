#!/usr/bin/env python3
"""Compile the real sparse F8 observer with typed NGX/descriptor seams.

Only the ABI boundary, clock and descriptor implementation are mocked. The
production observer, serializer, optional key table and getter policy are used
unchanged. No GPU, model, game or engine-layer availability is claimed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker, semicolon=False):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end + int(semicolon)]


HARNESS = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include "nr_optional_layer_probe.h"
using NgxResult=std::uint32_t;
constexpr NgxResult kNgxSuccess=1;
constexpr NgxResult kFailure=0xBAD00004u;
HANDLE_TYPE
PARAM_TYPE
PARAM_SLOT
using GetVoidFn=NgxResult (*)(const NgxParams *,const char *,void **);
using GetD3D12Fn=NgxResult (*)(const NgxParams *,const char *,ID3D12Resource **);
using GetUIntFn=NgxResult (*)(const NgxParams *,const char *,unsigned *);
using GetIntFn=NgxResult (*)(const NgxParams *,const char *,int *);
using GetFloatFn=NgxResult (*)(const NgxParams *,const char *,float *);
GET_RESOURCE
GET_UINT
GET_INT
GET_FLOAT
FEATURE_TYPE
namespace reshade { namespace api { struct effect_runtime; } }
SESSION_TYPE
SRWLOCK g_feedback_lock=SRWLOCK_INIT;
std::atomic<unsigned> g_feedback_status{1};
std::atomic<int> g_last_bypass_reason{0};
enum class BypassReason { None };
const char *BypassReasonName(BypassReason) { return "none"; }
FeedbackSession session;
FeedbackSession *g_feedback_session=&session;
unsigned checks=0,descriptions=0,typed_reads=0,untyped_reads=0,scalar_reads=0;
ULONGLONG clock_value=10000;
void Check(bool ok,const char *message) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"OPTIONAL_LAYER_ORACLE: %s\n",message);std::exit(23); }
}
const auto real_resource=reinterpret_cast<ID3D12Resource *>(std::uintptr_t(0x10000));
void *const poison=reinterpret_cast<void *>(std::uintptr_t(1));
struct Response {
    NgxResult typed=kFailure,untyped=kFailure,scalar=kFailure;
    ID3D12Resource *typed_value=nullptr;
    void *untyped_value=nullptr;
    unsigned scalar_value=0;
} response;
bool IsLayer(const char *key) {
    for(const auto &layer:nr_optional_layer_probe::kLayers)
        if(std::strcmp(key,layer.key)==0)return true;
    return false;
}
bool IsScalar(const char *key) {
    if(std::strcmp(key,nr_optional_layer_probe::kRoughnessMode)==0)return true;
    for(const auto &layer:nr_optional_layer_probe::kLayers)
        for(const char *rect:layer.rect)if(rect&&std::strcmp(key,rect)==0)return true;
    return false;
}
NgxResult ReadTyped(const NgxParams *,const char *key,ID3D12Resource **out) {
    *out=nullptr;if(!IsLayer(key))return kFailure;
    ++typed_reads;*out=response.typed_value;return response.typed;
}
NgxResult ReadUntyped(const NgxParams *,const char *key,void **out) {
    *out=nullptr;if(!IsLayer(key))return kFailure;
    ++untyped_reads;*out=response.untyped_value;return response.untyped;
}
NgxResult ReadUnsigned(const NgxParams *,const char *key,unsigned *out) {
    *out=0;if(!IsScalar(key))return kFailure;
    ++scalar_reads;*out=response.scalar_value;return response.scalar;
}
NgxResult ReadInteger(const NgxParams *,const char *,int *) { return kFailure; }
NgxResult ReadFloat(const NgxParams *,const char *,float *) { return kFailure; }
std::array<void *,15> table{};
NgxParams params{table.data()};
void Getters(bool typed=true,bool untyped=true,bool scalar=true) {
    table={};
    if(typed)table[9]=reinterpret_cast<void *>(&ReadTyped);
    if(untyped)table[8]=reinterpret_cast<void *>(&ReadUntyped);
    if(scalar)table[12]=reinterpret_cast<void *>(&ReadUnsigned);
    table[11]=reinterpret_cast<void *>(&ReadInteger);
    table[14]=reinterpret_cast<void *>(&ReadFloat);
}
D3D12_RESOURCE_DESC Describe(ID3D12Resource *resource) {
    Check(resource==real_resource,"only a successful typed resource may reach GetDesc; poison void is never inspected");
    ++descriptions;
    D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=UINT64_MAX;d.Height=UINT_MAX;
    d.DepthOrArraySize=UINT16_MAX;d.MipLevels=UINT16_MAX;
    d.Format=static_cast<DXGI_FORMAT>(UINT_MAX);
    d.SampleDesc.Count=UINT_MAX;d.Flags=static_cast<D3D12_RESOURCE_FLAGS>(UINT_MAX);
    return d;
}
#undef ID3D12Resource_GetDesc
#define ID3D12Resource_GetDesc Describe
#define GetTickCount64() clock_value
APPEND
LAYER_OBSERVER
OBSERVER
#undef GetTickCount64
#undef ID3D12Resource_GetDesc
void Reset() {
    session={};g_feedback_session=&session;g_feedback_status=1;clock_value=10000;
    descriptions=typed_reads=untyped_reads=scalar_reads=0;response={};Getters();
}
void Observe(int feature_id=1,const NgxParams *observed=&params) {
    FeatureRecord feature;feature.feature_id=feature_id;feature.generation=37;
    feature.width=2228;feature.height=1253;feature.out_width=3840;feature.out_height=2160;
    feature.create_flags=75;FeedbackObserveEvaluate(observed,feature,91);
}
bool Has(const char *value) { return std::strstr(session.report,value)!=nullptr; }
void AllStatus(const char *status) {
    for(const auto &layer:nr_optional_layer_probe::kLayers) {
        const std::string expected=std::string("layerProbe.")+layer.key+"=status:"+status;
        Check(std::strstr(session.report,expected.c_str())!=nullptr,"every frozen resource key uses the observed presence class");
    }
}
int main() {
    const unsigned count=static_cast<unsigned>(std::size(nr_optional_layer_probe::kLayers));
    Reset();response={kNgxSuccess,kNgxSuccess,kNgxSuccess,real_resource,poison,UINT_MAX};
    // Simulate existing F8 metadata before its first actual sparse callback.
    session.report_used=12000;std::memset(session.report,'x',session.report_used);
    session.report[session.report_used]='\n';++session.report_used;
    const std::size_t before=session.report_used;
    Observe(13);AllStatus("typed-present");
    Check(descriptions==count&&typed_reads==count&&untyped_reads==count,"all resources inspected once through the real observer");
    Check(scalar_reads==25,"query only the 24 frozen subrect fields plus roughness mode");
    Check(Has("layerProbe.sample=1 featureId=13 generation=37 carrier=91\n"),"record the real callback identity without relabelling RR as Feature1");
    Check(Has("engine-and-Streamline-not-enumerated"),"scope is NGX only");
    Check(Has("layerProbe.complete=1\n")&&!session.report_truncated,"all-present observation complete");
    const std::size_t added=session.report_used-before;
    Check(added<nr_optional_layer_probe::kReportBudget,"maximum numeric resource/subrect rows remain within reserved probe budget");
    const std::string tail(7000,'t');FeedbackAppend(session,"%s\nend.marker=retained\n",tail.c_str());
    Check(Has("end.marker=retained\n")&&!session.report_truncated,"worst-case optional rows preserve the existing trailer budget");
    Check(!Has("0x00010000")&&!Has("0x0000000000010000"),"resource addresses are not serialized");

    Reset();response={kNgxSuccess,kNgxSuccess,kNgxSuccess,nullptr,poison,0};
    Observe();AllStatus("untyped-present-not-inspected");
    Check(descriptions==0&&Has("typed:success-null(0x00000001)"),"typed success-null is distinct and poison void remains opaque");
    Check(Has(",x:0,y:0,w:0,h:0"),"a supplied zero subrect value is not replaced by a default or absence");
    const auto reads=typed_reads;clock_value+=100;Observe();
    session.observations=59;clock_value+=100;Observe();
    Check(typed_reads==reads&&session.observations==60,"optional probes run only at first sparse observation, never per frame or again at 60");

    Reset();response={kFailure,kNgxSuccess,kFailure,static_cast<ID3D12Resource *>(poison),poison,UINT_MAX};
    Observe();AllStatus("untyped-present-not-inspected");
    Check(descriptions==0&&Has("typed:getter-error(0xBAD00004)"),"failed typed getter cannot leak its nonnull output into GetDesc");
    Check(Has("x:getter-error(0xBAD00004)")&&!Has(",x:4294967295"),"failed scalar output is not reported as a real subrect");

    Reset();response={kFailure,kFailure,kFailure,static_cast<ID3D12Resource *>(poison),poison,UINT_MAX};
    Observe();AllStatus("unknown-access");
    Check(descriptions==0&&Has("untyped:getter-error(0xBAD00004)"),"both errors retain their result codes without inspecting poison");
    Check(!Has("status:not-supplied"),"getter errors cannot be reported as resource absence");
    Check(scalar_reads==1,"absent resource rows do not invent or query subrects");

    Reset();response={kNgxSuccess,kNgxSuccess,kNgxSuccess,nullptr,nullptr,0};
    Observe();AllStatus("not-supplied");
    Check(descriptions==0&&Has("typed:success-null(0x00000001),untyped:success-null(0x00000001)"),"successful null pointers differ from failed getters");

    Reset();Getters(false,true,false);response.untyped=kNgxSuccess;response.untyped_value=poison;
    Observe();AllStatus("untyped-present-not-inspected");
    Check(descriptions==0&&Has("typed:missing-getter")&&Has("x:missing-getter"),"missing getter slots are explicit and no resource is inferred");
    Reset();Getters(false,false,false);Observe();AllStatus("unknown-access");
    Check(Has("typed:missing-getter,untyped:missing-getter"),"missing methods do not fabricate HRESULT success");
    Check(!Has("status:not-supplied"),"missing methods cannot be reported as resource absence");
    Reset();response.typed=kNgxSuccess;Observe();AllStatus("unknown-access");
    Check(!Has("status:not-supplied"),"one successful null plus one getter error is not sufficient absence evidence");
    Reset();response.untyped=kNgxSuccess;Observe();AllStatus("unknown-access");
    Check(!Has("status:not-supplied"),"typed error plus untyped successful null remains unknown");
    Reset();Observe(1,nullptr);AllStatus("unknown-access");
    Check(typed_reads==0&&descriptions==0,"null parameter block never invokes a getter");
    Reset();NgxParams no_table{};Observe(1,&no_table);AllStatus("unknown-access");
    Check(typed_reads==0&&descriptions==0,"null parameter vtable never invokes a getter");

    Reset();g_feedback_status=2;Observe();Check(typed_reads==0&&session.observations==0,"no request means no probe");
    Reset();g_feedback_session=nullptr;Observe();Check(typed_reads==0,"no live session means no probe");
    Reset();session.last_observation=clock_value-50;Observe();Check(typed_reads==0,"ordinary calls respect the 100ms observer interval");
    Reset();session.observations=119;session.timeline_omitted=1;Observe();Check(typed_reads==0,"bounded observation count remains enforced");
    Reset();AcquireSRWLockExclusive(&g_feedback_lock);Observe();ReleaseSRWLockExclusive(&g_feedback_lock);
    Check(typed_reads==0,"busy feedback lock returns without querying");

    Reset();session.report_used=sizeof(session.report)-8192-nr_optional_layer_probe::kReportBudget+1;
    std::memset(session.report,'b',session.report_used);session.report[session.report_used]=0;
    FeedbackAppendOptionalLayers(session,&params,FeatureRecord{},91);
    Check(typed_reads==0&&untyped_reads==0&&Has("layerProbe.status=skipped-metadata-budget\n"),"insufficient optional budget is explicit and performs no queries");
    std::printf("PASS optional layer observer: %u assertions; %u frozen resources; max callback text=%zu bytes; poison/null/error/missing slots, sparse cadence and trailer preservation. CPU mocks only.\n",checks,count,added);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'clang++'))
    parser.add_argument('--output-directory', type=Path)
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    core = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    feedback = (ROOT / 'src/nr_feedback_capture.inl').read_text(encoding='utf-8')
    layer = re.search(r'// OPTIONAL_LAYER_OBSERVER_BEGIN\n(.*?)// OPTIONAL_LAYER_OBSERVER_END', feedback, re.S).group(1)
    observer = definition(feedback, 'void FeedbackObserveEvaluate(')
    assert 'if (s->observations == 1) FeedbackAppendOptionalLayers(*s, params, feature, carrier);' in observer
    assert 'FeedbackObserveEvaluate(params, feature, carrier);' in core
    rr = (ROOT / 'src/interop/nr_rr_post_runtime.inl').read_text(encoding='utf-8')
    assert rr.index('FeedbackObserveEvaluate(params,feature,carrier);') < rr.index('const NgxResult native_result=original(')
    tokens = {
        'HANDLE_TYPE': definition(core, 'struct NgxHandle', True),
        'PARAM_TYPE': definition(core, 'struct NgxParams', True),
        'PARAM_SLOT': definition(core, 'template <typename Fn>'),
        'GET_RESOURCE': definition(core, 'bool GetResource('),
        'GET_UINT': definition(core, 'bool GetUInt('),
        'GET_INT': definition(core, 'bool GetInt('),
        'GET_FLOAT': definition(core, 'bool GetFloat('),
        'FEATURE_TYPE': definition(core, 'struct FeatureRecord', True),
        'SESSION_TYPE': definition(feedback, 'struct FeedbackSession', True),
        'APPEND': definition(feedback, 'void FeedbackAppend('),
        'LAYER_OBSERVER': layer,
        'OBSERVER': observer,
    }
    production = HARNESS
    for token, value in tokens.items():
        production = production.replace('\n' + token + '\n', '\n' + value + '\n')
    variants = [('production', production)]
    if args.negative_control:
        unsafe = production.replace('if (typed.Present()) {', 'if (typed.Present() || untyped.Present()) {')
        unsafe = unsafe.replace('ID3D12Resource_GetDesc(typed_value)',
            'ID3D12Resource_GetDesc(typed.Present() ? typed_value : static_cast<ID3D12Resource *>(untyped_value))')
        repeated = production.replace('if (s->observations == 1) FeedbackAppendOptionalLayers',
            'if (s->observations == 1 || s->observations == 60) FeedbackAppendOptionalLayers')
        false_absence = production.replace('known_empty ? "not-supplied" : "unknown-access"',
            'known_empty ? "not-supplied" : "not-supplied"')
        assert unsafe != production and repeated != production and false_absence != production
        variants += [('unsafe-void-inspection', unsafe), ('repeat-probe-at-60', repeated), ('errors-as-absence', false_absence)]
    if args.output_directory:
        args.output_directory.mkdir(parents=True, exist_ok=False)
    runs = []
    with tempfile.TemporaryDirectory(prefix='nr-optional-layer-') as temporary:
        directory = args.output_directory or Path(temporary)
        for name, code in variants:
            cpp = directory / (name + '.cpp')
            exe = directory / (name + '.exe')
            cpp.write_text(code, encoding='utf-8')
            subprocess.run([args.compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                '-I', str(ROOT / 'src'), str(cpp), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            if name == 'production':
                if run.returncode != 0: raise RuntimeError(f'Exit {run.returncode}: ' + run.stdout + run.stderr)
                print(run.stdout.strip())
            elif run.returncode != 23 or 'OPTIONAL_LAYER_ORACLE:' not in run.stderr:
                raise RuntimeError(f'Negative control did not fail as expected: {name}: {run}')
            else:
                print(f'PASS compiled negative control {name}: {run.stderr.strip()}')
            runs.append({'name': name, 'returncode': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr})
        if args.output_directory:
            files = ['src/nr_feedback_capture.inl', 'src/nr_optional_layer_probe.h', 'scripts/validate-optional-layer-probe.py']
            result = {'schema': 'nr-optional-layer-observer-test-v1', 'success': True,
                'gpu_or_game_verified': False, 'actual_layer_availability_verified': False,
                'inputs': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in files}, 'runs': runs}
            (directory / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

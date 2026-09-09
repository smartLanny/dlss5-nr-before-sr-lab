#!/usr/bin/env python3
"""Derive a private, visibly experimental Core from a verified generated stage.

The explicit source patches and frozen kernel bindings are recorded in a new
manifest. This does not add any production configuration/Auto qualification
path, alter an existing stage, install to a game, or execute graphics work.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(path, old, new):
    data = path.read_text(encoding='utf-8-sig')
    if data.count(old) != 1:
        raise ValueError('Private experiment patch anchor is not unique: ' + str(path))
    path.write_text(data.replace(old, new), encoding='utf-8')


def cpp_string(value):
    # JSON escaping is C++ string syntax here, never shell quoting.
    return json.dumps(str(value), ensure_ascii=True)


HEADER = r'''#pragma once
// Generated private laboratory profile. Never included by the normal Core.
namespace nr_nvfp4_test {
inline constexpr bool candidate = @CANDIDATE@;
inline constexpr unsigned warmup = @WARMUP@;
inline constexpr const char* mode = @MODE@;
inline constexpr const wchar_t* asset_directory = L@ASSETS@;
inline constexpr const wchar_t* sample_path = L@SAMPLES@;
inline volatile LONG epoch_requested = 0;
inline volatile LONG epoch_started = 0;
inline std::uint64_t epoch = 0, frame = 0;
inline HANDLE file = INVALID_HANDLE_VALUE;
inline const DWORD process_id=GetCurrentProcessId();
inline const ULONGLONG run_start_ticks=GetTickCount64();
struct Row {
    nr_gpu_timing::Key key;
    std::uint64_t ticket,epoch,frame,pairs,launches,barriers,observed_m;
    UINT64 ticks[7];double scale;
};
inline Row rows[16384]{};
inline unsigned row_count=0,flushed=0,dropped=0;
inline SRWLOCK row_lock=SRWLOCK_INIT,flush_lock=SRWLOCK_INIT;
inline void Configure(nr::nvfp4::Config& config) {
    config.asset_directory = asset_directory;
    config.diagnostic_passthrough = false;
    @CONFIGURE@
    if(file == INVALID_HANDLE_VALUE)
        file = CreateFileW(sample_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Log("[nvfp4-test] private compiled profile=%s candidate=%d; renderer Auto registry is unchanged", mode, candidate);
}
inline void Write(const nr_gpu_timing::Key& key, std::uint64_t ticket,
                  std::uint64_t sample_epoch, std::uint64_t sample_frame,
                  std::uint64_t pairs, std::uint64_t launches, std::uint64_t barriers,
                  std::uint64_t observed_m, const UINT64* ticks, double scale) {
    AcquireSRWLockExclusive(&row_lock);
    if(row_count < 16384) {
        auto& row=rows[row_count++];row.key=key;row.ticket=ticket;row.epoch=sample_epoch;row.frame=sample_frame;
        row.pairs=pairs;row.launches=launches;row.barriers=barriers;row.observed_m=observed_m;
        memcpy(row.ticks,ticks,sizeof(row.ticks));row.scale=scale;
    } else ++dropped;
    ReleaseSRWLockExclusive(&row_lock);
}
inline void Flush() {
    // No file I/O runs while the Core's timing/recording lock is held.
    AcquireSRWLockExclusive(&flush_lock);
    for(;;) {
    AcquireSRWLockExclusive(&row_lock);
    if(flushed==row_count||file==INVALID_HANDLE_VALUE){ReleaseSRWLockExclusive(&row_lock);break;}
    const auto row=rows[flushed];ReleaseSRWLockExclusive(&row_lock);
    const auto& key=row.key;const auto* ticks=row.ticks;const double scale=row.scale;
    char text[1600];
    const int n = snprintf(text, sizeof(text),
        "{\"mode\":\"%s\",\"pid\":%lu,\"run_start_ticks\":%llu,\"ticket\":%llu,\"context\":%llu,\"width\":%u,\"height\":%u,"
        "\"epoch\":%llu,\"frame\":%llu,\"warmup\":%s,\"gpu_complete\":true,\"valid\":true,"
        "\"M\":%llu,\"replaced_pairs\":%llu,\"replacement_launches\":%llu,\"uav_barriers\":%llu,"
        "\"encoding_ms\":%.9f,\"feature18_ms\":%.9f,\"composition_ms\":%.9f,\"sr_ms\":%.9f,"
        "\"nr_chain_ms\":%.9f,\"nr_sr_ms\":%.9f,\"boundary\":\"owned-Core-EncodeBegin-through-CompositeEnd\"}\n",
        mode, (unsigned long)process_id, (unsigned long long)run_start_ticks,
        (unsigned long long)row.ticket, (unsigned long long)key.context, key.width, key.height,
        (unsigned long long)row.epoch, (unsigned long long)row.frame,
        (!row.epoch || row.frame < warmup) ? "true" : "false",
        (unsigned long long)row.observed_m, (unsigned long long)row.pairs, (unsigned long long)row.launches, (unsigned long long)row.barriers,
        double(ticks[1]-ticks[0])*scale, double(ticks[3]-ticks[2])*scale,
        double(ticks[4]-ticks[3])*scale, double(ticks[6]-ticks[5])*scale,
        double(ticks[4]-ticks[0])*scale, double(ticks[6]-ticks[0])*scale);
    DWORD written=0;
    if(n<=0||n>=int(sizeof(text))||!WriteFile(file,text,DWORD(n),&written,nullptr)||written!=DWORD(n))break;
    ++flushed;
    }
    ReleaseSRWLockExclusive(&flush_lock);
}
}
extern "C" __declspec(dllexport) unsigned __cdecl NR_NVFP4_TestReadyV1() {
    const auto status = nr::nvfp4::GetStatus();
    static std::uint64_t previous_m=UINT64_MAX,previous_pairs=UINT64_MAX;
    static nr::nvfp4::State previous_state=nr::nvfp4::State::Disabled;
    if(status.observed_m!=previous_m||status.replaced_pairs!=previous_pairs||status.state!=previous_state){
        Log("[nvfp4-test] readiness state=%u M=%llu pairs=%llu originalLaunches=%llu reason=%s",
            unsigned(status.state),(unsigned long long)status.observed_m,(unsigned long long)status.replaced_pairs,
            (unsigned long long)status.original_launches,status.reason);
        previous_m=status.observed_m;previous_pairs=status.replaced_pairs;previous_state=status.state;
    }
    if(status.may_have_recorded_failure || !status.observed_m) return 0;
    return nr_nvfp4_test::candidate ? unsigned(status.replaced_pairs == 8) : unsigned(status.original_launches > 0);
}
extern "C" __declspec(dllexport) unsigned __cdecl NR_NVFP4_TestBeginEpochV1() {
    if(!NR_NVFP4_TestReadyV1()) return 0;
    if(InterlockedCompareExchange(&nr_nvfp4_test::epoch_started,1,0)!=0)return 0;
    InterlockedExchange(&nr_nvfp4_test::epoch_requested,1);return 1;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--build', type=Path)
    p.add_argument('--expansion-build', type=Path)
    p.add_argument('--expansion-variant', type=int, choices=range(3), default=0)
    p.add_argument('--assets', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mode', choices=('passthrough', 'optimized', 'fused', 'two'), required=True)
    p.add_argument('--variant', type=int, choices=range(4), default=0)
    p.add_argument('--accepted-m', default='640,960')
    p.add_argument('--warmup', type=int, default=128)
    a = p.parse_args()
    if not 0 <= a.warmup <= 10000:
        p.error('Warmup must be between 0 and 10000')
    a.stage, a.output, a.assets = a.stage.resolve(), a.output.resolve(), a.assets.resolve()
    if a.output.exists() or a.output == a.stage or a.output.is_relative_to(a.stage):
        raise ValueError('Choose a fresh output outside the input stage')
    manifest = json.loads((a.stage / 'source-manifest.json').read_text(encoding='utf-8-sig'))
    for name, expected in manifest['files'].items():
        if sha(a.stage / name) != expected:
            raise ValueError('Generated input stage changed: ' + name)
    candidate = a.mode != 'passthrough'
    configure = 'config.mode = nr::nvfp4::Mode::Passthrough;'
    receipt = None
    if candidate:
        if not a.build:
            p.error('A candidate needs its explicit kernel build')
        a.build = a.build.resolve()
        receipt = json.loads((a.build / 'kernel-build.json').read_text(encoding='utf-8-sig'))
        hashes = {x['file']: x['sha256'] for x in receipt['artifacts']}
        for name, expected in hashes.items():
            if sha(a.build / name) != expected:
                raise ValueError('Kernel receipt mismatch: ' + name)
        if a.mode in ('fused', 'two') and a.variant:
            raise ValueError('Only fused variant 0 currently exists')
        suffix = ('128x128x128', '128x64x128', '128x64x256', '128x64x128_pingpong')[a.variant]
        kernel = 'nr_nvfp4_contract_publish_v1' if a.mode in ('fused', 'two') else 'nr_nvfp4_contract_v1_' + suffix
        cubin = 'nr_nvfp4_contract_fused_sm120.cubin' if a.mode in ('fused', 'two') else 'nr_nvfp4_contract_sm120.cubin'
        shapes = [int(x) for x in a.accepted_m.split(',')]
        if not shapes or len(shapes) > 16 or any(x < 1 or x > 16384 for x in shapes):
            raise ValueError('Invalid explicit tested shapes')
        configure = '\n    '.join([
            'static const uint32_t shapes[] = {' + ','.join(map(str, shapes)) + '};',
            'static const nr::nvfp4::OptimizedAcceptance accepted = [] {',
            '    nr::nvfp4::OptimizedAcceptance value;',
            '    value.directory = L' + cpp_string(a.build) + ';',
            '    value.cubin_sha256 = ' + cpp_string(hashes[cubin]) + ';',
            '    value.builder_sha256 = ' + cpp_string(hashes['nr_nvfp4_params.dll']) + ';',
            '    value.build_id = ' + cpp_string(receipt['abi_fingerprint']) + ';',
            '    value.kernel_id = ' + cpp_string(kernel) + ';',
            '    value.variant = ' + str(a.variant) + ';',
            '    value.tested_m = shapes; value.tested_m_count = sizeof(shapes)/sizeof(shapes[0]);',
            '    value.allow_auto = false; value.fused_publish = ' + str(a.mode in ('fused', 'two')).lower() + ';',
            '    return value;',
            '}();',
            'config.mode = nr::nvfp4::Mode::Optimized; config.optimized_acceptance = &accepted;',
        ])
    expansion_receipt = None
    if a.mode == 'two':
        if not a.expansion_build:
            p.error('The two-launch profile requires --expansion-build')
        a.expansion_build = a.expansion_build.resolve()
        expansion_receipt = json.loads((a.expansion_build / 'expansion-build.json').read_text(encoding='utf-8-sig'))
        artifacts = {x['file']: x['sha256'] for x in expansion_receipt['artifacts']}
        for name, expected in artifacts.items():
            if sha(a.expansion_build / name) != expected:
                raise ValueError('Expansion receipt mismatch: ' + name)
        kernel = ('nr_pack_expand_v1_m64n128', 'nr_pack_expand_v1_m32n128', 'nr_pack_expand_v1_m128n128')[a.expansion_variant]
        expansion_config = '\n    '.join([
            'static const nr::nvfp4::ExpansionAcceptance expansion = [] {',
            '    nr::nvfp4::ExpansionAcceptance value;',
            '    value.directory = L' + cpp_string(a.expansion_build) + ';',
            '    value.cubin_sha256 = ' + cpp_string(artifacts['nr_nvfp4_pack_expansion_sm120.cubin']) + ';',
            '    value.builder_sha256 = ' + cpp_string(artifacts['nr_nvfp4_expansion_params.dll']) + ';',
            '    value.build_id = ' + cpp_string(expansion_receipt['abi_fingerprint']) + ';',
            '    value.kernel_id = ' + cpp_string(kernel) + '; value.variant = ' + str(a.expansion_variant) + '; return value;',
            '}();',
        ])
        configure = expansion_config + '\n    ' + configure.replace('    return value;', '    value.expansion = &expansion; return value;')
    core = a.output / 'core'
    shutil.copytree(a.stage / 'core', core, ignore=shutil.ignore_patterns('build', '.git', '__pycache__'))
    values = {'CANDIDATE': str(candidate).lower(), 'WARMUP': str(a.warmup), 'MODE': cpp_string(a.mode),
              'ASSETS': cpp_string(a.assets), 'SAMPLES': cpp_string(core / 'build/nvfp4-chain-samples.jsonl'),
              'CONFIGURE': configure}
    header = HEADER
    for key, value in values.items():
        header = header.replace('@' + key + '@', value)
    (core / 'src/nvfp4_chain_experiment.h').write_text(header, encoding='utf-8')
    timing = core / 'src/nr_gpu_timing.inl'
    replace_once(timing, '#include "nr_gpu_timing.h"', '#include "nr_gpu_timing.h"\n#include "nvfp4_chain_experiment.h"')
    replace_once(timing, '        bool finished=false, valid=false;',
                 '        bool finished=false, valid=false;\n        bool test_attached=false;\n'
                 '        Key test_key{};\n'
                 '        std::uint64_t test_epoch=0,test_frame=0,test_pairs=0,test_launches=0,test_barriers=0,test_m=0;')
    replace_once(timing, '        s->epoch=epoch_;s->lifetime.Record(token);',
                 '        s->epoch=epoch_;s->test_key=key;s->lifetime.Record(token);')
    replace_once(timing, '                    stats_.available=true;',
                 '                    stats_.available=true;\n'
                 '                    if(s.test_attached)nr_nvfp4_test::Write(s.test_key,s.ticket,s.test_epoch,s.test_frame,\n'
                 '                        s.test_pairs,s.test_launches,s.test_barriers,s.test_m,values,scale);')
    replace_once(timing, 'public:\n    Ticket Begin', '''public:
    void AttachTest(Ticket ticket,std::uint64_t token,std::uint64_t context,const nr::nvfp4::Snapshot& status) {
        AcquireSRWLockExclusive(&lock_);
        const auto frame=nr_nvfp4_test::frame++;
        if(auto* s=Find(ticket);s&&!s->finished&&!s->test_attached&&s->lifetime.token==token&&s->test_key.context==context){
            s->test_attached=true;s->test_epoch=nr_nvfp4_test::epoch;s->test_frame=frame;
            s->test_pairs=status.replaced_pairs;s->test_launches=status.replacement_launches;
            s->test_barriers=status.uav_barriers;s->test_m=status.observed_m;
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    Ticket Begin''')
    replace_once(timing, 'NrGpuTimingRuntime g_gpu_timing;', '''NrGpuTimingRuntime g_gpu_timing;
extern "C" __declspec(dllexport) unsigned __cdecl NR_NVFP4_TestPollV1() {
    const auto stats=g_gpu_timing.Read();nr_nvfp4_test::Flush();
    if(nr_nvfp4_test::dropped)Log("[nvfp4-test] timing sink overflow=%u",nr_nvfp4_test::dropped);
    return unsigned(stats.completed);
}''')
    integration = core / 'src/nr_nvfp4_core.inl'
    replace_once(integration, 'NrSlot *slot=nullptr) {',
                 'NrSlot *slot=nullptr,nr_gpu_timing::Ticket test_ticket=0) {')
    replace_once(core / 'src/nr_before_sr.cpp', '        }, slot);', '        }, slot, substitution.timing);')
    replace_once(integration, '        const bool initialized = nr::nvfp4::Init(config);',
                 '        nr_nvfp4_test::Configure(config);\n        const bool initialized = nr::nvfp4::Init(config);')
    replace_once(integration, '        result=call(); // Exactly one owned provider call; no FFN retry here.', '''        if(phase==nr::nvfp4::Phase::Evaluate&&InterlockedExchange(&nr_nvfp4_test::epoch_requested,0)){
            ++nr_nvfp4_test::epoch;nr_nvfp4_test::frame=0;
            SetInt(context.params,"DLSSNR.Reset",1);context.reset_retry_pending=true;
            context.diagnostic_reset_requested=true;
            if(slot){slot->capture_params.reset=true;slot->suppress_transfer_this_frame=g_config.suppress_reset_frame;}
            Log("[nvfp4-test] controlled NR epoch=%llu reset requested",(unsigned long long)nr_nvfp4_test::epoch);
        }
        result=call(); // Exactly one owned provider call; no FFN retry here.''')
    replace_once(integration, '        failed_after_recording=scope.MayHaveRecordedFailure();',
                 '        failed_after_recording=scope.MayHaveRecordedFailure();\n'
                 '        if(phase==nr::nvfp4::Phase::Evaluate)g_gpu_timing.AttachTest(test_ticket,info.recording_token,info.context_id,scope.Status());')
    # Normal source/Auto is untouched. The private INI still requests precision
    # so that the existing mode-2 owned scope and timing boundaries execute.
    ini = core / 'config/nr_before_sr.ini'
    replace_once(ini, 'NrPrecisionMode=0', 'NrPrecisionMode=1')
    replace_once(ini, 'GpuTiming=0', 'GpuTiming=1')
    script = core / 'scripts/build.ps1'
    build_text = script.read_text(encoding='utf-8-sig')
    # Only English is needed for the controlled host; preserve source identity.
    anchor = '$AddonOutputs = @()'
    if build_text.count(anchor) != 1:
        raise ValueError('Private build language anchor missing')
    build_text = build_text.replace(anchor, "$AddonBuilds = @($AddonBuilds | Where-Object { $_.Language -eq 'English' })\n" + anchor)
    script.write_text(build_text, encoding='utf-8-sig')
    all_files = {x.relative_to(a.output).as_posix(): sha(x) for x in sorted(core.rglob('*')) if x.is_file()}
    changed = {name: digest for name, digest in all_files.items() if manifest['files'].get(name) != digest}
    report = {'schema': 1, 'kind': 'private compiled laboratory profile', 'production_auto_qualified': False,
              'source_commit': manifest['nvfp4']['source_commit'], 'parent_manifest_sha256': sha(a.stage / 'source-manifest.json'),
              'generator_sha256': sha(Path(__file__)), 'mode': a.mode, 'warmup': a.warmup,
              'kernel_receipt': receipt, 'expansion_receipt': expansion_receipt,
              'changed_files': changed, 'files': all_files,
              'timing_boundary': 'NR chain = EncodeBegin through CompositeEnd, after exact fence and successful recording reset',
              'graphics_executed': False, 'deployed_to_game': False}
    (a.output / 'experiment-source-manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output': str(a.output), 'mode': a.mode, 'changed_files': list(changed), 'production_auto_qualified': False}))


if __name__ == '__main__':
    main()

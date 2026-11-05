"""Actual Execute/fail-stop/Com functions with explicit D3D/Win32 test doubles.

The subprocess is real; this proves failure/unwind behavior, not GPU completion.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using DWORD=std::uint32_t;
using UINT64=std::uint64_t;
using HRESULT=std::int32_t;
using SIZE_T=std::size_t;
using HANDLE=void*;
constexpr HRESULT S_OK=0, E_FAIL=static_cast<HRESULT>(0x80004005u);
constexpr DWORD WAIT_OBJECT_0=0, WAIT_TIMEOUT=258, WAIT_FAILED=0xffffffffu;
constexpr bool FAILED(HRESULT h){return h<0;}
constexpr unsigned D3D12_MESSAGE_SEVERITY_WARNING=2, D3D12_MESSAGE_SEVERITY_ERROR=1;
struct D3D12_MESSAGE {unsigned Severity;const char* pDescription;};
std::string mode;
bool pending=false;
UINT64 target=0, completed=0;
DWORD last_error=0;
DWORD GetLastError(){return last_error;}
DWORD WaitForSingleObject(HANDLE,DWORD milliseconds){
    assert(milliseconds==30000);
    if(mode=="timeout" || mode=="terminate-api-failure")return WAIT_TIMEOUT;
    if(mode=="wait-failed"){last_error=5;return WAIT_FAILED;}
    if(mode=="abandoned")return 0x80;
    if(mode=="false-signal")return WAIT_OBJECT_0;
    if(mode=="removed-signal"){completed=std::numeric_limits<UINT64>::max();return WAIT_OBJECT_0;}
    completed=target;pending=false;return WAIT_OBJECT_0;
}
struct ID3D12CommandList {};
struct List:ID3D12CommandList {
    HRESULT Close(){return mode=="close-error"?E_FAIL:S_OK;}
    HRESULT Reset(void*,void*){return S_OK;}
} list_object;
struct Fence {
    HRESULT SetEventOnCompletion(UINT64,HANDLE){return mode=="event-error"?E_FAIL:S_OK;}
    UINT64 GetCompletedValue(){last_error=17;return completed;}
} fence_object;
struct Queue {
    void ExecuteCommandLists(unsigned,ID3D12CommandList**){pending=true;}
    HRESULT Signal(Fence*,UINT64 value){target=value;return mode=="signal-error"?E_FAIL:S_OK;}
} queue_object;
struct Allocator {HRESULT Reset(){return mode=="reset-error"?E_FAIL:S_OK;}} allocator_object;
struct Device {HRESULT GetDeviceRemovedReason(){return mode=="device-error"?E_FAIL:S_OK;}} device_object;
struct Messages {
    UINT64 GetNumStoredMessages(){return mode=="debug-error"?1:0;}
    HRESULT GetMessage(UINT64,D3D12_MESSAGE* message,SIZE_T* bytes){
        if(message)*message={D3D12_MESSAGE_SEVERITY_ERROR,"fixture debug failure"};
        *bytes=sizeof(D3D12_MESSAGE);return S_OK;
    }
    void ClearStoredMessages(){}
} messages_object;
template<class T>struct Ref {T* p;T* operator->(){return p;}};
struct Resource {
    void Release(){
        std::fputs(pending?"FINAL_RELEASE_INFLIGHT\n":"RELEASE_SAFE\n",stderr);
        std::fflush(stderr);
        if(pending)std::_Exit(99); // Models the observed secondary debug failure.
    }
} resource;
// Compile the real Windows branch with explicit test doubles, including on
// Linux. A real subprocess exits; no Win32 execution is claimed by this test.
#ifndef _WIN32
#define _WIN32 1
#endif
HANDLE GetCurrentProcess(){return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));}
int TerminateProcess(HANDLE process,unsigned code){
    assert(process==GetCurrentProcess());
    std::fprintf(stderr,"TERMINATE_PROCESS_SELF exit_code=%u\n",code);
    std::fflush(stderr);
    if(mode=="terminate-api-failure"){last_error=5;return 0;}
    std::_Exit(static_cast<int>(code));
}
'''
MAIN = r'''
int main(int argc,char** argv){
    assert(argc==2);mode=argv[1];
    try {
        Com<Resource> texture;texture.p=&resource;
        Gpu gpu;
        gpu.Execute();
        std::puts("EXECUTE_COMPLETE");
        return 0;
    }catch(const std::exception& error){
        std::fprintf(stderr,"CAUGHT_ORIGINAL %s\n",error.what());
        return 2;
    }
}
'''


class InflightFailureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which(os.environ.get('CXX', 'g++'))
        if not cls.compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        spec = importlib.util.spec_from_file_location('post_extract', ROOT / 'scripts/extract-post-gpu.py')
        extractor = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(extractor)
        cls.extract = staticmethod(extractor.function)
        cls.source = (ROOT / 'tests/post_scaled_work_gpu.cpp.in').read_text(encoding='utf-8-sig')
        cls.temp = tempfile.TemporaryDirectory(prefix='post-gpu-inflight-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.current = cls.build('actual', cls.source)

    @classmethod
    def build(cls, name, source):
        text = STUB
        text += cls.extract(source, 'void Require(') + '\n'
        text += cls.extract(source, 'void Check(') + '\n'
        text += cls.extract(source, 'template<class T> struct Com {') + ';\n'
        text += 'namespace post_gpu_diagnostics {\nstd::atomic<unsigned> case_id{61}, submission{240};\n'
        text += 'void Phase(const char*){}\n'
        text += cls.extract(source, '[[noreturn]] void InFlightFailure(') + '\n}\n'
        text += '''struct Gpu {
            Ref<List> list{&list_object}; Ref<Queue> queue{&queue_object};
            Ref<Fence> fence{&fence_object}; Ref<Allocator> allocator{&allocator_object};
            Ref<Device> device{&device_object}; Ref<Messages> messages{&messages_object};
            UINT64 submitted=240;HANDLE completion=nullptr;
        '''
        text += cls.extract(source, 'void Execute()') + '\n};\n' + MAIN
        cpp = cls.directory / (name + '.cpp')
        exe = cls.directory / (name + ('.exe' if os.name == 'nt' else ''))
        cpp.write_text(text, encoding='utf-8')
        result = subprocess.run([cls.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                 str(cpp), '-o', str(exe)], capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return exe

    def run_mode(self, binary, mode):
        return subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)

    def assert_fail_stop(self, binary, mode, diagnostic):
        result = self.run_mode(binary, mode)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('INFLIGHT_FAILURE ', result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertIn('case=61 submission=241 ', result.stderr)
        self.assertIn('target=241 ', result.stderr)
        self.assertIn('hresult=0x80004005' if mode in ('signal-error', 'event-error')
                      else 'hresult=0x00000000', result.stderr)
        self.assertIn('completed=18446744073709551615' if mode == 'removed-signal'
                      else 'completed=0 ', result.stderr)
        self.assertIn('fail_stop_no_unwind=1 exit_code=1', result.stderr)
        self.assertIn('TERMINATE_PROCESS_SELF exit_code=1', result.stderr)
        if mode == 'terminate-api-failure':
            self.assertIn('FAIL_STOP_TERMINATE_FAILED win32_error=5 fallback_crt_exit=1', result.stderr)
        else:
            self.assertNotIn('FAIL_STOP_TERMINATE_FAILED', result.stderr)
        self.assertNotIn('RELEASE_', result.stderr)
        self.assertNotIn('EXECUTE_COMPLETE', result.stdout)

    def test_real_execute_keeps_pending_resources_alive_on_failure(self):
        for mode, diagnostic in (
            ('signal-error', 'operation=Signal'),
            ('event-error', 'operation=SetEventOnCompletion'),
            ('timeout', 'wait_observed=1 wait_result=0x00000102 win32_error=0'),
            ('wait-failed', 'wait_observed=1 wait_result=0xffffffff win32_error=5'),
            ('abandoned', 'wait_observed=1 wait_result=0x00000080'),
            ('false-signal', 'operation=fence-completion'),
            ('removed-signal', 'completed=18446744073709551615'),
            ('terminate-api-failure', 'wait_observed=1 wait_result=0x00000102'),
        ):
            with self.subTest(mode=mode):
                self.assert_fail_stop(self.current, mode, diagnostic)

    def test_completed_and_pre_submit_failures_keep_normal_unwind(self):
        result = self.run_mode(self.current, 'success')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('EXECUTE_COMPLETE', result.stdout)
        self.assertIn('RELEASE_SAFE', result.stderr)
        for mode in ('close-error', 'device-error', 'debug-error', 'reset-error'):
            with self.subTest(mode=mode):
                result = self.run_mode(self.current, mode)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn('RELEASE_SAFE', result.stderr)
                self.assertIn('CAUGHT_ORIGINAL ', result.stderr)
                self.assertNotIn('INFLIGHT_FAILURE', result.stderr)

    def test_old_throw_path_reproduces_secondary_release_failure(self):
        old = '''if (wait_result != WAIT_OBJECT_0)
            post_gpu_diagnostics::InFlightFailure("WaitForSingleObject", S_OK,
                submitted, completed, wait_result, wait_error, true);'''
        self.assertEqual(self.source.count(old), 1)
        mutation = self.source.replace(old, '''Require(wait_result == WAIT_OBJECT_0,
                "GPU completion timed out");''', 1)
        result = self.run_mode(self.build('old-wait-throw', mutation), 'timeout')
        self.assertEqual(result.returncode, 99, result.stderr)
        self.assertIn('FINAL_RELEASE_INFLIGHT', result.stderr)
        self.assertNotIn('CAUGHT_ORIGINAL', result.stderr)

    def test_success_exit_mutation_is_rejected(self):
        original = 'TerminateProcess(GetCurrentProcess(), 1);'
        self.assertEqual(self.source.count(original), 1)
        mutation = self.source.replace(original, 'TerminateProcess(GetCurrentProcess(), 0);', 1)
        binary = self.build('wrong-exit', mutation)
        with self.assertRaises(AssertionError):
            self.assert_fail_stop(binary, 'timeout', 'operation=WaitForSingleObject')

    def test_native_termination_bypass_mutation_is_rejected(self):
        original = 'TerminateProcess(GetCurrentProcess(), 1);'
        self.assertEqual(self.source.count(original), 1)
        binary = self.build('skip-native-termination', self.source.replace(original, '', 1))
        with self.assertRaises(AssertionError):
            self.assert_fail_stop(binary, 'timeout', 'operation=WaitForSingleObject')


if __name__ == '__main__':
    unittest.main()

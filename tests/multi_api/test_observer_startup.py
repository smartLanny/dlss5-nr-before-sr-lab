"""Actual worker control flow with deterministic loader/install failure inputs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ObserverStartup(unittest.TestCase):
    def test_worker_terminal_outcomes(self):
        cxx = os.environ.get('CXX')
        if not cxx:
            self.skipTest('CXX required for executable worker fault test')
        text = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        start = text.index('DWORD WINAPI HookWorker(void *) {')
        end = text.index('\n}\n', start) + 3
        worker = text[start:end]
        prelude = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#define WINAPI
using DWORD=unsigned long; using HMODULE=void*; using LPCWSTR=const wchar_t*;
constexpr int GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS=1, GET_MODULE_HANDLE_EX_FLAG_PIN=2, MAX_PATH=260;
std::atomic<bool> g_observer_stopping{false}, g_observer_startup_failed{false}, g_evaluate_hooks_ready{false};
int sleeps=0, installs=0, available_after=3, stop_after=-1;
bool pin_ok=true, install_ok=true;
bool GetModuleHandleExW(int,LPCWSTR,HMODULE*) { return pin_ok; }
void GetModuleFileNameW(HMODULE,wchar_t*,int) {}
template<class... T>void Log(const char*,T...) {}
struct LoadedNgxModule { HMODULE handle; LoadedNgxModule(LPCWSTR):handle(sleeps>=available_after?(void*)1:nullptr){} };
bool InstallHooks(HMODULE) { ++installs; if(install_ok)g_evaluate_hooks_ready=true; return install_ok; }
void Sleep(int) { ++sleeps; if(sleeps==stop_after)g_observer_stopping=true; }
void Reset() { g_observer_stopping=false;g_observer_startup_failed=false;g_evaluate_hooks_ready=false;
    sleeps=installs=0;available_after=3;stop_after=-1;pin_ok=install_ok=true; }
'''
        cases = r'''
int main() {
    HookWorker(nullptr); assert(sleeps==3 && installs==1 && g_evaluate_hooks_ready && !g_observer_startup_failed);
    Reset(); install_ok=false; HookWorker(nullptr); assert(installs==1 && g_observer_startup_failed && !g_evaluate_hooks_ready);
    Reset(); pin_ok=false; HookWorker(nullptr); assert(installs==0 && sleeps==0 && g_observer_startup_failed);
    Reset(); available_after=25000; HookWorker(nullptr); assert(sleeps==24000 && installs==0 && g_observer_startup_failed);
    Reset(); stop_after=2; HookWorker(nullptr); assert(sleeps==2 && installs==0 && !g_evaluate_hooks_ready);
    Reset(); g_observer_stopping=true; HookWorker(nullptr); assert(sleeps==0 && installs==0);
    puts("actual worker delayed module, install failure, pin failure, deadline, stop-before/during wait passed");
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / 'worker.cpp'
            exe = Path(tmp) / 'worker.exe'
            cpp.write_text(prelude + worker + cases, encoding='utf-8')
            subprocess.run([cxx, '-std=c++20', '-static', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

"""Execute the generated promotion function under partial-hook failure.

The function under test is extracted verbatim, not reimplemented. OS/hook APIs
are substitutes; this proves the promotion admission contract, not real hooks.
"""
import os
from contextlib import nullcontext
from pathlib import Path
import subprocess
import tempfile
import unittest


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
        end += 1
    return text[start:end]


@unittest.skipUnless(os.environ.get('CARRIER_COMPLETION_SOURCE'), 'requires actual generated carrier')
class ObserverPromotion(unittest.TestCase):
    def test_partial_observer_cannot_promote_evaluate(self):
        root = Path(os.environ['CARRIER_COMPLETION_SOURCE'])
        source = (root / 'carrier/src/dlss5-bridge.cpp').read_text(encoding='utf-8')
        actual = function(source, 'static void PromoteObservedLayers()')
        harness = r'''
#include <cstdio>
#include <cstdint>
#include <atomic>
using LONG = long;
using HMODULE = void *;
struct Hook { bool active = false; };
struct Layer { HMODULE mod{}; Hook create, release, eval, eval_c; };
struct Detours { void *eval{}, *eval_c{}; };
static std::atomic<bool> g_carrier_bridge_armed{true};
static LONG g_layer_count = 1;
static Layer g_layer[1];
static Detours kDetour[1];
static int g_hook_cs, installs, lockDepth;
void EnterCriticalSection(int*) { ++lockDepth; }
void LeaveCriticalSection(int*) { --lockDepth; }
void *GetProcAddress(HMODULE, const char*) { return reinterpret_cast<void *>(uintptr_t(0x1000)); }
bool HookInstall(Hook& hook, void*, void*) { ++installs; hook.active = true; return true; }
void Log(const char*, ...) {}
'''
        harness += actual
        harness += r'''
int main() {
  unsigned checks = 0;
  for (int create = 0; create != 2; ++create) {
    for (int release = 0; release != 2; ++release) {
      g_layer[0] = Layer{};
      g_layer[0].mod = reinterpret_cast<HMODULE>(uintptr_t(0x2000));
      g_layer[0].create.active = create != 0;
      g_layer[0].release.active = release != 0;
      installs = lockDepth = 0;
      PromoteObservedLayers();
      const int expected = create && release ? 2 : 0;
      if (installs != expected || lockDepth != 0) {
        std::fprintf(stderr, "PARTIAL_OBSERVER_PROMOTION create=%d release=%d installs=%d expected=%d lock=%d\n",
                     create, release, installs, expected, lockDepth);
        return 7;
      }
      ++checks;
    }
  }
  std::printf("PASS: %u actual promotion cases; incomplete pairs never enable Evaluate\n", checks);
}
'''
        compiler = os.environ.get('CXX', 'clang++')
        fixed = os.environ.get('CARRIER_TEST_OUTPUT')
        directory = nullcontext(fixed) if fixed else tempfile.TemporaryDirectory(prefix='carrier-promotion-')
        with directory as tmp:
            path = Path(tmp)
            cpp = path / 'promotion.cpp'
            exe = path / 'promotion.exe'
            cpp.write_text(harness, encoding='utf-8')
            build = subprocess.run([compiler, '-std=c++17', '-O2', str(cpp), '-o', str(exe)],
                                   text=True, capture_output=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(exe)], text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main(verbosity=2)

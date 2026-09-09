"""CPU-only private-generator isolation and actual timing lifetime boundaries.

The optional C++ test compiles the production timing POD and the generator's
actual sample writer/epoch exports against CPU stubs; no D3D/CUDA is loaded.
Set NR_NVFP4_TEST_CXX to a C++17 compiler if it is not on PATH.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts/prepare-nvfp4-chain-experiment.py"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiler():
    explicit = os.environ.get("NR_NVFP4_TEST_CXX")
    if explicit:
        return explicit
    return shutil.which("clang++") or shutil.which("g++")


def cpp_definition(text, anchor):
    """Select an actual simple C++ definition; no reimplementation of its logic."""
    start = text.index(anchor)
    opening = text.index("{", start)
    depth = 1
    for index in range(opening + 1, len(text)):
        depth += (text[index] == "{") - (text[index] == "}")
        if depth == 0:
            return text[start:index + 1]
    raise ValueError("Unterminated C++ definition: " + anchor)


class ChainExperimentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.stage = self.root / "normal-stage"
        self.output = self.root / "private-stage"
        names = ("src/nr_before_sr.cpp", "src/nr_gpu_timing.h", "src/nr_gpu_timing.inl",
                 "src/nr_nvfp4_core.inl", "src/nvfp4/policy_registry.h",
                 "config/nr_before_sr.ini", "scripts/build.ps1")
        for name in names:
            destination = self.stage / "core" / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        self.manifest = {"nvfp4": {"source_commit": "0" * 40}, "files": {
            path.relative_to(self.stage).as_posix(): digest(path)
            for path in (self.stage / "core").rglob("*") if path.is_file()}}
        (self.stage / "source-manifest.json").write_text(json.dumps(self.manifest))
        self.original = {str(path.relative_to(self.stage)): path.read_bytes()
                         for path in self.stage.rglob("*") if path.is_file()}

    def tearDown(self):
        self.temporary.cleanup()

    def generate(self, *arguments, output=None):
        return subprocess.run([sys.executable, str(GENERATOR), "--stage", str(self.stage),
                               "--assets", str(self.root / "unused-assets"), "--output", str(output or self.output),
                               "--mode", "passthrough", *arguments], capture_output=True, text=True)

    def assert_parent_unchanged(self):
        self.assertEqual(self.original, {str(path.relative_to(self.stage)): path.read_bytes()
                                         for path in self.stage.rglob("*") if path.is_file()})

    def test_successful_private_generation_preserves_normal_stage_and_policy(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_parent_unchanged()
        receipt = json.loads((self.output / "experiment-source-manifest.json").read_text())
        self.assertFalse(receipt["production_auto_qualified"])
        self.assertFalse(receipt["graphics_executed"])
        self.assertEqual(receipt["parent_manifest_sha256"], digest(self.stage / "source-manifest.json"))
        self.assertEqual(receipt["generator_sha256"], digest(GENERATOR))
        self.assertEqual(receipt["files"], {
            path.relative_to(self.output).as_posix(): digest(path)
            for path in (self.output / "core").rglob("*") if path.is_file()})
        changed = {name: value for name, value in receipt["files"].items()
                   if self.manifest["files"].get(name) != value}
        self.assertEqual(receipt["changed_files"], changed)
        self.assertEqual(digest(self.output / "core/src/nvfp4/policy_registry.h"),
                         self.manifest["files"]["core/src/nvfp4/policy_registry.h"])

    def test_changed_source_receipt_is_rejected_before_output_creation(self):
        path = self.stage / "core/config/nr_before_sr.ini"
        path.write_bytes(path.read_bytes() + b"\nchanged-after-receipt\n")
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Generated input stage changed", result.stderr)
        self.assertFalse(self.output.exists())

    def test_existing_output_is_not_overwritten(self):
        self.output.mkdir()
        sentinel = self.output / "keep.bin"
        sentinel.write_bytes(b"previous evidence")
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_bytes(), b"previous evidence")
        self.assert_parent_unchanged()

    def test_output_inside_normal_stage_is_rejected_without_parent_mutation(self):
        result = self.generate(output=self.stage / "nested-private-stage")
        self.assertNotEqual(result.returncode, 0)
        self.assert_parent_unchanged()

    def test_corrupt_kernel_receipt_cannot_enter_private_candidate(self):
        build = self.root / "candidate-build"
        build.mkdir()
        (build / "candidate.cubin").write_bytes(b"not the receipted bytes")
        (build / "kernel-build.json").write_text(json.dumps({
            "artifacts": [{"file": "candidate.cubin", "sha256": "0" * 64}]}))
        result = self.generate("--mode", "fused", "--build", str(build))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Kernel receipt mismatch", result.stderr)
        self.assertFalse(self.output.exists())
        self.assert_parent_unchanged()

    def test_reference_uses_upstream_assets_without_an_optimized_build(self):
        result = self.generate("--mode", "reference", "--accepted-m", "960")
        self.assertEqual(result.returncode, 0, result.stderr)
        header = (self.output / "core/src/nvfp4_chain_experiment.h").read_text()
        self.assertIn("config.mode = nr::nvfp4::Mode::UpstreamReference;", header)
        self.assertIn("inline constexpr bool candidate = true;", header)
        self.assertIn('inline constexpr const char* mode = "reference";', header)
        self.assertIn('asset_directory = L' + json.dumps(str(self.root / "unused-assets")), header)
        self.assertNotIn("OptimizedAcceptance", header)
        receipt = json.loads((self.output / "experiment-source-manifest.json").read_text())
        self.assertEqual(receipt["mode"], "reference")
        self.assertIsNone(receipt["kernel_receipt"])
        self.assertIsNone(receipt["expansion_receipt"])
        self.assertFalse(receipt["production_auto_qualified"])
        rejected_output = self.root / "fused-without-build"
        rejected = self.generate("--mode", "fused", output=rejected_output)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("A candidate needs its explicit kernel build", rejected.stderr)
        self.assertFalse(rejected_output.exists())
        self.assert_parent_unchanged()

    @unittest.skipUnless(compiler(), "Set NR_NVFP4_TEST_CXX for the C++ CPU boundary check")
    def test_actual_writer_epoch_warmup_and_recording_retirement(self):
        generated = self.generate("--mode", "reference")
        self.assertEqual(generated.returncode, 0, generated.stderr)
        timing = (self.output / "core/src/nr_gpu_timing.inl").read_text()
        actual_attach = cpp_definition(timing, "    void AttachTest(")
        actual_find = cpp_definition(timing, "    Sample *Find(")
        actual_sample = cpp_definition(timing, "    struct Sample {")
        header = (self.output / "core/src/nvfp4_chain_experiment.h").read_text()
        (self.root / "profile.h").write_text(header)
        shutil.copyfile(ROOT / "src/nr_gpu_timing.h", self.root / "nr_gpu_timing.h")
        program = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "nr_gpu_timing.h"
using LONG=long; using HANDLE=std::intptr_t; using DWORD=unsigned long; using UINT64=std::uint64_t;
using ULONGLONG=std::uint64_t;
DWORD GetCurrentProcessId(){return 1234;}
ULONGLONG GetTickCount64(){return 5678;}
constexpr HANDLE INVALID_HANDLE_VALUE=-1;
constexpr int FILE_APPEND_DATA=0,FILE_SHARE_READ=0,OPEN_ALWAYS=0,FILE_ATTRIBUTE_NORMAL=0;
struct SRWLOCK{bool held=false;};
#define SRWLOCK_INIT {}
int locks_held=0;bool write_under_row_lock=false;
void AcquireSRWLockExclusive(SRWLOCK* lock){lock->held=true;++locks_held;}
void ReleaseSRWLockExclusive(SRWLOCK* lock){lock->held=false;--locks_held;}
std::string recorded;
HANDLE CreateFileW(const wchar_t*,int,int,void*,int,int,void*){return 1;}
bool WriteFile(HANDLE,const char* text,DWORD size,DWORD* written,void*){if(locks_held!=1)write_under_row_lock=true;recorded.assign(text,size);*written=size;return true;}
LONG InterlockedCompareExchange(volatile LONG* where,LONG desired,LONG expected){const LONG old=*where;if(old==expected)*where=desired;return old;}
LONG InterlockedExchange(volatile LONG* where,LONG desired){const LONG old=*where;*where=desired;return old;}
void Log(const char*,...){}
namespace nr::nvfp4 {
enum class Mode{Disabled,UpstreamReference};
struct Config{const wchar_t*asset_directory=nullptr;bool diagnostic_passthrough=false;Mode mode=Mode::Disabled;};
enum class State{Disabled,Observing,Preparing,Ready,Fallback,Quarantined};
struct Snapshot{bool may_have_recorded_failure=false;unsigned observed_m=0,replaced_pairs=0,original_launches=0,replacement_launches=0,uav_barriers=0;State state=State::Disabled;char reason[192]{};};
Snapshot status;
Snapshot GetStatus(){return status;}
}
#include "profile.h"
struct ID3D12QueryHeap{};struct ID3D12Resource{};struct ID3D12Fence{};struct ID3D12Device{};
struct ActualAttachment {
using Ticket=nr_gpu_timing::Ticket;using Lifetime=nr_gpu_timing::Lifetime;using Key=nr_gpu_timing::Key;
@ACTUAL_SAMPLE@;
SRWLOCK lock_=SRWLOCK_INIT;Sample samples_[3]{};
@ACTUAL_FIND@
@ACTUAL_ATTACH@
};
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"line %d failed: %s\n",__LINE__,#c);return 1;}}while(0)
int main(){
    nr_gpu_timing::Lifetime lifetime;
    lifetime.Record(7);CHECK(lifetime.Submit(11,12,13));
    CHECK(!lifetime.Retirable(13)); // Completed GPU work remains replayable until Reset.
    lifetime.Reset(false);CHECK(lifetime.Retirable(13));
    CHECK(!lifetime.Retirable(12));CHECK(!lifetime.Retirable(UINT64_MAX));
    lifetime.Record(8);lifetime.Reset(false);CHECK(!lifetime.Retirable(0));
    lifetime.Record(9);lifetime.Reset(true);CHECK(lifetime.Retirable(0));
    lifetime.Record(10);CHECK(lifetime.Submit(11,12,13));CHECK(lifetime.Submit(11,12,14));
    CHECK(lifetime.replayed); // PollLocked rejects replayed timing even after retirement.
    CHECK(NR_NVFP4_TestReadyV1()==0);
    nr::nvfp4::status.observed_m=640;nr::nvfp4::status.replaced_pairs=7;CHECK(NR_NVFP4_TestReadyV1()==0);
    nr::nvfp4::status.replaced_pairs=8;CHECK(NR_NVFP4_TestReadyV1()==1);
    nr::nvfp4::status.may_have_recorded_failure=true;CHECK(NR_NVFP4_TestReadyV1()==0);
    nr::nvfp4::status.may_have_recorded_failure=false;
    CHECK(NR_NVFP4_TestBeginEpochV1()==1);CHECK(NR_NVFP4_TestBeginEpochV1()==0);
    InterlockedExchange(&nr_nvfp4_test::epoch_requested,0);CHECK(NR_NVFP4_TestBeginEpochV1()==0);
    nr_nvfp4_test::epoch=1;nr_nvfp4_test::frame=0;
    ActualAttachment actual;
    auto& first=actual.samples_[0];first.ticket=1;first.lifetime.Record(77);first.test_key.context=23;
    auto& second=actual.samples_[1];second.ticket=2;second.lifetime.Record(77);second.test_key.context=23;
    auto status=nr::nvfp4::status;status.replacement_launches=24;status.uav_barriers=24;
    actual.AttachTest(1,77,23,status);
    CHECK(first.test_attached&&first.test_frame==0&&first.test_pairs==8);
    status.replaced_pairs=7;actual.AttachTest(2,77,23,status);
    CHECK(first.test_frame==0&&first.test_pairs==8);CHECK(second.test_frame==1&&second.test_pairs==7);
    actual.AttachTest(1,77,23,status);CHECK(first.test_frame==0&&first.test_pairs==8);
    auto& other=actual.samples_[2];other.ticket=3;other.lifetime.Record(77);other.test_key.context=24;
    actual.AttachTest(3,77,23,status);CHECK(!other.test_attached);
    actual.AttachTest(3,99,24,status);CHECK(!other.test_attached);
    other.finished=true;actual.AttachTest(3,77,24,status);CHECK(!other.test_attached);
    nr::nvfp4::Config config;nr_nvfp4_test::Configure(config);
    CHECK(config.mode==nr::nvfp4::Mode::UpstreamReference);
    nr_gpu_timing::Key key{23,0,0,1920,1080};UINT64 ticks[]={100,110,120,150,180,190,250};
    unsigned warmup=0,measured=0;
    for(unsigned frame=0;frame<728;++frame){
        nr_nvfp4_test::Write(key,frame+1,1,frame,8,24,24,640,ticks,0.001);
        nr_nvfp4_test::Flush();
        if(recorded.find("\"warmup\":true")!=std::string::npos)++warmup;else ++measured;
    }
    CHECK(warmup==128);CHECK(measured==600);
    CHECK(recorded.find("\"nr_chain_ms\":0.080000000")!=std::string::npos);
    CHECK(recorded.find("\"feature18_ms\":0.030000000")!=std::string::npos);
    CHECK(recorded.find("\"frame\":727")!=std::string::npos);
    CHECK(recorded.find("\"pid\":1234")!=std::string::npos);
    CHECK(recorded.find("\"run_start_ticks\":5678")!=std::string::npos);
    CHECK(recorded.find("\"ticks\":[100,110,120,150,180,190,250]")!=std::string::npos);
    nr_nvfp4_test::Write(key,1,0,999,0,0,0,640,ticks,0.001);
    nr_nvfp4_test::Flush();
    CHECK(recorded.find("\"warmup\":true")!=std::string::npos);
    CHECK(!write_under_row_lock);CHECK(locks_held==0);
    const auto before=recorded;
    for(unsigned index=nr_nvfp4_test::row_count;index<16384;++index)
        nr_nvfp4_test::Write(key,index,1,index,8,24,24,640,ticks,0.001);
    nr_nvfp4_test::Write(key,20000,1,20000,8,24,24,640,ticks,0.001);
    CHECK(nr_nvfp4_test::dropped==1);CHECK(nr_nvfp4_test::row_count==16384);
    CHECK(recorded==before); // Recording only enqueues bounded memory, no hidden file I/O.
    std::puts("PASS CPU writer: frame0..127 warmup, frame128..727 measured, exact lifetime reset required");
}
'''
        for marker, definition in (("ACTUAL_SAMPLE", actual_sample), ("ACTUAL_FIND", actual_find),
                                   ("ACTUAL_ATTACH", actual_attach)):
            program = program.replace("@" + marker + "@", definition)
        source = self.root / "cpu-boundaries.cpp"
        source.write_text(program)
        executable = self.root / ("cpu-boundaries.exe" if os.name == "nt" else "cpu-boundaries")
        command = [compiler(), "-std=c++17", "-O0", "-static", str(source), "-o", str(executable)]
        built = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()

"""Execute production CreateOwner admission and Capture's disabled guard on CPU.

Only allocation-info/DXGI services are controlled. Stop at the first Owner
allocation; this proves admission decisions, not GPU allocation or NGX success.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PrivateSrAdmission(unittest.TestCase):
    def test_real_preflight_uses_available_memory_without_fixed_cap(self):
        configured = os.environ.get("NR_LLVM_BIN")
        compiler = str(Path(configured) / "clang++.exe") if configured else shutil.which(os.environ.get("CXX", "g++"))
        if not compiler or not Path(compiler).is_file():
            self.skipTest("Set NR_LLVM_BIN or CXX to a C++20 compiler")
        product = (ROOT / "src/nr_private_sr_product.inl").read_text(encoding="utf-8")
        begin = product.index("bool CreateOwner(")
        end = product.index("    auto* o=new(std::nothrow)Owner;", begin)
        admission = product[begin:end] + "    (void)parent;(void)feature;(void)queue;admitted_budget=budget;++allocation_boundary;return true;\n}\n"
        begin = product.index("unsigned Capture(")
        end = product.index("    nr_private_sr_status::requested.fetch_add(1);", begin)
        disabled = product[begin:end] + "    (void)list;(void)device;(void)context;(void)parent;(void)params;(void)feature;(void)guides;(void)color;(void)rw;(void)rh;(void)nw;(void)nh;(void)ow;(void)oh;++allocation_boundary;return 1;\n}\n"
        source = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "nr_private_sr_status.h"
using Id=std::uint64_t;using State=nr_private_sr_status::State;
struct ID3D12Device{};struct ID3D12GraphicsCommandList{};struct NgxParams{};
struct NgxHandle{};struct NrGuideBinding{};enum class CodecColorMode{Automatic};
struct FeatureRecord{};struct LifecycleQueue{};
struct NrContext{void* private_sr=nullptr;unsigned frames_seen=17;nr_private_sr_status::BudgetRetry private_sr_budget_retry{};};
struct Config{bool private_residual_sr=false;}g_config;
unsigned query_calls=0,memory_calls=0,allocation_boundary=0,checks=0;Id admitted_budget=0;
namespace nr_private_sr_budget {
struct Plan{bool valid=false;Id guides=0,backend=0,required=0;};Plan observed{};
Plan Query(ID3D12Device*,unsigned,unsigned,unsigned,unsigned){++query_calls;return observed;}
}
namespace nr_process_memory {
struct Memory{long local_result=0;struct {Id CurrentUsage=0,Budget=0;}local;}observed;
Memory Read(unsigned,bool){++memory_calls;return observed;}
}
bool FAILED(long result){return result<0;}
unsigned ID3D12Device_GetAdapterLuid(ID3D12Device*){return 0;}
template<class... T>void Log(const char*,T...){}
@ADMISSION@
@DISABLED@
void Check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(3);}}
int main(){
 constexpr Id required=1102970880,reserve=64ull<<20,extra=4ull<<20,used=2ull<<30;
 ID3D12Device device;NgxHandle parent;FeatureRecord feature;LifecycleQueue queue;
 nr_private_sr_status::BudgetKey key{};key.configuration=102;
 auto reset=[&]{query_calls=memory_calls=allocation_boundary=0;admitted_budget=0;
  nr_private_sr_budget::observed={true,0,0,required};nr_process_memory::observed={};};
 auto admit=[&](NrContext& c){return CreateOwner(c,&device,&parent,feature,&queue,3840,2160,3840,2160,key);};
 reset();NrContext c;nr_process_memory::observed.local={used,used+required+reserve+extra};
 Check(admit(c),"4K100 with sufficient real headroom must reach allocation, not fixed-cap refusal");
 Check(admitted_budget==required+extra,"Owner keeps spare minus reserve, including optional timing slack");
 Check(query_calls==1&&memory_calls==1&&allocation_boundary==1,"only one preflight precedes allocation");
 Check(c.private_sr_budget_retry.available==required+extra&&!c.private_sr_budget_retry.permanent,
  "headroom admission diagnostics stay retryable and match available bytes");
 reset();c={};nr_process_memory::observed.local={used,used+required+reserve};
 Check(admit(c)&&admitted_budget==required,"exact texture plan can proceed; optional timing does not gate rendering");
 for(unsigned condition=0;condition<5;++condition){
  reset();c={};auto& memory=nr_process_memory::observed;
  if(condition==0)memory.local={used,used+required+reserve-1};
  if(condition==1)memory.local={used,used+reserve};
  if(condition==2)memory.local={used,used-1};
  if(condition==3)memory.local={used,used};
  if(condition==4)memory.local={used,used+reserve-1};
  Check(!admit(c),"insufficient DXGI headroom rejects private allocation");
  const auto& retry=c.private_sr_budget_retry;
  Check(retry.valid&&!retry.permanent&&retry.reason==State::BudgetHeadroom&&retry.key==key,
   "low headroom reports retryable exact-key refusal");
  Check(!allocation_boundary&&!c.private_sr&&query_calls==1&&memory_calls==1,
   "low headroom never reaches private resource allocation");
 }
 reset();c={};nr_process_memory::observed.local_result=-1;
 Check(!admit(c)&&c.private_sr_budget_retry.reason==State::MemoryInfoUnknown&&!allocation_boundary,
  "unknown DXGI information fails closed before allocation");
 reset();c={};nr_private_sr_budget::observed={};
 Check(!admit(c)&&c.private_sr_budget_retry.reason==State::BudgetUnknown&&!memory_calls&&!allocation_boundary,
  "invalid allocation query fails closed before memory or allocation work");
 reset();c={};c.private_sr=&device;
 Check(!admit(c)&&!query_calls&&!memory_calls&&!allocation_boundary,"existing owner is not allocated twice");
 reset();c={};g_config.private_residual_sr=false;NrGuideBinding guides;
 Check(!Capture(nullptr,nullptr,c,nullptr,nullptr,feature,guides,CodecColorMode::Automatic,0,0,0,0,0,0),
  "disabled Capture returns immediately");
 Check(!query_calls&&!memory_calls&&!allocation_boundary,"disabled private SR adds no resource work");
 std::printf("Production private SR admission: %u checks; controlled Query/DXGI, no GPU or NGX\n",checks);
}
'''.replace("@ADMISSION@", admission).replace("@DISABLED@", disabled)
        with tempfile.TemporaryDirectory(prefix="nr-private-sr-admission-") as directory:
            work = Path(directory)
            cpp, exe = work / "admission.cpp", work / ("admission.exe" if os.name == "nt" else "admission")
            cpp.write_text(source, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror",
                                    "-I", str(ROOT / "src"), str(cpp), "-o", str(exe)], capture_output=True, text=True, timeout=45)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("Production private SR admission: 25 checks", ran.stdout)


if __name__ == "__main__":
    unittest.main()

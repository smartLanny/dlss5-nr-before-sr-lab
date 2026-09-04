"""CPU coverage for the bounded game-pixel-jitter forwarding contract."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def definition(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--source", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (args.source or root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
jitter_struct = definition(source, "struct NrJitterBinding {") + ";"
jitter = definition(source, "NrJitterBinding ResolveNrJitter(")
harness = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
struct NgxParams { bool x=false,y=false; float xv=0,yv=0; };
bool GetFloat(const NgxParams* p, const char* key, float* out) {
  if (!p || !out) return false;
  if (key[14]=='X') { if (!p->x) return false; *out=p->xv; return true; }
  if (!p->y) return false; *out=p->yv; return true;
}
enum class NrJitterMode : int { Legacy=0, GameInputPixel=1 };
JITTER
int failures=0;
void check(bool ok,const char* label) { if(!ok) { ++failures; std::printf("FAIL: %s\n",label); } }
int main() {
  NgxParams p{true,true,0.5f,-0.25f};
  auto a=ResolveNrJitter(&p,NrJitterMode::GameInputPixel,1000,500,500,250);
  check(a.known && std::fabs(a.x-.25f)<1e-6f && std::fabs(a.y+.125f)<1e-6f,"source to work pixel scale");
  auto b=ResolveNrJitter(&p,NrJitterMode::Legacy,1000,500,500,250);
  check(!b.known,"legacy mode preserves old no-forward behaviour");
  NgxParams missing{false,true,0,-0.25f};
  auto c=ResolveNrJitter(&missing,NrJitterMode::GameInputPixel,1000,500,500,250);
  check(!c.known && !c.valid_x,"missing input is unknown, not zero");
  NgxParams invalid{true,true,NAN,INFINITY};
  auto d=ResolveNrJitter(&invalid,NrJitterMode::GameInputPixel,1000,500,500,250);
  check(!d.known,"non-finite input is rejected");
  NgxParams zero{true,true,0,0};
  auto e=ResolveNrJitter(&zero,NrJitterMode::GameInputPixel,1000,500,1000,500);
  check(e.known && e.x==0 && e.y==0,"finite zero jitter is valid data");
  NgxParams huge{true,true,2000,0};
  auto f=ResolveNrJitter(&huge,NrJitterMode::GameInputPixel,1000,500,500,250);
  check(!f.known,"out of range input is rejected");
  std::printf("jitter regression: %s (%d failures)\n", failures?"FAIL":"PASS", failures);
  return failures ? 1 : 0;
}
'''.replace("JITTER", jitter_struct + "\n" + jitter)
with tempfile.TemporaryDirectory() as td:
    cpp = Path(td) / "jitter.cpp"
    exe = Path(td) / "jitter.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([args.compiler, "-std=c++20", "-Wall", "-Wextra", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

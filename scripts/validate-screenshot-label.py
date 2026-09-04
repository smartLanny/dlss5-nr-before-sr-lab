"""Compile the production screenshot formatter and test bounded ASCII output."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def definition(source, marker, semicolon=False):
    start = source.index(marker)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(semicolon)]


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
harness = r'''
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
BUILD_IDENTITY
enum class NrJitterMode : int { Legacy=0, GameInputPixel=1 };
CHAIN_CONFIG
enum class BypassReason { None, Unsupported, Other };
FORMATTER
int checks=0,failures=0;
void check(bool ok,const char* name) { ++checks; if(!ok) { ++failures; std::printf("FAIL: %s\n",name); } }
int main() {
    ChainConfig config{};
    check(!FormatScreenshotPostfix(nullptr,0,config,0,BypassReason::None),"null/zero destination");
    char untouched='Q';
    check(!FormatScreenshotPostfix(&untouched,0,config,0,BypassReason::None)&&untouched=='Q',"zero capacity does not write");
    const float values[]={-std::numeric_limits<float>::max(),-1,0,1,2,std::numeric_limits<float>::max(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
    std::size_t longest=0;
    char longest_label[91]{};
    for(bool enabled:{false,true}) for(int mode:{-1,1,2,INT_MAX})
    for(auto reason:{BypassReason::None,BypassReason::Unsupported})
    for(unsigned generation:{0u,100u,UINT_MAX}) for(unsigned style:{0u,1u,2u,UINT_MAX})
    for(bool peak:{false,true}) for(float value:values) {
        config.enabled=enabled; config.mode=mode; config.style=style;
        config.intensity=value; config.transfer_strength=value; config.post_transfer_strength=value;
        config.color_strength=value; config.experimental_peak_shoulder=peak;
        std::array<char,95> guarded; guarded.fill('Q');
        char* text=guarded.data()+2;
        check(FormatScreenshotPostfix(text,91,config,generation,reason),"full-capacity formatting succeeds");
        std::size_t length=std::strlen(text);
        check(length<=90&&guarded[0]=='Q'&&guarded[1]=='Q'&&guarded[93]=='Q'&&guarded[94]=='Q',"90-byte limit and no overrun");
        bool ascii=true;
        for(std::size_t i=0;i<length;++i) ascii=ascii&&static_cast<unsigned char>(text[i])>=32&&static_cast<unsigned char>(text[i])<127;
        check(ascii,"ASCII label");
        check(std::strstr(text,"DLSS5_")&&std::strstr(text,"_REQ-G")&&std::strstr(text,"_I")&&
            std::strstr(text,"_S")&&std::strstr(text,"_B")&&std::strstr(text,"_C")&&std::strstr(text,"_HP"),"required requested fields retained");
        check(std::strstr(text,!enabled?"_off_":(reason==BypassReason::None?"_active_":"_bypass_")),"bounded state label");
        if(length>longest) { longest=length; std::memcpy(longest_label,text,length+1); }
    }
    config={}; config.enabled=true; config.mode=2; config.intensity=2;
    config.transfer_strength=1; config.color_strength=1; config.style=2; config.experimental_peak_shoulder=true;
    char complete[91]{};
    check(FormatScreenshotPostfix(complete,sizeof(complete),config,UINT_MAX,BypassReason::None),"max fields label");
    const auto needed=std::strlen(complete)+1;
    for(std::size_t capacity=1;capacity<=91;++capacity) {
        std::array<char,95> guarded; guarded.fill('Q');
        const bool ok=FormatScreenshotPostfix(guarded.data()+2,capacity,config,UINT_MAX,BypassReason::None);
        check(ok==(capacity>=needed),"snprintf truncation reported correctly");
        check(guarded[1]=='Q'&&guarded[capacity+2]=='Q',"small buffer remains bounded");
        check(std::memchr(guarded.data()+2,'\0',capacity)!=nullptr,"small buffer always terminated");
    }
    // Reproduced installation path and ReShade's game/timestamp prefix, with
    // the largest formatter output. This is the regression's MAX_PATH seam.
    const char* directory="C:\\Program Files (x86)\\Steam\\steamapps\\common\\Cyberpunk 2077\\bin\\x64\\";
    const char* reshade_prefix="Cyberpunk2077 2026-09-04 12-34-56_0 ";
    const std::size_t full_path=std::strlen(directory)+std::strlen(reshade_prefix)+longest+std::strlen(".png");
    check(full_path<260,"Cyberpunk installation screenshot fits traditional MAX_PATH");
    std::printf("Screenshot label regression: %d checks, %d failures; longest=%zu; CP path=%zu\n%s\n",checks,failures,longest,full_path,longest_label);
    return failures?1:0;
}
'''
harness = harness.replace("BUILD_IDENTITY", re.search(r'constexpr const char \*kBuildIdentity = [^;]+;', source).group(0))
harness = harness.replace("CHAIN_CONFIG", definition(source, "struct ChainConfig", True))
harness = harness.replace("FORMATTER", definition(source, "bool FormatScreenshotPostfix("))
with tempfile.TemporaryDirectory(prefix="nr-screenshot-label-") as temporary:
    cpp = Path(temporary) / "test.cpp"
    cpp.write_text(harness, encoding="utf-8")
    outputs = []
    for language in (0, 1):
        binary = Path(temporary) / f"test-{language}.exe"
        subprocess.run([args.compiler,"-std=c++20","-O2","-static","-Wall","-Wextra","-Werror",
            f"-DNR_UI_CHINESE={language}",str(cpp),"-o",str(binary)],check=True)
        outputs.append(subprocess.check_output([str(binary)],text=True))
    assert outputs[0] == outputs[1], "Chinese/English screenshot labels differ"
    print(outputs[0],end="")
    print("Chinese/English output identical")

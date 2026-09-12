"""Compile actual F7 controls with a value-only ImGui recorder; no capture/GPU."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker):
    start = source.index(marker)
    at = source.index('{', start)
    depth = 1
    end = at + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <string>
#include <vector>
bool zh=false,report_busy=false,present=false,prefer_fg=false,request_ok=true;
unsigned statuses[2]{},workers[2]{},requests=0,core_requests=0;
std::string click,output;std::vector<std::string> buttons;unsigned disabled=0;
namespace ImGui {
void TextWrapped(const char* fmt,...){char text[4096];va_list ap;va_start(ap,fmt);std::vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);output+=text;output+='\n';}
void Text(const char*,...){}void TextDisabled(const char*,...){}
void BeginDisabled(bool value=true){if(value)++disabled;}void EndDisabled(){if(disabled)--disabled;}
bool Button(const char* label){buttons.emplace_back(label);return !disabled&&!click.empty()&&std::string(label).find(click)!=std::string::npos;}
bool TreeNode(const char*){return false;}void TreePop(){}bool SliderInt(const char*,int*,int,int,const char*){return false;}
void SetClipboardText(const char*){}
}
namespace nr_processing_runtime {bool PresentSelected(){return present;}}
namespace nr_compat_present_runtime {
unsigned CaptureStatus(bool fg){return statuses[fg];}unsigned CaptureWorkers(bool fg){return workers[fg];}bool CaptureUsesFg(){return prefer_fg;}
}
namespace nr_core_feedback {bool Busy(){return report_busy;}}
namespace nr_feedback_text {unsigned CoreHotkey(unsigned value){return value;}}
constexpr int VK_F1=112,VK_F8=119,VK_F12=123;
bool UseChineseUi(){return zh;}void DrawCoreStatus(){}void F8Tooltip(const char*){}
enum class Status {Idle,Requested,Running,Packaging,Complete,Partial,Failed};
enum class Comparison {Current,ShareMatrix};Comparison last_comparison=Comparison::Current;
std::atomic<Status> g_status{Status::Idle};std::atomic<unsigned> g_hotkey{VK_F8};
bool IsActive(){return g_status==Status::Requested||g_status==Status::Running;}
bool RequestCore(){++core_requests;return true;}
bool RequestFullscreen(Comparison value=Comparison::Current){++requests;last_comparison=value;return request_ok;}
void Cancel(){}
int g_lock=0;void AcquireSRWLockShared(int*){}void ReleaseSRWLockShared(int*){}
struct Group{bool shot_received=false;};struct Session{Group groups[7]{};unsigned group=0,group_count=7;};Session* g_session=nullptr;
wchar_t g_last_zip[64]=L"OLD-NATIVE-MATRIX.zip",g_ini_path[64]{};
void WideToUtf8(const wchar_t*,char* out,unsigned){std::snprintf(out,4096,"OLD-NATIVE-MATRIX.zip");}
template<std::size_t N>int swprintf_s(wchar_t(&out)[N],const wchar_t* fmt,unsigned value){return std::swprintf(out,N,fmt,value);}
bool WritePrivateProfileStringW(const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*){return true;}
// @HELPER@
// @CONTROLS@
unsigned checks=0;
void Check(bool ok,const char* name){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
bool button(const char* match){for(const auto& b:buttons)if(b.find(match)!=std::string::npos)return true;return false;}
void draw(){buttons.clear();output.clear();disabled=0;DrawControls();Check(!disabled,"disabled scopes balanced");}
int main(){
    for(bool language:{false,true}) {
        zh=language;present=false;click.clear();statuses[0]=statuses[1]=workers[0]=workers[1]=0;
        g_status=Status::Idle;draw();Check(button(zh?"七组画面":"Seven-group"),"native matrix remains available on inline route");
        click=zh?"七组画面":"Seven-group";draw();Check(last_comparison==Comparison::ShareMatrix,"native matrix still binds the original comparison");
        present=true;click.clear();draw();Check(!button(zh?"七组画面":"Seven-group")&&!button(zh?"采集全屏原图":"Capture full-screen original"),"compatibility route hides ignored native matrix/original buttons");
        Check(button(zh?"当前兼容路径各层":"current compatibility layers"),"compatibility F7 has its own explicit current-layer action");
        g_status=Status::Complete;draw();Check(output.find("OLD-NATIVE-MATRIX.zip")==std::string::npos,"old native matrix completion is not shown as compatibility capture");g_status=Status::Idle;
        click=zh?"当前兼容路径各层":"current compatibility layers";const auto before=requests;draw();Check(requests==before+1&&last_comparison==Comparison::Current,"compatibility button requests current capture only");
        statuses[1]=1;draw();Check(requests==before+1&&output.find(zh?"请求待完成":"request pending")!=std::string::npos,"pending FG capture disables new UI requests and shows waiting");
        report_busy=true;draw();Check(requests==before+1&&output.find(zh?"请求待完成":"request pending")!=std::string::npos,"capture status remains visible while core report is busy");report_busy=false;
        statuses[1]=2;workers[1]=1;draw();Check(output.find(zh?"正在导出":"being exported")!=std::string::npos&&output.find("Documents/DLSS5-FG-Capture")==std::string::npos,"worker retirement is required before claiming export complete");
        click.clear();workers[1]=0;draw();Check(output.find("Documents/DLSS5-FG-Capture")!=std::string::npos&&output.find(zh?"已导出":"data exported")!=std::string::npos,"completed FG capture shows actual destination");
        statuses[0]=2;statuses[1]=3;draw();Check(output.find("Documents/DLSS5-Present-Capture")!=std::string::npos&&output.find(zh?"捕获失败":"capture failed")!=std::string::npos,"Present export and FG failure are reported independently");
        prefer_fg=true;draw();Check(output.find(zh?"帧生成前":"before frame generation")!=std::string::npos,"next capture route uses actual preference seam");prefer_fg=false;
        statuses[0]=statuses[1]=0;request_ok=false;click=zh?"当前兼容路径各层":"current compatibility layers";draw();Check(output.find(zh?"未被接受":"not accepted")!=std::string::npos,"rejected request is not reported as pending or exported");request_ok=true;draw();
        click=zh?"生成反馈 TXT":"Save feedback TXT";auto core_before=core_requests;draw();Check(core_requests==core_before+1,"F8 TXT remains independently available");
    }
    std::printf("PASS compatibility F7 UI: %u checks; no GPU/files/capture execution\n",checks);
}
'''


class CompatibilityCaptureUi(unittest.TestCase):
    def test_actual_controls(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        source = (ROOT / 'src/nr_f8_sequence.inl').read_text(encoding='utf-8-sig')
        harness = HARNESS.replace('// @HELPER@', definition(source, 'void DrawCompatibilityCapture('))
        harness = harness.replace('// @CONTROLS@', definition(source, 'void DrawControls('))
        with tempfile.TemporaryDirectory(prefix='nr-compat-capture-ui-') as temp:
            cpp=Path(temp)/'ui.cpp';exe=Path(temp)/'ui.exe';cpp.write_text(harness,encoding='utf-8')
            built=subprocess.run([compiler,'-std=c++17','-O0','-static','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
            print(ran.stdout.strip())


if __name__=='__main__':unittest.main(verbosity=2)

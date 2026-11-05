"""Execute the current feedback page and production publication/status functions.

ImGui, locks and operating-system services are recorders/doubles: this checks
labels, path/copy actions and disabled scopes, not rendered UI, capture or GPU.
Routing mutations must compile and fail the same observable-output assertions.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker):
    start = source.index(marker)
    at = source.index('{', start)
    depth, end = 1, at + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <string>
#include <vector>
using DWORD=unsigned long;
constexpr DWORD ERROR_SUCCESS=0;
bool zh=false,present=false,acceptance=false;
unsigned locks=0,checks=0,core_cancels=0,image_requests=0,image_cancels=0;
unsigned stage=0,statuses[2]{},workers[2]{};
std::string output,click,clipboard;std::vector<std::string> buttons;
std::vector<bool> disabled;unsigned disabled_count=0;
void Check(bool ok,const char* name){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
void AcquireSRWLockShared(int* lock){++*lock;++locks;}
void ReleaseSRWLockShared(int* lock){Check(*lock>0&&locks>0,"balanced snapshot lock");--*lock;--locks;}
void AcquireSRWLockExclusive(int* lock){AcquireSRWLockShared(lock);}
void ReleaseSRWLockExclusive(int* lock){ReleaseSRWLockShared(lock);}
namespace ImGui {
void Ui(){Check(!locks,"ImGui is outside snapshot locks");}
void TextWrapped(const char* fmt,...){Ui();char text[8192]{};va_list ap;va_start(ap,fmt);std::vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);output+=text;output+='\n';}
void BeginDisabled(bool value=true){Ui();disabled.push_back(value);disabled_count+=value;}
void EndDisabled(){Ui();Check(!disabled.empty(),"disabled scope exists");disabled_count-=disabled.back();disabled.pop_back();}
bool Button(const char* label){Ui();buttons.emplace_back(label);return !disabled_count&&!click.empty()&&std::string(label).find(click)!=std::string::npos;}
void SetClipboardText(const char* path){Ui();clipboard=path;}
float GetFontSize(){Ui();return 14.f;}
void SameLine(){Ui();}void PushTextWrapPos(float){Ui();}void PopTextWrapPos(){Ui();}
void PushID(const char*){Ui();}void PopID(){Ui();}
}
namespace reshade {namespace api {struct effect_runtime{};}}
struct ChainConfig{unsigned nr_passes=1;};
bool UseChineseUi(){return zh;}
const char* NrUi(const char* en,const char* cn){return zh?cn:en;}
float SettingsContentWidth(){return 700.f;}
void SettingsTooltip(const char*){ImGui::Ui();}
void SettingsNote(const char* text){ImGui::TextWrapped("%s",text);}
void SettingsHelp(const char*){ImGui::Ui();}void SettingsSection(const char*){ImGui::Ui();}
bool NrCombo(const char*,const char*,int&,const char*){ImGui::Ui();return false;}
unsigned StageCaptureStatus(){return stage;}
namespace nr_acceptance {bool Active(){return acceptance;}void Draw(reshade::api::effect_runtime*,bool){ImGui::Ui();}}
#define NR_HAS_ACCEPTANCE 1
namespace nr_hotkeys {
enum Action{Image,Report};unsigned Key(Action a){return a==Image?118:119;}
const char* Name(unsigned key){return key==118?"F7":"F8";}
}
namespace nr_processing_runtime {bool PresentSelected(){return present;}}
namespace nr_compat_present_runtime {
unsigned CaptureStatus(bool fg){return statuses[fg];}unsigned CaptureWorkers(bool fg){return workers[fg];}
}
namespace nr_probe_policy {enum class Mode{Normal};}
struct Probe{nr_probe_policy::Mode mode=nr_probe_policy::Mode::Normal;};
Probe NrProbePolicySnapshot(){return {};}
void SetNrProbePolicy(nr_probe_policy::Mode){}
struct Timing{struct Wait{unsigned mode=0;}wait;};Timing NrTimingPolicySnapshot(){return {};}
void SetNrWaitComparison(unsigned,bool=false){}
namespace nr_replay {void DrawUi(){}}
namespace nr_dual_runtime {void DrawDetailsUi(bool,bool){}}
void DrawFaceDiagnosticControls(reshade::api::effect_runtime*,const ChainConfig&){}
void DrawTechnicalDiagnostics(reshade::api::effect_runtime*){}
const char *kAddonVersion="fixture",*kAddonAuthor="fixture",*kAddonAuthorAscii="fixture",*kOfficialReleaseUrl="fixture";
void WideToUtf8(const wchar_t* in,char* out,std::size_t capacity){std::snprintf(out,capacity,"%ls",in);}
DWORD GetCurrentProcessId(){return 42;}
unsigned long long GetTickCount64(){return 100;}
namespace nr_bundle {
enum class Status{Complete,Partial,Failed};
struct Result{wchar_t zip_path[1024]{};bool required_complete=false;Status status=Status::Failed;};
struct Job{};
template<std::size_t N>void CopyPath(wchar_t(&dst)[N],const wchar_t* src){std::wcsncpy(dst,src,N-1);dst[N-1]=0;}
void CopyText(char* dst,std::size_t size,const char* src){std::snprintf(dst,size,"%s",src?src:"");}
}
namespace nr_feedback_ticket {
constexpr unsigned kCapacity=128;
void Build(char* out,std::size_t size,const char*,bool){std::snprintf(out,size,"fixture ticket");}
}
namespace nr_feedback_report {
struct Output{std::shared_ptr<const std::string> text;std::wstring path;DWORD error=0;bool complete=false;};
}
std::atomic<unsigned> g_feedback_status{0};std::atomic<bool> g_feedback_requested{false};
int g_feedback_lock=0;wchar_t g_feedback_last_zip[1024]{};
namespace nr_core_feedback {
// @PAIR@
std::shared_ptr<const std::string> g_full_report;wchar_t g_report_path[1024]{};
DWORD g_report_error=ERROR_SUCCESS;bool g_report_complete=false,g_report_path_copy_claimed=false;
enum class ReportClipboardStatus:unsigned{Idle,Pending,Copied,Failed};
std::atomic<ReportClipboardStatus> g_report_clipboard_status{ReportClipboardStatus::Idle};
PairSnapshot g_pair{};char g_reason[160]{},g_submission[nr_feedback_ticket::kCapacity]{};
std::atomic<unsigned long long> g_serial{0};std::atomic<bool> g_cancel{false};nr_bundle::Job* g_job=nullptr;
void Cancel(){++core_cancels;}
// @PUBLICATION@
}
namespace nr_f8 {
enum class Status{Idle,Requested,Running,Packaging,Complete,Partial,Failed};
enum class Comparison{Current,ShareMatrix};struct Options{};
std::atomic<Status> g_status{Status::Idle};int g_lock=0,g_control=0;wchar_t g_last_zip[1024]{};
bool RequestFullscreen(Comparison=Comparison::Current){++image_requests;return true;}
bool RequestWithOptions(const Options&){++image_requests;return true;}
void Cancel(){++image_cancels;}
// @STATUS@
}
// @PAGE@
bool has(const char* text){return output.find(text)!=std::string::npos;}
unsigned count_button(const char* text){unsigned count=0;for(const auto& b:buttons)count+=b.find(text)!=std::string::npos;return count;}
void draw(){output.clear();buttons.clear();DrawUnifiedFeedback(nullptr,ChainConfig{},false);Check(!locks&&disabled.empty()&&!disabled_count,"page leaves locks and disabled scopes balanced");}
void reset(){
    present=acceptance=false;stage=0;statuses[0]=statuses[1]=workers[0]=workers[1]=0;click.clear();clipboard.clear();
    g_feedback_requested=false;g_feedback_status=0;g_feedback_last_zip[0]=0;
    nr_core_feedback::g_full_report.reset();nr_core_feedback::g_report_path[0]=0;nr_core_feedback::g_report_error=0;
    nr_core_feedback::g_reason[0]=0;nr_core_feedback::g_report_clipboard_status=nr_core_feedback::ReportClipboardStatus::Idle;
    nr_f8::g_status=nr_f8::Status::Idle;nr_f8::g_last_zip[0]=0;
}
void publish(bool complete,bool archive=false,DWORD error=0){
    nr_bundle::Result result{};result.status=error?nr_bundle::Status::Failed:complete?nr_bundle::Status::Complete:nr_bundle::Status::Partial;
    result.required_complete=complete;
    if(archive&&!error)nr_bundle::CopyPath(result.zip_path,L"C:\\feedback\\current-core.zip");
    nr_feedback_report::Output text{};text.error=error;text.complete=complete;
    if(!error){text.text=std::make_shared<const std::string>("current report");text.path=L"C:\\feedback\\current-report.txt";}
    g_feedback_requested=false; // The real worker has already claimed this request.
    nr_core_feedback::Publish(result,complete,error?"fixture write failure":"fixture evidence",nullptr,&text,archive);
}
int main(){
    for(bool language:{false,true}){
        zh=language;reset();draw();
        Check(count_button("###report")==1&&count_button("###capture")==1,"one current operation toolbar");
        Check(!has("192 KiB"),"legacy bounded-size tooltip is not routed");
        publish(true);draw();
        Check(has("C:\\feedback\\current-report.txt"),"current TXT path reaches feedback page");
        Check(has(zh?"正在复制":"copying its file path"),"pending clipboard remains pending");
        nr_core_feedback::g_report_clipboard_status=nr_core_feedback::ReportClipboardStatus::Copied;draw();
        Check(has(zh?"自动复制":"copied automatically"),"confirmed clipboard success is visible");
        nr_core_feedback::g_report_clipboard_status=nr_core_feedback::ReportClipboardStatus::Failed;draw();
        Check(has(zh?"自动复制路径失败":"automatic path copy failed")&&has("current-report.txt"),"clipboard failure retains this saved file path");
        publish(false);draw();Check(has(zh?"TXT 部分":"TXT is partial")&&has("current-report.txt"),"partial TXT is explicit and locatable");
        publish(true,true);nr_f8::g_status=nr_f8::Status::Complete;
        nr_bundle::CopyPath(nr_f8::g_last_zip,L"C:\\feedback\\current-images.zip");draw();
        Check(has("current-core.zip"),"paired core path reaches feedback page");
        Check(has("current-images.zip"),"paired image path reaches feedback page");
        click=zh?"复制核心包路径":"Copy core path";draw();Check(clipboard=="C:\\feedback\\current-core.zip","copy current core path");
        click=zh?"复制图像包路径":"Copy image path";draw();Check(clipboard=="C:\\feedback\\current-images.zip","copy current image path");
        click="###report";draw(); // Execute actual RequestCore -> core Request reset.
        Check(g_feedback_status==1&&nr_f8::g_status==nr_f8::Status::Idle,"current toolbar starts the actual core-only request");
        Check(!has("current-report.txt")&&!has("current-core.zip")&&!has("current-images.zip"),"new request hides all previous paired outputs immediately");
        const auto serial=nr_core_feedback::g_serial.load();draw();Check(nr_core_feedback::g_serial==serial,"busy report button is disabled");
        click="###capture";const auto images=image_requests;draw();Check(image_requests==images,"busy image button is disabled");
        click=zh?"取消核心采集":"Cancel core capture";const auto cancelled=core_cancels;draw();Check(core_cancels==cancelled+1,"pending core cancellation remains available");
        click.clear();publish(false,false,112);nr_f8::g_status=nr_f8::Status::Failed;draw();
        Check(has("Win32 112")&&has("fixture write failure"),"actual publication failure code and reason reach page");
        Check(!has("current-report.txt")&&!has("current-core.zip")&&!has("current-images.zip"),"failed new publication cannot reuse previous paths");
        Check(!has(zh?"TXT 已保存":"TXT saved")&&!has(zh?"图像补充包已生成":"Image supplement saved"),"failure is not presented as saved");
        nr_f8::g_status=nr_f8::Status::Partial;nr_bundle::CopyPath(nr_f8::g_last_zip,L"C:\\feedback\\partial-images.zip");draw();
        Check(has("partial-images.zip")&&has(zh?"图像补充包部分完成":"Image supplement partially complete"),"partial image result keeps its own path");
        reset();present=true;nr_f8::g_status=nr_f8::Status::Complete;
        nr_bundle::CopyPath(nr_f8::g_last_zip,L"C:\\feedback\\old-native.zip");statuses[0]=2;statuses[1]=1;workers[0]=1;draw();
        Check(!has("old-native.zip")&&!has("Documents/DLSS5-Present-Capture"),"compatibility hides old native output and waits for export workers");
        Check(has(zh?"正在导出":"being exported")&&has(zh?"请求待完成":"request pending"),"compatibility pending and worker states remain separate");
        workers[0]=0;statuses[1]=3;draw();
        Check(has("Documents/DLSS5-Present-Capture")&&has(zh?"FG 各层捕获失败":"FG layer capture failed"),"compatibility destination and independent failure reach current page");
        Check(has(zh?"不代表最终屏幕输出":"not native game-stage screenshots or final screen output"),"compatibility evidence scope is explicit");
        reset();publish(true,true);nr_f8::g_status=nr_f8::Status::Complete;
        nr_bundle::CopyPath(nr_f8::g_last_zip,L"C:\\feedback\\current-images.zip");acceptance=true;
        for(const char* action:{"###report","###capture",zh?"复制核心包路径":"Copy core path",zh?"复制图像包路径":"Copy image path"}){
            click=action;const auto before=nr_core_feedback::g_serial.load();const auto requests=image_requests;draw();
            Check(nr_core_feedback::g_serial==before&&image_requests==requests&&clipboard.empty(),"acceptance disables existing operations and result actions");
            Check(has("current-report.txt")&&has("current-images.zip"),"disabled acceptance view still reports current outputs");
        }
    }
    std::printf("PASS feedback publication/page/status recorder: %u checks; no rendered UI, OS clipboard, files, capture or GPU\n",checks);
}
'''


class FeedbackOutputUi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not cls.compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        sequence = (ROOT / 'src/nr_f8_sequence.inl').read_text(encoding='utf-8-sig')
        core = (ROOT / 'src/nr_feedback_core.inl').read_text(encoding='utf-8-sig')
        pages = (ROOT / 'src/nr_unified_pages.inl').read_text(encoding='utf-8-sig')
        cls.harness = HARNESS.replace('// @PAIR@', definition(core, 'struct PairSnapshot {') + ';')
        cls.harness = cls.harness.replace('// @PUBLICATION@', '\n'.join(definition(core, marker) for marker in (
            'bool Busy(', 'bool Request(', 'void Publish(', 'ReportClipboardStatus GetReportClipboardStatus(')))
        cls.harness = cls.harness.replace('// @STATUS@', '\n'.join(definition(sequence, marker) for marker in (
            'bool IsActive(', 'bool RequestCore(', 'void DrawCoreTextStatus(', 'void DrawCoreStatus(',
            'void DrawCompatibilityCaptureStatus(', 'void DrawImageStatus(')))
        cls.harness = cls.harness.replace('// @PAGE@', definition(pages, 'void DrawUnifiedFeedback('))

    def execute(self, source):
        with tempfile.TemporaryDirectory(prefix='nr-feedback-output-ui-') as directory:
            cpp, exe = Path(directory) / 'ui.cpp', Path(directory) / 'ui.exe'
            cpp.write_text(source, encoding='utf-8')
            link = [] if sys.platform == 'darwin' else ['-static']
            built = subprocess.run([self.compiler, '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
                                    *link, str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_current_page_and_publication(self):
        ran = self.execute(self.harness)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        print(ran.stdout.strip())

    def test_missing_core_route_is_rejected(self):
        mutated = self.harness.replace('nr_f8::DrawCoreStatus();', '', 1)
        self.assertNotEqual(mutated, self.harness)
        ran = self.execute(mutated)
        self.assertNotEqual(ran.returncode, 0)
        self.assertIn('FAIL current TXT path reaches feedback page', ran.stderr)

    def test_missing_image_route_is_rejected(self):
        mutated = self.harness.replace('nr_f8::DrawImageStatus(UseChineseUi());', '', 1)
        self.assertNotEqual(mutated, self.harness)
        ran = self.execute(mutated)
        self.assertNotEqual(ran.returncode, 0)
        self.assertIn('FAIL paired image path reaches feedback page', ran.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)

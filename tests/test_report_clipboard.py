"""Run the production clipboard transfer/retry seam without touching the OS clipboard."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReportClipboardTests(unittest.TestCase):
    def test_transfer_ownership_and_bounded_present_retry(self):
        cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        if not cxx:
            self.skipTest("C++ compiler is required")
        source = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8")
        start = source.index("bool CopyReportPathToClipboard(")
        # Keep the clipboard seam bounded before the independent Esc observer.
        end = source.index("void ObserveMenuTransition(", start)
        seam = source[start:end]
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
using HWND=void*; using HGLOBAL=void*; using ULONGLONG=unsigned long long;
constexpr unsigned GMEM_MOVEABLE=2, CF_UNICODETEXT=13;
namespace reshade { namespace api { struct effect_runtime; } }
int fail=0, frees=0, opens=0, closes=0, transfers=0;
std::wstring clipboard;
HGLOBAL GlobalAlloc(unsigned flags,std::size_t n){assert(flags==2);return fail==1?nullptr:std::malloc(n);}
void *GlobalLock(HGLOBAL h){return fail==2?nullptr:h;}
bool GlobalUnlock(HGLOBAL){return true;}
HGLOBAL GlobalFree(HGLOBAL h){++frees;std::free(h);return nullptr;}
bool OpenClipboard(HWND){++opens;return fail!=3;}
bool EmptyClipboard(){return fail!=4;}
HGLOBAL SetClipboardData(unsigned type,HGLOBAL h){assert(type==13);if(fail==5)return nullptr;clipboard=static_cast<wchar_t*>(h);++transfers;std::free(h);return reinterpret_cast<void*>(1);}
bool CloseClipboard(){++closes;return true;}
namespace nr_core_feedback {
enum class ReportClipboardStatus{Idle,Pending,Copied,Failed};
ReportClipboardStatus status=ReportClipboardStatus::Idle;
std::wstring ready,current; int results=0;
void Publish(const wchar_t *s){ready=current=s;status=ReportClipboardStatus::Pending;}
bool TakeReportPathForClipboard(wchar_t (&p)[1024]){if(ready.empty())return false;std::wmemset(p,0,1024);std::wcscpy(p,ready.c_str());ready.clear();return true;}
ReportClipboardStatus GetReportClipboardStatus(){return status;}
void SetReportClipboardResult(const wchar_t *s,bool copied){if(current==s && status==ReportClipboardStatus::Pending){status=copied?ReportClipboardStatus::Copied:ReportClipboardStatus::Failed;++results;}}
}
''' + seam + r'''
int main(){
    assert(!CopyReportPathToClipboard(nullptr,nullptr));
    assert(!CopyReportPathToClipboard(nullptr,L""));
    std::wstring long_path(1024,L'x');
    assert(!CopyReportPathToClipboard(nullptr,long_path.c_str()));
    assert(opens==0 && frees==0);
    for(fail=1;fail<=5;++fail){
        int f=frees,c=closes,t=transfers;
        assert(!CopyReportPathToClipboard(nullptr,L"report.txt"));
        assert(frees==f+(fail!=1));
        assert(closes==c+(fail>=4)); assert(transfers==t);
    }
    fail=0; int f=frees;
    assert(CopyReportPathToClipboard(nullptr,L"C:\\Reports\\\u53cd\u9988.txt"));
    assert(frees==f && clipboard==L"C:\\Reports\\\u53cd\u9988.txt");
    using namespace nr_core_feedback;
    Publish(L"first.txt");fail=3;int o=opens;
    PollReportPathClipboard(nullptr,1000);assert(opens==o+1 && results==0);
    PollReportPathClipboard(nullptr,1099);assert(opens==o+1);
    fail=0;PollReportPathClipboard(nullptr,1100);
    assert(status==ReportClipboardStatus::Copied && results==1 && clipboard==L"first.txt");
    o=opens;PollReportPathClipboard(nullptr,2000);assert(opens==o);
    Publish(L"timeout.txt");fail=3;
    PollReportPathClipboard(nullptr,3000);PollReportPathClipboard(nullptr,4500);
    assert(status==ReportClipboardStatus::Failed && results==2);
    Publish(L"old.txt");PollReportPathClipboard(nullptr,5000);
    Publish(L"new.txt");fail=0;PollReportPathClipboard(nullptr,5010);
    assert(clipboard==L"new.txt" && results==3);
    Publish(L"cancelled.txt");fail=3;PollReportPathClipboard(nullptr,6000);
    status=ReportClipboardStatus::Idle;o=opens;PollReportPathClipboard(nullptr,6100);
    assert(opens==o && results==3);
}
'''
        with tempfile.TemporaryDirectory(prefix="nr-clipboard-") as directory:
            cpp = Path(directory) / "clipboard.cpp"
            exe = Path(directory) / "clipboard.exe"
            cpp.write_text(harness, encoding="utf-8")
            # macOS has no fully static CRT; retain the existing flags elsewhere.
            link_flags = [] if sys.platform == "darwin" else ["-static"]
            subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", *link_flags,
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()

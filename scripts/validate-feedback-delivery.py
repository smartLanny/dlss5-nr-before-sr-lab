"""Host test of the production F8 cleanup only-two-deliverables policy.
Win32 IO is a fault-injection seam, not Windows/game runtime verification.
The normal Windows core coordinator suite separately checks real files/ZIP/TXT.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def definition(text, marker, semicolon=False):
    start = text.index(marker)
    pos = text.index('{', start)
    depth = 0
    while pos < len(text):
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
        if depth == 0:
            return text[start:pos + int(semicolon)]
    raise ValueError(marker)

PRE = r'''
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
using DWORD=unsigned long;using UINT64=unsigned long long;using ULONGLONG=unsigned long long;
constexpr DWORD ERROR_SUCCESS=0,GENERIC_READ=1,DELETE=2,FILE_SHARE_READ=4,OPEN_EXISTING=8;
constexpr DWORD FILE_FLAG_OPEN_REPARSE_POINT=16,FILE_FLAG_SEQUENTIAL_SCAN=32;
constexpr DWORD FILE_TYPE_DISK=1,FILE_ATTRIBUTE_DIRECTORY=2,FILE_ATTRIBUTE_REPARSE_POINT=4;
constexpr int TRUE=1,FALSE=0,FileDispositionInfo=4;
struct BY_HANDLE_FILE_INFORMATION {DWORD dwFileAttributes=0,nFileSizeHigh=0,nFileSizeLow=0,nNumberOfLinks=1;};
struct FILE_DISPOSITION_INFO {int DeleteFile=0;};
struct DiskFile {std::string bytes="summary";bool exists=true,locked=false,read_fail=false,delete_fail=false,pending=false;DWORD type=FILE_TYPE_DISK,attributes=0,links=1,access=0;};
std::map<std::wstring,DiskFile> files;
using HANDLE=DiskFile*;constexpr HANDLE INVALID_HANDLE_VALUE=nullptr;
int checks=0,failures=0,opens=0,dispositions=0;
void check(bool ok,const char *label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
HANDLE CreateFileW(const wchar_t *path,DWORD access,DWORD share,void*,DWORD how,DWORD flags,void*){
    ++opens;check(share==FILE_SHARE_READ && how==OPEN_EXISTING,"read/delete sharing and existing-only");
    check(flags==(FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN),"do not follow final reparse point");
    auto it=files.find(path);if(it==files.end()||!it->second.exists||it->second.locked)return INVALID_HANDLE_VALUE;
    it->second.access=access;return &it->second;
}
void CloseHandle(HANDLE h){if(h&&h->pending)h->exists=false;}
DWORD GetFileType(HANDLE h){return h->type;}
int GetFileInformationByHandle(HANDLE h,BY_HANDLE_FILE_INFORMATION *i){i->dwFileAttributes=h->attributes;i->nNumberOfLinks=h->links;i->nFileSizeLow=static_cast<DWORD>(h->bytes.size());return TRUE;}
int ReadFile(HANDLE h,void *out,DWORD size,DWORD *read,void*){if(h->read_fail)return FALSE;*read=std::min<DWORD>(size,h->bytes.size());std::memcpy(out,h->bytes.data(),*read);return TRUE;}
int SetFileInformationByHandle(HANDLE h,int info,void *ptr,DWORD size){
    ++dispositions;check((h->access&DELETE)!=0 && info==FileDispositionInfo && size==sizeof(FILE_DISPOSITION_INFO),"delete same verified open handle");
    if(h->delete_fail)return FALSE;h->pending=static_cast<FILE_DISPOSITION_INFO*>(ptr)->DeleteFile!=0;return TRUE;
}
namespace nr_feedback_text {constexpr std::size_t kSummaryLimit=262144;}
namespace nr_bundle {
enum class FileKind{Copy,HashOnly};enum class Status{Failed,Complete,Partial};
struct Artifact {wchar_t source[1024]={};char name[240]={},sha256[65]={};FileKind kind=FileKind::Copy;bool included=true,source_changed=false,truncated=false;UINT64 bytes=7;};
struct Job {wchar_t directory[1024]=L"C:\\feedback\\session";Artifact files[5];unsigned count=1;bool executed=true;};
struct Result {Status status=Status::Complete;wchar_t zip_path[1024]=L"C:\\feedback\\session\\feedback.zip";bool required_complete=true;};
bool LocalAbsolutePath(const wchar_t *p){return p && std::wcslen(p)>3 && p[1]==L':' && p[2]==L'\\';}
bool ValidUtf8(const char *p){return p!=nullptr;}
SHA
}
namespace nr_feedback_report {
OUTPUT
FILE
READ
CLEAN
}
'''
MAIN = r'''
const wchar_t *native=L"\\\\?\\C:\\feedback\\session\\summary-0.txt";
void init(nr_bundle::Job &job,nr_bundle::Result &zip,nr_feedback_report::Output &out){
    job={};zip={};out={};files.clear();opens=dispositions=0;
    out.text=std::make_shared<const std::string>("saved text");out.path=L"C:\\feedback\\session\\feedback-report.txt";
    std::wcscpy(job.files[0].source,L"C:\\feedback\\session\\summary-0.txt");
    std::strcpy(job.files[0].name,"logs/nr-current-summary.txt");
    nr_bundle::Sha256 hash;hash.Update(reinterpret_cast<const unsigned char*>("summary"),7);hash.Finish(job.files[0].sha256);
    files[native]={};
}
int main(){
    nr_bundle::Job job;nr_bundle::Result zip;nr_feedback_report::Output out;
    init(job,zip,out);
    check(nr_feedback_report::CleanupSummaryScratch(job,zip,out)==1,"normal cleanup removes verified duplicate");
    check(!files[native].exists,"scratch gone only after handle closes");
    check(dispositions==1,"only one file marked for deletion");
    for(int c=0;c<22;++c){
        init(job,zip,out);
        switch(c){
        case 0:job.executed=false;break;
        case 1:zip.status=nr_bundle::Status::Failed;break;
        case 2:zip.required_complete=false;break;
        case 3:out.complete=false;break;
        case 4:out.text.reset();break;
        case 5:out.error=5;break;
        case 6:std::wcscpy(job.directory,L"relative");break;
        case 7:zip.zip_path[0]=0;break;
        case 8:out.path=L"C:\\other\\feedback-report.txt";break;
        case 9:job.files[0].included=false;break;
        case 10:job.files[0].source_changed=true;break;
        case 11:job.files[0].truncated=true;break;
        case 12:std::strcpy(job.files[0].name,"logs/other-summary.txt");break;
        case 13:std::wcscpy(job.files[0].source,L"C:\\game\\nr-before-sr.log");break;
        case 14:job.files[0].kind=nr_bundle::FileKind::HashOnly;break;
        case 15:files[native].bytes="changed";break;
        case 16:files[native].locked=true;break;
        case 17:files[native].attributes=FILE_ATTRIBUTE_REPARSE_POINT;break;
        case 18:files[native].links=2;break;
        case 19:files[native].read_fail=true;break;
        case 20:files[native].delete_fail=true;break;
        case 21:files[native].type=9;break;
        }
        check(nr_feedback_report::CleanupSummaryScratch(job,zip,out)==0,"unsafe or incomplete cleanup rejected");
        check(files[native].exists,"keep recovery evidence and unrelated files");
    }
    init(job,zip,out);zip.status=nr_bundle::Status::Partial;
    check(nr_feedback_report::CleanupSummaryScratch(job,zip,out)==0,"partial archive preserves scratch for recovery");
    init(job,zip,out);std::string text;
    check(nr_feedback_report::ReadVerifiedSummary(job.files[0],text),"normal verified read unchanged");
    check(files[native].exists && dispositions==0,"ordinary report read never deletes");
    std::printf("F8 production scratch policy: %d checks / %d failures\n",checks,failures);
    return failures?1:0;
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='c++')
    args=parser.parse_args()
    report=(ROOT/'src/nr_feedback_report.inl').read_text(encoding='utf-8-sig')
    bundle=(ROOT/'src/nr_diagnostic_bundle.inl').read_text(encoding='utf-8-sig')
    code=PRE.replace('SHA\n',definition(bundle,'struct Sha256',True)+'\n')
    for key,marker,semi in [('OUTPUT','struct Output',True),('FILE','struct File {',True),('READ','bool ReadVerifiedSummary(',False),('CLEAN','unsigned CleanupSummaryScratch(',False)]:
        code=code.replace('\n'+key+'\n','\n'+definition(report,marker,semi)+'\n')
    code+=MAIN
    with tempfile.TemporaryDirectory(prefix='f8-delivery-') as temp:
        source=Path(temp)/'test.cpp';exe=Path(temp)/'test'
        source.write_text(code,encoding='utf-8')
        subprocess.run([args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(source),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    assert report.index('CleanupSummaryScratch(job, zip, output);')>report.index('if (WriteAtomic(job.directory, *cached, output))')
    print('Cleanup runs only after atomic TXT publication; no GPU/NGX is invoked.')

if __name__=='__main__':main()

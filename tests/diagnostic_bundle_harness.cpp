#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include "../src/nr_diagnostic_bundle.inl"

namespace {
unsigned int checks=0;
bool Check(bool condition,const char *label) {
    ++checks;
    if (!condition) std::printf("FAIL %s (error=%lu)\n",label,GetLastError());
    return condition;
}
bool Put(const wchar_t *path,const void *data,DWORD size) {
    HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
        CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (f==INVALID_HANDLE_VALUE) return false;
    DWORD n=0;const bool ok=WriteFile(f,data,size,&n,nullptr)!=FALSE&&n==size;
    CloseHandle(f);return ok;
}
bool Path(wchar_t *result,const wchar_t *parent,const wchar_t *child) {
    return swprintf_s(result,nr_bundle::kMaxPath,L"%ls\\%ls",parent,child)>=0;
}
bool CaseDirectory(wchar_t *result,const wchar_t *parent,const wchar_t *child) {
    return Path(result,parent,child)&&CreateDirectoryW(result,nullptr)!=FALSE;
}
bool TestSha() {
    struct V { const char *message;const char *sha; };
    const V values[]={
        {"","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}};
    for (const auto &v:values) {
        nr_bundle::Sha256 sha{};char actual[65]={};
        for (const char *p=v.message;*p;++p) sha.Update(reinterpret_cast<const unsigned char *>(p),1);
        sha.Finish(actual);
        if (!Check(std::strcmp(actual,v.sha)==0,"SHA256 published vector fragmented input")) return false;
    }
    nr_bundle::Sha256 sha{};char actual[65]={};unsigned char block[1000];std::memset(block,'a',sizeof(block));
    for (unsigned int i=0;i<1000;++i) sha.Update(block,sizeof(block));
    sha.Finish(actual);
    return Check(std::strcmp(actual,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")==0,"SHA256 million a");
}
bool TestMetadataCapacity(const wchar_t *parent) {
    wchar_t directory[nr_bundle::kMaxPath]={};
    std::string large="metadata.case=large\n";
    while(large.size()<96u*1024u)large+="字段=诊断; value=0123456789abcdef\n";
    large+="metadata.requiredTail=complete\n";
    if(!CaseDirectory(directory,parent,L"metadata-large"))return false;
    auto *job=nr_bundle::CreateJob(directory,large.c_str());
    if(!Check(job && std::strlen(job->metadata)==large.size(),"large UTF8 metadata crosses old 32KiB limit without package truncation"))return false;
    const auto complete=nr_bundle::Run(*job);nr_bundle::DestroyJob(job);
    if(!Check(complete.status==nr_bundle::Status::Complete&&complete.required_complete,"large metadata produces an actual complete archive"))return false;

    const std::string suffix="中文";
    std::string boundary(nr_bundle::kMetadataBytes-1-suffix.size(),'a');boundary+=suffix;
    if(!CaseDirectory(directory,parent,L"metadata-limit"))return false;
    job=nr_bundle::CreateJob(directory,boundary.c_str());
    if(!Check(job && std::strlen(job->metadata)==nr_bundle::kMetadataBytes-1 && nr_bundle::ValidUtf8(job->metadata),
        "exact metadata limit keeps the final multibyte character and terminating NUL"))return false;
    const auto limit=nr_bundle::Run(*job);nr_bundle::DestroyJob(job);
    if(!Check(limit.status==nr_bundle::Status::Complete&&limit.required_complete,"near-capacity UTF8 metadata survives real ZIP and JSON serialization"))return false;

    boundary+='x';job=nr_bundle::CreateJob(directory,boundary.c_str());
    if(!Check(job==nullptr,"metadata at capacity is rejected instead of silently clipping bytes")){nr_bundle::DestroyJob(job);return false;}
    std::string invalid(nr_bundle::kMetadataBytes-2,'a');invalid+='\xE4';
    job=nr_bundle::CreateJob(directory,invalid.c_str());
    if(!Check(job==nullptr,"incomplete final UTF8 code point is rejected within size limit")){nr_bundle::DestroyJob(job);return false;}
    return true;
}
}

int wmain(int argc,wchar_t **argv) {
    if (argc!=2) return 2;
    if (!TestSha()) return 1;
    if (!TestMetadataCapacity(argv[1])) return 1;
    wchar_t source[nr_bundle::kMaxPath]={},empty[nr_bundle::kMaxPath]={},log[nr_bundle::kMaxPath]={};
    wchar_t directory[nr_bundle::kMaxPath]={},missing[nr_bundle::kMaxPath]={};
    if (!Path(source,argv[1],L"source.bin")||!Path(empty,argv[1],L"empty.bin")||
        !Path(log,argv[1],L"active.log")||!Path(missing,argv[1],L"absent.bin")) return 1;
    unsigned char bytes[131079];for (unsigned int i=0;i<sizeof(bytes);++i) bytes[i]=static_cast<unsigned char>(i*37u+11u);
    if (!Check(Put(source,bytes,sizeof(bytes))&&Put(empty,"",0)&&Put(log,"old-line\ncurrent-line\n",22),"fixtures")) return 1;
    // Production code streams buffers from Job; this fixture is intentionally
    // larger than one stream chunk to exercise incremental CRC/SHA boundaries.
    if (!CaseDirectory(directory,argv[1],L"complete")) return 1;
    auto *job=nr_bundle::CreateJob(directory,"build=beta0.3.3.5\nGPU=test\nquoted=\"yes\"\n中文=元数据\n");
    if (!Check(job!=nullptr,"heap job creation")) return 1;
    if (!Check(nr_bundle::AddFile(*job,source,"stage/original.pfm",nr_bundle::FileKind::Copy,true),"add copy")||
        !Check(nr_bundle::AddFile(*job,empty,"empty.bin",nr_bundle::FileKind::Copy),"add empty")||
        !Check(nr_bundle::AddFile(*job,source,"截图/人物.bin",nr_bundle::FileKind::Copy),"UTF8 entry")||
        !Check(nr_bundle::AddFile(*job,source,"modules/nvngx_dlssnr.dll",nr_bundle::FileKind::HashOnly,true),"hash-only module")) return 1;
    const auto complete=nr_bundle::Run(*job);
    if (!Check(complete.status==nr_bundle::Status::Complete&&complete.included_files==3&&complete.hashed_modules==1,
        "complete archive result")||!Check(nr_bundle::Run(*job).status==nr_bundle::Status::Failed,"job cannot run twice")) return 1;
    nr_bundle::DestroyJob(job);
    job=nr_bundle::CreateJob(directory,"must-not-overwrite");
    const auto existing=nr_bundle::Run(*job);
    if (!Check(existing.win32_error==ERROR_FILE_EXISTS,"existing final not overwritten")||
        !Check(existing.zip_path[0]==0&&!existing.required_complete,"failed session never claims another session's ZIP path")) return 1;
    nr_bundle::DestroyJob(job);

    if (!CaseDirectory(directory,argv[1],L"partial")) return 1;
    job=nr_bundle::CreateJob(directory,"missing and bounded log test");
    HANDLE writer=CreateFileW(log,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (!Check(writer!=INVALID_HANDLE_VALUE,"active writer handle")) return 1;
    nr_bundle::AddFile(*job,log,"logs/live.log",nr_bundle::FileKind::LogTail,false,13);
    nr_bundle::AddFile(*job,missing,"screenshots/missing.png",nr_bundle::FileKind::Copy,true);
    nr_bundle::AddFile(*job,source,"too-large.bin",nr_bundle::FileKind::Copy,false,32);
    nr_bundle::AddMissing(*job,"stages/metadata.txt","stage-timeout-with-NR-bypassed",true);
    const auto partial=nr_bundle::Run(*job);CloseHandle(writer);
    if (!Check(partial.status==nr_bundle::Status::Partial&&partial.included_files==1&&
        partial.missing_files==3&&partial.truncated_files==1&&!partial.required_complete,"missing and size caps")) return 1;
    nr_bundle::DestroyJob(job);

    if (!CaseDirectory(directory,argv[1],L"names")) return 1;
    job=nr_bundle::CreateJob(directory,"path policy");
    const char *bad[]={"../escape.txt","/absolute.txt","a\\b.txt","a//b.txt","C:ads","x/../y",
        "name. ","NUL.txt","COM1.bin","manifest.json","capture-info.txt",
        "manifest.json/child","CAPTURE-INFO.TXT/child","bad\xff"};
    for (const char *name:bad) if (!Check(!nr_bundle::AddMissing(*job,name,"bad"),"reject unsafe or reserved name")) return 1;
    nr_bundle::AddFile(*job,empty,"Safe.txt",nr_bundle::FileKind::Copy);
    if (!Check(!nr_bundle::AddFile(*job,empty,"safe.TXT",nr_bundle::FileKind::Copy),"reject duplicate Windows entry")) return 1;
    if (!Check(!nr_bundle::AddFile(*job,empty,"safe.txt/child",nr_bundle::FileKind::Copy),"reject file/directory entry conflict")) return 1;
    nr_bundle::AddFile(*job,L"\\\\server\\share\\secret","network.txt",nr_bundle::FileKind::Copy);
    nr_bundle::AddFile(*job,L"C:\\a:stream","ads.txt",nr_bundle::FileKind::Copy);
    nr_bundle::AddFile(*job,argv[1],"directory.txt",nr_bundle::FileKind::Copy);
    if (!Check(nr_bundle::Run(*job).status==nr_bundle::Status::Partial,"rejections recorded in valid ZIP")) return 1;
    nr_bundle::DestroyJob(job);

    if (!CaseDirectory(directory,argv[1],L"cancelled")) return 1;
    job=nr_bundle::CreateJob(directory,"cancel before read");
    nr_bundle::AddFile(*job,source,"not-read.bin",nr_bundle::FileKind::Copy,true);nr_bundle::RequestCancel(*job);
    if (!Check(nr_bundle::Run(*job).status==nr_bundle::Status::Partial,"cancelled job preserves manifest")) return 1;
    nr_bundle::DestroyJob(job);

    if (!CaseDirectory(directory,argv[1],L"busy")) return 1;
    job=nr_bundle::CreateJob(directory,"busy guard");nr_bundle::g_running.store(true);
    const auto busy=nr_bundle::Run(*job);nr_bundle::g_running.store(false);
    if (!Check(busy.status==nr_bundle::Status::Failed&&busy.win32_error==ERROR_BUSY&&!job->executed,"single worker guard")) return 1;
    nr_bundle::DestroyJob(job);

    if (!CaseDirectory(directory,argv[1],L"existing-partial")) return 1;
    wchar_t partial_path[nr_bundle::kMaxPath]={};Path(partial_path,directory,L"feedback.zip.partial");
    if (!Put(partial_path,"owner-sentinel",14)) return 1;
    job=nr_bundle::CreateJob(directory,"another owner");
    if (!Check(nr_bundle::Run(*job).status==nr_bundle::Status::Failed,"existing partial not overwritten")) return 1;
    nr_bundle::DestroyJob(job);

    if (!CaseDirectory(directory,argv[1],L"entry-limit")) return 1;
    job=nr_bundle::CreateJob(directory,"entry limit");
    for (unsigned int i=0;i<nr_bundle::kMaxFiles;++i) {
        char name[40]={};std::snprintf(name,sizeof(name),"entry-%u.txt",i);
        if (!Check(nr_bundle::AddFile(*job,empty,name,nr_bundle::FileKind::Copy),"bounded entry reserve")) return 1;
    }
    if (!Check(!nr_bundle::AddMissing(*job,"overflow.txt","file-list-limit"),"65th explicit entry rejected")||
        !Check(nr_bundle::Run(*job).status==nr_bundle::Status::Partial,"manifest reports limit rejection")) return 1;
    nr_bundle::DestroyJob(job);
    if (!CaseDirectory(directory,argv[1],L"long-path")) return 1;
    for (unsigned int i=0;i<4;++i) {
        wchar_t next[nr_bundle::kMaxPath]={};
        if (!Path(next,directory,L"abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz")) return 1;
        wchar_t native[nr_bundle::kMaxPath]={};nr_bundle::NativePath(native,next);
        if (!CreateDirectoryW(native,nullptr)) return 1;
        nr_bundle::CopyPath(directory,next);
    }
    job=nr_bundle::CreateJob(directory,"long paths");
    if (!Check(job!=nullptr,"long-path job")) return 1;
    nr_bundle::AddFile(*job,empty,"empty.txt",nr_bundle::FileKind::Copy);
    if (!Check(nr_bundle::Run(*job).status==nr_bundle::Status::Complete,"archive beyond MAX_PATH")) return 1;
    nr_bundle::DestroyJob(job);
    std::printf("PASS diagnostic bundle native checks=%u\n",checks);return 0;
}

// Real Win32 files + production ZIP/TXT writer. No ReShade/NGX/GPU is used.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <memory>
#include <string>
#include <filesystem>
#include <fstream>
#include "nr_diagnostic_bundle.inl"
#include "nr_feedback_text.h"
#include "nr_feedback_report.inl"
namespace fs=std::filesystem;
unsigned checks=0,failures=0;
void check(bool ok,const char *label){++checks;if(!ok){++failures;std::printf("FAIL: %s (%lu)\n",label,GetLastError());}}
void put(const fs::path &path,const char *text){std::ofstream stream(path,std::ios::binary);stream<<text;check(stream.good(),"fixture write");}
int wmain(int argc,wchar_t **argv){
    if(argc!=2)return 2;
    const auto root=fs::absolute(argv[1]);
    if(fs::exists(root)){std::fprintf(stderr,"Use a new fixture directory\n");return 2;}
    fs::create_directories(root);
    const char *summary="format=DLSS5-LogSummary-v1\n[tail]\nNR bypass: reason=awaiting-native-reset\n";
    for(unsigned scenario=0;scenario<7;++scenario){
        const auto dir=root/std::to_wstring(scenario);fs::create_directory(dir);
        auto source=dir/"summary-0.txt";
        if(scenario==4)source=root/"original-game.log";
        put(source,summary);
        auto *job=nr_bundle::CreateJob(dir.c_str(),"format=delivery-test\n");
        check(job!=nullptr,"job created");if(!job)return 1;
        nr_bundle::AddFile(*job,source.c_str(),"logs/nr-current-summary.txt",nr_bundle::FileKind::Copy,true);
        const auto zip=nr_bundle::Run(*job);
        check(zip.status==nr_bundle::Status::Complete,"real ZIP completed");
        HANDLE lock=INVALID_HANDLE_VALUE;
        if(scenario==1)put(dir/"feedback-report.txt","existing user report");
        if(scenario==2)put(source,"changed after ZIP hashing");
        if(scenario==3)lock=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(scenario==5)check(CreateHardLinkW((root/L"extra-link.txt").c_str(),source.c_str(),nullptr)!=FALSE,"hard-link fixture");
        // Scenario 6: capture incomplete although its archive was written.
        const auto text=nr_feedback_report::Build(*job,zip,scenario!=6);
        if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
        if(scenario==0){
            check(text.text && text.complete,"TXT complete");
            check(text.text->find("awaiting-native-reset")!=std::string::npos,"summary retained in TXT");
            check(!fs::exists(source),"complete delivery removes duplicate scratch");
            unsigned count=0;
            for(const auto &entry:fs::directory_iterator(dir)){
                ++count;const auto leaf=entry.path().filename();
                check(leaf==L"feedback.zip" || leaf==L"feedback-report.txt","only two public deliverables");
            }
            check(count==2,"exactly TXT plus ZIP");
        }else{
            check(fs::exists(source),"failure/changed/locked/foreign/hardlinked/partial source retained");
            if(scenario==1)check(!text.text && text.error!=0,"TXT failure remains explicit");
            if(scenario==2||scenario==6)check(text.text && !text.complete,"partial text not falsely complete");
        }
        check(fs::exists(dir/"feedback.zip"),"ZIP never deleted");
        nr_bundle::DestroyJob(job);
    }
    std::printf("Real Win32 F8 delivery: %u checks / %u failures\n",checks,failures);
    return failures?1:0;
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "nr_face_worker.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>

namespace nr_face {
struct Worker::State {
    std::mutex mutex;
    std::atomic<bool> stop{false};
    bool pending=false, published=false;
    Reference reference{};
    Result result{};
    std::wstring runtime,model;
    HANDLE event{};
    HMODULE module{};
    ~State(){if(event)CloseHandle(event);}
    void Run() {
        Detector detector;std::string error;
        bool opened=false;
        try {
            const auto size=std::filesystem::file_size(model);
            if(size==229797) {
                std::ifstream file(std::filesystem::path(model),std::ios::binary);
                std::vector<unsigned char> data(size);
                if(file.read(reinterpret_cast<char*>(data.data()),size))
                    opened=detector.Open(runtime.c_str(),data.data(),data.size(),
                        "b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1",error);
            }
        } catch(...) {opened=false;}
        if(!opened) {
            std::lock_guard<std::mutex> lock(mutex);result.status=nr_region::Status::WorkerUnavailable;published=true;
            return;
        }
        // Large input lives on the heap, not the game's small worker stack.
        auto local=std::make_unique<Reference>();
        while(!stop.load(std::memory_order_acquire)) {
            if(WaitForSingleObject(event,INFINITE)!=WAIT_OBJECT_0)break;
            if(stop.load(std::memory_order_acquire))break;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(!pending)continue;
                local->frame=reference.frame;local->width=reference.width;local->height=reference.height;
                std::copy_n(reference.bgr.data(),local->width*local->height*3,local->bgr.data());pending=false;
            }
            Result next{};next.frame=local->frame;std::vector<Face> faces;
            const auto start=std::chrono::steady_clock::now();
            const bool success=detector.Detect(local->bgr.data(),local->width,local->height,local->width*3,faces,error);
            next.cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            next.status=success?(faces.empty()?nr_region::Status::NoFace:nr_region::Status::Available):nr_region::Status::WorkerUnavailable;
            next.count=static_cast<unsigned>(std::min<std::size_t>(faces.size(),next.faces.size()));
            std::copy_n(faces.begin(),next.count,next.faces.begin());
            if(!stop.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(mutex);result=next;published=true;
            }
        }
    }
    static DWORD WINAPI Entry(void* pointer) {
        auto* owned=static_cast<std::shared_ptr<State>*>(pointer);
        auto state=std::move(*owned);delete owned;
        const auto module=state->module;
        try{state->Run();}catch(...){
            std::lock_guard<std::mutex> lock(state->mutex);
            state->result.status=nr_region::Status::WorkerUnavailable;state->published=true;
        }
        state->stop.store(true,std::memory_order_release);
        state.reset(); // Finish all C++ destructors before atomically dropping module/thread.
        FreeLibraryAndExitThread(module,0);
    }
};
Worker::Worker()=default;
Worker::~Worker(){Stop();}
bool Worker::Start(const std::wstring& runtime,const std::wstring& model) {
    if(state_)return false;
    try {
        if(!std::filesystem::path(runtime).is_absolute()||!std::filesystem::path(model).is_absolute())return false;
        auto state=std::make_shared<State>();state->runtime=runtime;state->model=model;
        state->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!state->event)return false;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&State::Entry),&state->module))return false;
        auto* context=new(std::nothrow)std::shared_ptr<State>(state);
        if(!context){FreeLibrary(state->module);return false;}
        HANDLE thread=CreateThread(nullptr,0,State::Entry,context,0,nullptr);
        if(!thread){delete context;FreeLibrary(state->module);return false;}
        SetThreadPriority(thread,THREAD_PRIORITY_BELOW_NORMAL);CloseHandle(thread);
        state_=std::move(state);return true;
    }catch(...){return false;}
}
bool Worker::Submit(const Reference& r) {
    if(!state_||state_->stop.load()||!r.frame.Valid()||r.width<32||r.height<32||r.width>512||r.height>512||r.width%32||r.height%32)return false;
    std::unique_lock<std::mutex> lock(state_->mutex,std::try_to_lock);
    if(!lock.owns_lock())return false;
    state_->reference.frame=r.frame;state_->reference.width=r.width;state_->reference.height=r.height;
    std::copy_n(r.bgr.data(),r.width*r.height*3,state_->reference.bgr.data());state_->pending=true;
    SetEvent(state_->event);return true;
}
bool Worker::Take(Result& result) {
    if(!state_)return false;
    std::unique_lock<std::mutex> lock(state_->mutex,std::try_to_lock);
    if(!lock.owns_lock()||!state_->published)return false;
    result=state_->result;state_->published=false;return true;
}
void Worker::Stop(){if(state_){state_->stop.store(true,std::memory_order_release);SetEvent(state_->event);state_.reset();}}
bool Worker::Accepting() const{return state_&&!state_->stop.load(std::memory_order_acquire);}
}

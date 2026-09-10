#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/nr_face_worker.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
unsigned checks=0;
void Check(bool condition,const char* name){++checks;if(!condition)throw std::runtime_error(name);}
nr_region::Frame TestFrame(){return {1,1,100,1920,1080,0,0,960,540,nr_region::Route::BeforeSr,nr_region::ColorDomain::Encoded709,1};}
int wmain(int argc,wchar_t** argv) {
    try {
        using namespace nr_region;
        auto frame=TestFrame();Check(frame.Valid(),"valid frame");
        Input input{};input.source=Source::BeforeNrDetector;input.reference=frame;input.confidence=.8f;input.coordinates_verified=true;
        Check(input.For(frame)==Status::Available,"same frame usable");
        auto current=frame;current.frame+=2;
        Check(input.For(current)==Status::MissingMotion,"age is not proof of motion");
        input.proven_history_steps=2;Check(input.For(current)==Status::Available,"complete history");
        current.frame+=kMaxHistory;Check(input.For(current)==Status::Stale,"late results discarded");
        current=frame;current.epoch++;Check(input.For(current)==Status::ContractChanged,"scene cut");
        current=frame;current.width--;Check(input.For(current)==Status::ContractChanged,"DRS invalidation");
        current=frame;current.color=ColorDomain::Pq2020;Check(input.For(current)==Status::ContractChanged,"color contract change");
        current=frame;current.exposure_contract++;Check(input.For(current)==Status::ContractChanged,"exposure contract change");
        input.proven_history_steps=0;input.source=Source::ModelDifference;
        Check(input.For(frame,true)==Status::UntrustedSource,"model change cannot prove reflection");
        input.source=Source::VerifiedGameReflection;
        Check(input.For(frame,true)==Status::UntrustedSource,"label alone cannot prove reflection");
        input.producer_verified=true;Check(input.For(frame,true)==Status::Available,"verified resource adapter");
        Check(FaceEdit(.8f,.2f,1,true,0)==.2f,"C1 caps bright edit");
        Check(FaceEdit(-.8f,-.2f,1,true,0)==-.2f,"C1 caps dark edit");
        Check(FaceEdit(-.8f,.2f,1,true,0)==0,"C1 prevents regional sign inversion");
        Check(FaceEdit(.1f,.2f,1,true,0)==.1f,"C1 preserves smaller edit");
        Check(FaceEdit(.8f,.2f,0,true,1)==.8f,"outside face remains unchanged");
        Check(FaceEdit(.8f,.2f,1,false,1)==0,"original endpoint independent of cap");
        Check(std::abs(FaceEdit(.8f,.2f,.5f,true,.5f)-.375f)<1e-6f,"partial feather and original protection");
        auto selection=Select(.2f,.05f);
        Check(selection.bright==1&&selection.dark==0&&std::abs(selection.broad-.25f)<1e-6f,"fixed input selection");
        if(argc!=3)throw std::runtime_error("Supply pinned runtime DLL and prepared model");
        auto reference=std::make_unique<nr_face::Reference>();reference->frame=frame;reference->width=192;reference->height=192;
        nr_face::Worker worker;Check(worker.Start(argv[1],argv[2]),"worker start");
        Check(!worker.Start(argv[1],argv[2]),"no duplicate worker");
        Check(worker.Submit(*reference),"enqueue before init completes");
        nr_face::Result result{};bool got=false;
        for(unsigned i=0;i<1000&&!got;++i){got=worker.Take(result);if(!got)Sleep(2);}
        Check(got&&result.status==Status::NoFace&&result.count==0,"real ORT blank has no face");
        Check(result.frame.frame==100&&result.frame.context==1,"result identity retained");
        reference->frame.frame=101;
        bool submitted=false;for(unsigned i=0;i<100&&!submitted;++i){submitted=worker.Submit(*reference);if(!submitted)Sleep(1);}
        Check(submitted,"second request accepted");
        // Detach while inference may be active. The owned worker/module outlives this handle.
        const auto start=std::chrono::steady_clock::now();worker.Stop();
        Check(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<20,"stop is nonblocking");
        Check(!worker.Submit(*reference)&&!worker.Take(result),"stopped worker refuses work");
        Check(worker.Start(argv[1],L"C:/nr-test-does-not-exist.onnx"),"missing-model startup is asynchronous");
        got=false;for(unsigned i=0;i<1000&&!got;++i){got=worker.Take(result);if(!got)Sleep(2);}
        Check(got&&result.status==Status::WorkerUnavailable,"missing model reported without fabricated result");
        worker.Stop();Sleep(10);
        std::cout<<"Face worker and region contract: "<<checks<<" checks PASS; real CPU ORT only, no GPU/game benchmark\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}

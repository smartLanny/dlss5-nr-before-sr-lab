#include <cstdio>
#include <stdexcept>
#include "nr_late_present_receipt.hpp"

using namespace nr_late_present;
unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}

Candidate Good(Id stream=1,Id sequence=4,Id opportunity=10) {
    Candidate c{};c.stream=stream;c.epoch=7;c.sequence=sequence;c.latest_sequence=sequence;
    c.opportunity=opportunity;c.latest_device_opportunity=opportunity;c.device=11;c.producer_queue=22;
    c.producer_fence_value=33;c.config_generation=44;c.output_width=1920;c.output_height=1080;
    c.recorded=c.native_result_known=c.native_succeeded=c.submitted=true;return c;
}
Request Present(){Request r{};r.device=11;r.output_width=1920;r.output_height=1080;r.config_generation=44;return r;}

int main(){try{
    auto request=Present();Check(Select(request,nullptr,0).decision==Decision::NoReceipt,"no guide receipt means no work");
    auto candidate=Good();auto result=Select(request,&candidate,1);
    Check(result&&result.index==0&&result.age==0&&result.device_age==0,"current submitted native receipt accepted");
    candidate.latest_sequence=5;candidate.latest_device_opportunity=11;
    result=Select(request,&candidate,1);Check(result&&result.age==1&&result.device_age==1,"previous opportunity accepted with explicit age");
    candidate.latest_sequence=6;result=Select(request,&candidate,1);Check(result.decision==Decision::Expired,"per-producer age two expires");
    candidate=Good();candidate.latest_device_opportunity=12;
    Check(Select(request,&candidate,1).decision==Decision::Expired,"inactive producer expires after two device opportunities");

    Candidate same_stream[2]={Good(1,4,10),Good(1,5,11)};
    same_stream[0].latest_sequence=same_stream[1].latest_sequence=5;
    same_stream[0].latest_device_opportunity=same_stream[1].latest_device_opportunity=11;
    result=Select(request,same_stream,2);Check(result&&result.index==1,"latest submitted receipt wins within one producer stream");
    Candidate parallel[2]={Good(1,4,10),Good(2,1,11)};
    parallel[0].latest_device_opportunity=parallel[1].latest_device_opportunity=11;
    Check(Select(request,parallel,2).decision==Decision::Ambiguous,"two fresh shape-compatible producers remain ambiguous");

    auto flags=request;flags.known_generated=true;Check(Select(flags,&candidate,1).decision==Decision::KnownGenerated,"known generated backbuffer rejected");
    flags=request;flags.known_fg_consumed=true;Check(Select(flags,&candidate,1).decision==Decision::KnownFgConsumed,"known FG-consumed backbuffer rejected");
    flags=request;flags.known_inline_processed=true;Check(Select(flags,&candidate,1).decision==Decision::KnownInlineProcessed,"known inline-processed backbuffer rejected");

    candidate=Good();candidate.native_result_known=false;Check(Select(request,&candidate,1).decision==Decision::NativeResultPending,"pre-call copy is ineligible");
    candidate.native_result_known=true;candidate.native_succeeded=false;Check(Select(request,&candidate,1).decision==Decision::NativeFailed,"failed native result is ineligible");
    candidate.native_succeeded=true;candidate.submitted=false;Check(Select(request,&candidate,1).decision==Decision::Unsubmitted,"unsubmitted receipt is ineligible");
    candidate.submitted=true;candidate.device=12;Check(Select(request,&candidate,1).decision==Decision::WrongDevice,"device mismatch rejected");
    candidate.device=11;candidate.output_width=1280;Check(Select(request,&candidate,1).decision==Decision::WrongExtent,"output extent mismatch rejected");
    candidate.output_width=1920;candidate.config_generation=43;Check(Select(request,&candidate,1).decision==Decision::WrongConfig,"old config receipt rejected");
    candidate.config_generation=44;candidate.consumed=true;Check(Select(request,&candidate,1).decision==Decision::AlreadyConsumed,"consumed receipt cannot retry");
    std::printf("late Present receipt policy: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}

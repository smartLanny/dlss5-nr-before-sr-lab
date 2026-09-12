#include "../src/nr_compat_present_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace nr_compat_present;
unsigned checks=0;
void check(bool value){++checks;if(!value)throw std::runtime_error("check "+std::to_string(checks));}
Gate legal(){Gate g{};g.master=g.selected=g.finite_strength=g.nonzero_strength=g.application_present=g.full_rect=g.only_swapchain=g.d3d12=g.color_known=g.device_and_queue=g.prior_inline_drained=g.color_only_consent=g.fg_off_confirmation=true;return g;}
int main(){
    check(Admit(legal())==Status::Preparing);
    struct Failure{bool Gate::*member;bool value;Status expected;};
    const Failure failures[]={
      {&Gate::master,false,Status::Off},{&Gate::selected,false,Status::Off},
      {&Gate::finite_strength,false,Status::Off},{&Gate::nonzero_strength,false,Status::Off},
      {&Gate::application_present,false,Status::UnsupportedSurface},{&Gate::full_rect,false,Status::UnsupportedSurface},
      {&Gate::only_swapchain,false,Status::UnsupportedSurface},{&Gate::d3d12,false,Status::UnsupportedApi},
      {&Gate::color_known,false,Status::UnsupportedSurface},{&Gate::device_and_queue,false,Status::WrongDeviceOrQueue},
      {&Gate::other_nr,true,Status::OtherNrOwner},{&Gate::hard_fault,true,Status::Poisoned},
      {&Gate::prior_inline_drained,false,Status::Switching},{&Gate::color_only_consent,false,Status::AwaitingConsent},
      {&Gate::fg_off_confirmation,false,Status::FrameGenerationBlocked},{&Gate::observed_fg,true,Status::FrameGenerationBlocked}};
    for(auto f:failures){auto g=legal();g.*(f.member)=f.value;check(Admit(g)==f.expected);}
    auto bad=legal();bad.hard_fault=true;bad.color_only_consent=true;check(Admit(bad)==Status::Poisoned);
    bad.master=false;check(Admit(bad)==Status::Off);
    check(WorkExtent({1920,1080},100)==Extent{1920,1080});check(WorkExtent({1920,1080},50)==Extent{960,540});
    check(WorkExtent({3840,2160},75)==Extent{2880,1620});check(WorkExtent({1919,1079},50)==Extent{960,540});
    for(unsigned p:{0u,1u,49u,101u,1000u,UINT32_MAX})check(!Valid(WorkExtent({1920,1080},p)));
    for(auto e:{Extent{0,5},Extent{5,0},Extent{16385,1},Extent{1,16385},Extent{UINT32_MAX,UINT32_MAX}})check(!Valid(WorkExtent(e,100)));
    check(Valid(Options{}));check(!Valid(Options{false,false,false,49}));
    check(Fits(100,50,150));check(!Fits(100,51,150));check(!Fits(151,1,150));check(!Fits(0,UINT64_MAX,UINT64_MAX));check(!Fits(0,0,20));
    Timeline t;check(!t.Ready(0));check(t.Drained(0));check(t.Free(0)==0);check(t.Reserve(3)==0);
    check(!t.Submitted(0,1,true));auto f=t.Reserve(0);check(f==1);check(t.Submitted(0,f,true));
    check(!t.Ready(0));check(t.Ready(1));check(!t.Drained(0));check(t.Drained(1));check(t.Free(0)==1);
    auto f2=t.Reserve(1);check(t.Submitted(1,f2,false));auto f3=t.Reserve(2);check(t.Submitted(2,f3,false));
    check(t.Free(0)==-1);check(t.Free(1)==0);check(!t.Submitted(2,f2,false));
    check(t.Free(UINT64_MAX)==-1);check(!t.Ready(UINT64_MAX));check(!t.Drained(UINT64_MAX));
    t.Poison();check(t.Poisoned());check(t.Free(999)==-1);check(!t.Drained(999));check(t.Reserve(0)==0);check(!t.Submitted(0,4,false));
    // Serial generation of three distinct allocator slots, no reuse before the
    // exact fence; the shared scratch chronology is the same queue sequence.
    Timeline serial;
    std::uint64_t completed=0;
    for(unsigned i=0;i<50;++i){int slot=serial.Free(completed);check(slot>=0);
        const auto value=serial.Reserve(static_cast<unsigned>(slot));check(serial.Submitted(static_cast<unsigned>(slot),value,i==0));
        if(i>=2)completed=value-2;
    }
    check(!serial.Drained(completed));check(serial.Drained(serial.Last()));
    for(double x:{0.0,1e-6,1e-4,0.01,0.1,0.5,1.0})check(std::abs(DecodePq(EncodePq(x))-x)<1e-8);
    check(std::abs(DecodePq(EncodePq(80.0/10000.0))*125.0-1.0)<1e-8);
    for(unsigned i=0;i<=static_cast<unsigned>(Status::Poisoned);++i)check(std::string(Name(static_cast<Status>(i)))!="invalid");
    check(std::string(Name(static_cast<Status>(999)))=="invalid");
    std::cout<<checks<<" policy/extent/fence/color-reference checks passed; no GPU executed\n";
}

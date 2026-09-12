#include "ledger.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace cascade_test;
struct FakeProvider {
    int decline=-1, fail_create=-1, fail_eval=-1, fail_retire=-1, fail_fence=-1;
    bool wrong_output=false, duplicate_handles=false, pending=false, identity=false;
    unsigned creates=0, composites=0, fences=0;
    std::vector<Evaluation> evaluations;
    std::vector<unsigned> retired;
    std::map<Id,int> pixels{{1,100},{2,10},{3,-1},{4,-1}};
    int final_pixel=-1;
    bool CanRecord(unsigned i) { assert(!pending); return int(i)!=decline; }
    bool Create(unsigned i,Id,Handles& h) {
        assert(!pending);++creates;pending=true;
        h={100+creates,200+creates};
        if(duplicate_handles && i==1) h={101,201};
        return int(i)!=fail_create;
    }
    bool Evaluate(const Evaluation& e,Id& out) {
        assert(!pending && e.handles.feature && e.handles.params);
        assert(e.synthetic && e.input!=e.output && e.output!=e.immutable_proxy);
        pending=true;evaluations.push_back(e);
        if(int(e.layer)==fail_eval) return false;
        pixels[e.output]=pixels.at(e.input)+(identity?0:int(e.layer)+1);
        out=wrong_output?999:e.output;return true;
    }
    bool Complete(unsigned layer,Operation) {
        assert(pending);++fences;
        if(int(layer)==fail_fence) return false;
        pending=false;return true;
    }
    bool Retire(unsigned i,Handles h) {
        assert(!pending && h.feature && h.params);retired.push_back(i);
        return int(i)!=fail_retire;
    }
    bool Composite(Id original,Id proxy,Id last,float strength) {
        assert(!pending && original==1 && proxy==2);
        ++composites;pending=true;
        final_pixel=pixels.at(original)+int((pixels.at(last)-pixels.at(proxy))*strength);
        return true;
    }
};
Request RequestFor(Id frame,unsigned layers=3) {
    Request q;q.view=7;q.queue=9;q.source_epoch=1;q.source_frame=frame;
    q.original=1;q.proxy=2;q.ping_a=3;q.ping_b=4;q.width=16;q.height=16;
    q.layers=layers;q.tuning={11,12,13};q.synthetic=true;q.static_source=true;
    q.permit_unverified_static_experiment=true;q.source_phase=Phase::KnownStaticZero;
    return q;
}
int main() {
    unsigned scenarios=0;
    {
        Ledger ledger;FakeProvider p;p.identity=true;
        const auto r=ledger.Run(RequestFor(1),p);
        assert(r.status==Status::Complete && r.completed_layers==3 && r.composed);
        assert(p.evaluations.size()==3 && p.final_pixel==100 && p.pixels.at(2)==10);
        assert(p.pixels.at(3)==10 && p.pixels.at(4)==10);++scenarios;
    }
    {
        Ledger ledger;FakeProvider p;auto q=RequestFor(1);
        const auto r=ledger.Run(q,p);
        assert(r.status==Status::Complete && r.completed_layers==3 && r.last_completed_output==3 && r.composed);
        assert(p.creates==3 && p.evaluations.size()==3 && p.composites==1 && p.final_pixel==106 && p.pixels.at(2)==10);
        assert(p.evaluations[0].input==2 && p.evaluations[1].input==3 && p.evaluations[2].input==4);
        assert(p.evaluations[0].handles.feature!=p.evaluations[1].handles.feature);
        assert(p.evaluations[1].input_phase==Phase::Unverified && p.evaluations[2].reset);
        const auto duplicate=ledger.Run(q,p);
        assert(duplicate.status==Status::Rejected && p.evaluations.size()==3);
        q.source_frame=2;assert(ledger.Run(q,p).status==Status::Complete);
        assert(p.creates==3 && p.evaluations.size()==6 && ledger.layer(0).successful_history_steps==1);
        ++scenarios;
    }
    {
        Ledger ledger;FakeProvider p;
        for(unsigned count:{1u,2u,3u,1u}) {
            auto q=RequestFor(p.composites+1,count);
            assert(ledger.Run(q,p).status==Status::Complete);
        }
        assert(p.creates==3 && p.evaluations.size()==7 && p.retired==std::vector<unsigned>({1,2}));
        auto q=RequestFor(5,2);assert(ledger.Run(q,p).status==Status::Complete);
        assert(p.creates==4 && ledger.layer(1).generation==2 && ledger.layer(0).generation==1);
        ++scenarios;
    }
    for(int layer=0;layer<3;++layer) {
        Ledger ledger;FakeProvider p;p.fail_eval=layer;
        auto r=ledger.Run(RequestFor(1),p);
        assert(r.status==Status::Poisoned && r.completed_layers==unsigned(layer) && !r.composed);
        assert(p.evaluations.size()==unsigned(layer+1) && p.composites==0 && p.retired.empty());
        const auto calls=p.evaluations.size();
        p.fail_eval=-1;assert(ledger.Run(RequestFor(2),p).status==Status::Poisoned);
        assert(p.evaluations.size()==calls && p.retired.empty());++scenarios;
    }
    for(int layer=0;layer<3;++layer) {
        Ledger ledger;FakeProvider p;p.fail_create=layer;
        const auto r=ledger.Run(RequestFor(1),p);
        assert(r.status==Status::Poisoned && p.evaluations.size()==unsigned(layer));
        assert(ledger.layer(layer).handles.feature && p.retired.empty());++scenarios;
    }
    for(int layer=0;layer<3;++layer) {
        Ledger ledger;FakeProvider p;p.decline=layer;
        const auto r=ledger.Run(RequestFor(1),p);
        assert(r.status==Status::HostDeclined && !ledger.poisoned() && r.completed_layers==unsigned(layer));
        assert(r.last_completed_output==(layer==0?2u:layer==1?3u:4u));
        assert(p.composites==(layer==0?0u:1u));++scenarios;
    }
    {
        Ledger ledger;FakeProvider p;auto q=RequestFor(1);q.mock_temporal_history_only=true;q.reset_all=false;
        assert(ledger.Run(q,p).status==Status::Complete);
        q.source_frame=2;assert(ledger.Run(q,p).status==Status::Complete);
        assert(!p.evaluations[3].reset && !p.evaluations[4].reset && ledger.layer(1).successful_history_steps==2);
        q.source_frame=3;q.tuning[1]=22;assert(ledger.Run(q,p).status==Status::Complete);
        assert(!p.evaluations[6].reset && p.evaluations[7].reset && p.evaluations[8].reset);
        assert(ledger.layer(0).generation==1 && ledger.layer(1).generation==2 && ledger.layer(2).generation==1);
        q.source_frame=4;q.source_epoch=2;assert(ledger.Run(q,p).status==Status::Complete);
        assert(p.evaluations[9].reset && p.evaluations[10].reset && p.evaluations[11].reset);
        q.source_frame=5;q.width=32;assert(ledger.Run(q,p).status==Status::Complete);
        assert(ledger.layer(0).generation==2 && ledger.layer(2).generation==2);++scenarios;
    }
    {
        Ledger ledger;FakeProvider p;auto q=RequestFor(1);q.strength=0;
        assert(ledger.Run(q,p).status==Status::Identity && !p.creates && p.evaluations.empty() && !p.composites);
        q.source_frame=2;q.strength=1;ledger.Run(q,p);
        q.source_frame=3;q.strength=0;q.layers=1;ledger.Run(q,p);
        assert(p.creates==3 && p.retired.empty() && p.evaluations.size()==3);++scenarios;
    }
    for(int failure=0;failure<4;++failure) {
        Ledger ledger;FakeProvider p;
        if(failure==0)p.fail_fence=1;
        if(failure==1)p.duplicate_handles=true;
        if(failure==2)p.wrong_output=true;
        if(failure==3){ledger.Run(RequestFor(1),p);p.fail_retire=1;}
        auto q=RequestFor(failure==3?2:1,failure==3?1:3);
        assert(ledger.Run(q,p).status==Status::Poisoned);++scenarios;
    }
    for(int invalid=0;invalid<5;++invalid) {
        Ledger ledger;FakeProvider p;auto q=RequestFor(1);
        if(invalid==0)q.layers=4;
        if(invalid==1)q.proxy=q.ping_a;
        if(invalid==2)q.source_phase=Phase::Unverified;
        if(invalid==3)q.reset_all=false;
        if(invalid==4)q.synthetic=false;
        assert(ledger.Run(q,p).status==Status::Rejected && !p.creates && p.evaluations.empty());++scenarios;
    }
    std::printf("PASS %u test-only cascade ledger scenarios; fake calls only, no NGX or GPU.\n",scenarios);
}

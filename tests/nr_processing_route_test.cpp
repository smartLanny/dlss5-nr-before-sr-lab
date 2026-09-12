#include "nr_processing_route.h"
#include <cassert>
#include <iostream>
using namespace nr_processing;
int main(){
    Session s;
    assert(s.Current()==Start::Before);
    assert(s.Reject(Start::Before,1,"format",true));
    assert(s.Current()==Start::After && s.Requested()==Start::Before);
    assert(!s.Reject(Start::Before,1,"late duplicate"));
    assert(!s.Select(Start::Before,0)); // ordinary config reload/resize cannot promote
    s.Adopted(Start::After);
    for(unsigned i=2;i<1000;++i){assert(s.Ready(Start::After,i));assert(!s.Select(Start::Before,0));}
    assert(std::strcmp(s.Reason(),"format")==0);
    assert(s.Reject(Start::After,1000,"command-state"));
    assert(s.Current()==Start::Present);
    assert(!s.Reject(Start::Present,1000,"busy"));
    assert(!s.Reject(Start::Present,1000,"duplicate"));
    assert(!s.Reject(Start::Present,1001,"busy"));
    assert(!s.Reject(Start::Present,1002,"busy"));
    assert(!s.Ready(Start::Present,1009));assert(s.Ready(Start::Present,1010));
    assert(s.Select(Start::Before,1));assert(s.Current()==Start::Before);
    assert(s.Select(Start::After,2));assert(s.Requested()==Start::After);
    assert(!s.Reject(Start::Before,1200,"cannot go backwards"));
    assert(s.Reject(Start::After,1200,"input"));assert(s.Current()==Start::Present);
    assert(s.Select(Start::Present,3));assert(!s.Reject(Start::Present,0,"no source"));
    assert(s.Ready(Start::Present,1201));
    assert(FromLegacy(1)==Start::After&&FromLegacy(2)==Start::Before);
    std::cout<<"processing route: sticky suffix, reselection, duplicate/late rejection and bounded source retry passed\n";
}

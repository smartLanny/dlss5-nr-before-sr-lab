#include "nr_feedback_single_text.h"
#include <iostream>
#include <stdexcept>
using namespace nr_feedback_single;
int checks=0;
void check(bool ok, const char *why){++checks;if(!ok)throw std::runtime_error(why);}
int main(){
 const auto name=Filename("Helldivers2.exe","beta0.4.5-fix1",2026,9,7,2,34,5,6,42,1);
 check(name=="260907-helldivers2-beta0.4.5-fix1-023405006-42-1.txt","accurate example name");
 check(Filename("C:\\Games\\NBA2K25.EXE","0.4.5beta2",2026,9,7,1,2,3,4,42,2).find("260907-nba2k25-0.4.5beta2-")==0,"game leaf and real version");
 for(const auto &input : {"../evil.exe","C:\\Private\\NBA2K25.exe","...","", "<>:*?|/../", "中文.exe", "CON.exe", "a\nb.exe"}){
  const auto n=Filename(input,input,2026,9,7,2,34,5,6,42,1);
  const std::wstring wide(n.begin(),n.end());check(SafeLeaf(wide.c_str()),"safe filename including reserved leaf");
  check(n.find("Private")==std::string::npos,"no parent path leaked");
 }
 check(!SafeLeaf(L"../outside.txt") && !SafeLeaf(L"C:\\outside.txt"),"path escape rejected");
 check(!SafeLeaf(L".txt") && !SafeLeaf(L"file.log") && !SafeLeaf(nullptr),"invalid leaf rejected");
 check(Filename("game.exe","v",2026,9,7,0,0,0,0,42,1)!=Filename("game.exe","v",2026,9,7,0,0,0,0,42,2),"same-ms serial collision avoided");
 check(Filename("game.exe","v",2026,9,7,0,0,0,0,42,1)!=Filename("game.exe","v",2026,9,7,0,0,0,0,43,1),"cross-process collision avoided");
 Document normal;normal.Append("test=value\n");normal.Finish();check(normal.text.find("text.complete=1")!=std::string::npos,"complete report");
 Document optional;optional.Missing("previous","absent",false);optional.Finish();check(optional.complete && optional.optional_missing==1,"optional missing explicit, not required failure");
 Document required;required.Missing("current","absent",true);required.Finish();check(!required.complete && required.text.find("text.complete=0")!=std::string::npos,"required missing partial");
 Document large;check(!large.Append(std::string(kTextLimit,'x')),"budget rejects whole append");large.Finish();check(large.text.size()<kTextLimit && large.text.find("END DLSS5 FEEDBACK TEXT")!=std::string::npos,"bounded report always closes");
 check(!SafeLeaf(std::wstring(300,L'a').c_str()),"oversized name rejected");
 std::cout<<"single TXT policy: "<<checks<<" checks passed\n";
}

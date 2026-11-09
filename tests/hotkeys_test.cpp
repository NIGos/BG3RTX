#include "../common/hotkeys.h"
#include <cstdio>
#include <string>
int main(){using namespace bg3::hotkeys;int fail=0;
#define CHECK(x) if(!(x)){std::printf("FAIL line %d\n",__LINE__);++fail;}
for(auto s:{"Ctrl+Shift+F9","Alt+F12","A","0","Shift+Alt+Ctrl+Z","None","ctrl+f1"}){auto k=parse(s);CHECK(k.valid);CHECK(parse(format(k)).valid);}
for(auto s:{"","Ctrl","F0","F13","F01","Ctrl++A","Ctrl+Ctrl+A","A+Ctrl","Win+F9","Ctrl+F9+F8","None+A","Ctrl+None"," Ctrl+A","Ctrl+AA"})CHECK(!parse(s).valid);
CHECK(parse("None").key==0);CHECK(!conflict(parse("None"),parse("None")));
CHECK(conflict(parse("Shift+Ctrl+F9"),parse("Ctrl+Shift+F9")));
CHECK(validate(defaults[0],defaults[1]).empty());
CHECK(!validate("Ctrl+Shift+F10",defaults[1]).empty());
CHECK(!validate("Ctrl+Shift+Alt+F10",defaults[1]).empty());
CHECK(!validate(defaults[0],"Ctrl+Shift+F9").empty());
CHECK(!validate("invalid",defaults[1]).empty());
CHECK(validate_change(defaults[0],"invalid").empty());
CHECK(validate_change(defaults[0],"Ctrl+Shift+F10").empty());
CHECK(!validate_change("invalid",defaults[1]).empty());
CHECK(!validate_change(defaults[0],defaults[0]).empty());
CHECK(!validate_change("Ctrl+Shift+F10","invalid").empty());
// hotkey_rtx_hair (Ctrl+Shift+F7): the third binding; every pair of the set must differ.
CHECK(kCount==3&&std::string(ids[2])=="hotkey_rtx_hair"&&std::string(defaults[2])=="Ctrl+Shift+F7");
CHECK(index_of("hotkey_rtx_hair")==2&&index_of("hotkey_dlss5")==1&&index_of("nope")==-1);
CHECK(validate(defaults[0],defaults[1],defaults[2]).empty());
{const std::string d[kCount]={defaults[0],defaults[1],defaults[2]};CHECK(validate_all(d).empty());}
for(int i=0;i<kCount;++i)for(int j=i+1;j<kCount;++j)CHECK(!conflict(parse(defaults[i]),parse(defaults[j])));
CHECK(!validate(defaults[0],defaults[1],"Ctrl+Shift+F9").empty());
CHECK(!validate(defaults[0],defaults[1],"Ctrl+Shift+F8").empty());
CHECK(!validate(defaults[0],defaults[1],"Ctrl+Shift+F10").empty());
CHECK(validate(defaults[0],defaults[1],"None").empty());
{std::string cur[kCount]={defaults[0],defaults[1],defaults[2]};
 CHECK(!validate_change_at(cur,2,"Ctrl+Shift+F9").empty());
 CHECK(!validate_change_at(cur,2,"Ctrl+Shift+F8").empty());
 CHECK(!validate_change_at(cur,0,"Ctrl+Shift+F7").empty());
 CHECK(validate_change_at(cur,2,"Alt+H").empty());
 CHECK(validate_change_at(cur,2,defaults[2]).empty());
 CHECK(!validate_change_at(cur,2,"Ctrl+Shift+F10").empty());
 cur[0]="invalid";CHECK(validate_change_at(cur,2,"Alt+H").empty());}
return fail?1:0;}

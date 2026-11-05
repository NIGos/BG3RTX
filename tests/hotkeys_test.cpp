#include "../common/hotkeys.h"
#include <cstdio>
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
return fail?1:0;}

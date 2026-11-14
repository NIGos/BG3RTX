// CPU-only bridge fixture: injected effects never launch or write real settings.
#include "web_bridge.h"
#include "ui_state.h"
#include "session_logging.h"
#include <cstdio>
#include <fstream>
#include <iterator>
namespace st=bg3::settings;
int main(int argc,char** argv){
  // --json [experimental|needs]: renderer fixtures (release opt-in shown; unmet requirements).
  if((argc==2||argc==3)&&std::string(argv[1])=="--json"){
    const std::string view=argc==3?argv[2]:"";
    launcher::State sample;sample.values=st::preset_values(st::kHigh);sample.preset=st::kHigh;
    if(view=="needs")sample.values[st::knob_index("rt_geometry")]=0;
    launcher::web::Bridge bridge(sample);bridge.set_status("Line\n\"quoted\"\tcontrol\r");
    if(view=="experimental")bridge.handle("{\"type\":\"experimental\",\"value\":true}");
    if(!view.empty())bridge.set_status("");
    std::printf("%s",bridge.state_json().c_str());return 0;
  }
  int checks=0,failed=0,saves=0,plays=0,prepares=0,exports=0;
  // The page's apply wording (instant apply): release rows say "at once", developer rows keep the table's words.
  auto apply_text=[](st::Apply mode,bool release){
    if(!release)return std::string(st::apply_label(mode,false));
    return std::string(mode==st::Apply::Live?"Applies at once":mode==st::Apply::Hitch?"Applies at once; may briefly pause":mode==st::Apply::Restart?"Restart the game to apply":"");
  };
  auto check_line=[&](bool ok,int line){++checks;if(!ok){++failed;std::printf("FAIL check %d at line %d\n",checks,line);}};
#define check(ok) check_line((ok),__LINE__)
  // Instant apply: every change is written when it is made; Play only launches. A failed write changes nothing,
  // stays visible with Retry, and never reaches the launch callback.
  {
    launcher::State ps; ps.caps=~0u; ps.base=st::code_defaults();
    ps.base["dev"]="0"; ps.values=st::preset_values(st::kLow);
    bool fail_save=false; std::string effects;
    launcher::web::Hooks ph;
    ph.save=[&](launcher::State&,const st::KVList&,std::string&){effects+='S';return !fail_save;};
    ph.play=[&](std::string&){effects+='P';return true;};
    launcher::web::Bridge pb(ps,ph); pb.set_can_play(true);
    const int strength=st::knob_index("rt_intensity");
    auto json=pb.handle("{\"type\":\"set\",\"id\":\"rt_intensity\",\"value\":90}");
    check(effects=="S"&&ps.values[strength]==90);
    check(json.find("\"notice\":{\"text\":\"Mod lighting strength: 100% \xE2\x86\x92 90%\",\"also\":[],\"anchor\":\"rt_intensity\",\"undo\":true}")!=std::string::npos);
    check(json.find("\"dirty\"")==std::string::npos&&json.find("\"unsavedCount\"")==std::string::npos&&json.find("\"preview\"")==std::string::npos);
    pb.handle("{\"type\":\"set\",\"id\":\"rt_intensity\",\"value\":90}"); check(effects=="S");   // the same value writes nothing
    pb.handle("{\"type\":\"play\"}"); check(effects=="SP");                                      // nothing left to write
    for(const char* gone:{"save","confirm","cancel","discard"}){   // the Save flow no longer exists
      check(pb.handle(std::string("{\"type\":\"")+gone+"\"}").find("Launcher request failed")!=std::string::npos);check(effects=="SP");
    }
    pb.set_can_play(true); effects.clear(); fail_save=true;
    json=pb.handle("{\"type\":\"set\",\"id\":\"rt_intensity\",\"value\":80}");
    check(effects=="S"&&ps.values[strength]==90);                                                 // the page keeps showing what is on disk
    check(json.find("\"writeError\":{\"text\":\"Settings not saved. Nothing was changed.\",\"retry\":true}")!=std::string::npos);
    check(json.find("\"notice\":{\"text\":\"\"")!=std::string::npos);
    pb.handle("{\"type\":\"play\"}"); check(effects=="SP"); effects="S"; pb.set_can_play(true);   // Play still launches what is on disk
    json=pb.handle("{\"type\":\"retry\"}"); check(effects=="SS"&&ps.values[strength]==90&&json.find("\"retry\":true")!=std::string::npos);
    fail_save=false; json=pb.handle("{\"type\":\"retry\"}");
    check(effects=="SSS"&&ps.values[strength]==80&&json.find("\"writeError\":{\"text\":\"\",\"retry\":false}")!=std::string::npos);
    pb.handle("{\"type\":\"retry\"}"); check(effects=="SSS");                                    // nothing waits
    // Undo restores the state before the last change, with one write; it cannot be undone again.
    json=pb.handle("{\"type\":\"undo\"}");
    check(effects=="SSSS"&&ps.values[strength]==90&&json.find("\"notice\":{\"text\":\"Undone\",\"also\":[],\"anchor\":\"\",\"undo\":false}")!=std::string::npos);
    pb.handle("{\"type\":\"undo\"}"); check(effects=="SSSS"&&ps.values[strength]==90);
    // Dependencies at the click: ray-traced GI raises Ray-tracing detail; turning that off lowers GI with it, in
    // one write, named in the notice; Undo brings both back.
    effects.clear();
    const int gi=st::knob_index("gi"),geometry=st::knob_index("rt_geometry");
    json=pb.handle("{\"type\":\"set\",\"id\":\"gi\",\"value\":2}");
    check(effects=="S"&&ps.values[gi]==2&&ps.values[geometry]>=1);
    check(json.find("\"also\":[\"Ray-tracing detail: Off \xE2\x86\x92 Solid surfaces (after a game restart)\"]")!=std::string::npos);
    json=pb.handle("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":0}");
    check(effects=="SS"&&ps.values[geometry]==0&&ps.values[gi]==1);
    check(json.find("\"text\":\"Ray-tracing detail: Solid surfaces \xE2\x86\x92 Off (after a game restart)\"")!=std::string::npos);
    check(json.find("\"also\":[\"Indirect lighting: Ray-traced \xE2\x86\x92 Visible surfaces\"]")!=std::string::npos&&json.find("\"anchor\":\"rt_geometry\"")!=std::string::npos);
    check(st::enforce_needs(ps.values).empty());
    pb.handle("{\"type\":\"undo\"}"); check(effects=="SSS"&&ps.values[geometry]>=1&&ps.values[gi]==2);
    // A dependent lowered with its prerequisite comes back when the prerequisite does (this session).
    pb.handle("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":0}"); check(ps.values[gi]==1);
    json=pb.handle("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":2}");
    check(ps.values[geometry]==2&&ps.values[gi]==2&&json.find("Indirect lighting: Visible surfaces \xE2\x86\x92 Ray-traced")!=std::string::npos);
    // ...unless the player set the dependent himself in between.
    pb.handle("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":0}");
    pb.handle("{\"type\":\"set\",\"id\":\"gi\",\"value\":0}");
    pb.handle("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":1}"); check(ps.values[gi]==0);
    pb.handle("{\"type\":\"dismiss\"}"); check(pb.state_json().find("\"notice\":{\"text\":\"\"")!=std::string::npos);
    pb.handle("{\"type\":\"undo\"}"); check(ps.values[geometry]==1);                              // a dismissed notice has no Undo
  }
  {
    launcher::State runtime;runtime.base={{"dev","0"}};runtime.values=st::preset_values(st::kHigh);
    const std::string chosen="C:/User runtime/nvngx_dlssnr.dll";int writes=0;
    launcher::web::Hooks rh;rh.browseDlss5Runtime=[&](){return chosen;};
    rh.save=[&](launcher::State&,const st::KVList&,std::string&){++writes;return true;};
    launcher::web::Bridge rb(runtime,rh);
    rb.handle("{\"type\":\"browseDlss5Runtime\"}");
    check(runtime.dlss5_runtime_path==chosen&&runtime.loaded_dlss5_runtime_path==chosen&&writes==1);
    check(rb.state_json().find("C:/User runtime/nvngx_dlssnr.dll")!=std::string::npos);
    rb.handle("{\"type\":\"browseDlss5Runtime\"}");check(writes==1);   // the same file again (or a cancelled picker) writes nothing
    rb.handle("{\"type\":\"undo\"}");check(runtime.dlss5_runtime_path.empty()&&writes==2);
    rb.handle("{\"type\":\"browseDlss5Runtime\"}");check(writes==3);
    rb.handle("{\"type\":\"clearDlss5Runtime\"}");check(runtime.dlss5_runtime_path.empty()&&writes==4);
    rb.handle("{\"type\":\"clearDlss5Runtime\"}");check(writes==4);
    rb.handle("{\"type\":\"undo\"}");check(runtime.dlss5_runtime_path==chosen&&writes==5);
  }
  {
    launcher::State ks;ks.base={{"dev","0"}};ks.values=st::preset_values(st::kHigh);
    launcher::web::Hooks kh;int writes=0;std::string written;
    kh.save=[&](launcher::State& c,const st::KVList&,std::string&){++writes;written=c.shortcuts[1];return true;};
    launcher::web::Bridge kb(ks,kh);
    check(kb.state_json().find("\"category\":\"DLSS 5\"")!=std::string::npos);
    check(st::choice_available(*st::find_knob("dlss5_nr"),1,false,false,~0u));
    kb.handle(R"({"type":"set","id":"dlss5_nr","value":1})");
    check(ks.values[st::knob_index("dlss5_nr")]==1&&!kb.experimental&&writes==1);
    kb.handle(R"({"type":"undo"})");check(ks.values[st::knob_index("dlss5_nr")]==0&&writes==2);
    // A rejected key writes nothing; an accepted one is written at once.
    kb.handle(R"({"type":"shortcut","id":"hotkey_dlss5","value":"Ctrl+Shift+F9"})");check(writes==2&&ks.shortcuts[1]=="Ctrl+Shift+F8");
    kb.handle(R"({"type":"shortcut","id":"hotkey_dlss5","value":"Ctrl+Shift+F10"})");check(writes==2);
    auto key_json=kb.handle(R"({"type":"shortcut","id":"hotkey_dlss5","value":"Alt+N"})");check(writes==3&&written=="Alt+N"&&ks.loaded_shortcuts[1]=="Alt+N");
    check(key_json.find("In-game key changed: Ctrl+Shift+F8 \xE2\x86\x92 Alt+N")!=std::string::npos&&key_json.find("\"anchor\":\"hotkey_dlss5\"")!=std::string::npos);
    kb.handle(R"({"type":"resetShortcut","id":"hotkey_dlss5"})");check(ks.shortcuts[1]=="Ctrl+Shift+F8"&&writes==4);
    kb.handle(R"({"type":"resetShortcut","id":"hotkey_dlss5"})");check(writes==4);   // already the default
    kb.handle(R"({"type":"undo"})");check(ks.shortcuts[1]=="Alt+N"&&writes==5);
    kb.handle(R"({"type":"shortcut","id":"hotkey_rt_toggle","value":"None"})");check(ks.shortcuts[0]=="None");
    kb.handle(R"({"type":"undo"})");check(ks.shortcuts[0]=="Ctrl+Shift+F9");
    // hotkey_rtx_hair (5m): the third binding, validated against both others, reset and undone alike.
    check(ks.shortcuts[2]=="Ctrl+Shift+F7"&&kb.state_json().find("\"hotkey_rtx_hair\":\"Ctrl+Shift+F7\"")!=std::string::npos);
    writes=0;
    kb.handle(R"({"type":"shortcut","id":"hotkey_rtx_hair","value":"Ctrl+Shift+F9"})");check(writes==0&&ks.shortcuts[2]=="Ctrl+Shift+F7");
    kb.handle(R"({"type":"shortcut","id":"hotkey_rtx_hair","value":"Alt+N"})");check(writes==0);   // DLSS 5 holds Alt+N
    kb.handle(R"({"type":"shortcut","id":"hotkey_rtx_hair","value":"Ctrl+Shift+F10"})");check(writes==0);
    kb.handle(R"({"type":"shortcut","id":"hotkey_rtx_hair","value":"Alt+H"})");check(writes==1&&ks.shortcuts[2]=="Alt+H");
    check(kb.state_json().find("\"hotkey_rtx_hair\":\"Alt+H\"")!=std::string::npos);
    kb.handle(R"({"type":"shortcut","id":"hotkey_dlss5","value":"Alt+H"})");check(ks.shortcuts[1]=="Alt+N"&&writes==1);   // taken by RTX Hair
    kb.handle(R"({"type":"resetShortcut","id":"hotkey_rtx_hair"})");check(ks.shortcuts[2]=="Ctrl+Shift+F7"&&writes==2);
    kb.handle(R"({"type":"shortcut","id":"hotkey_rtx_hair","value":"None"})");check(ks.shortcuts[2]=="None"&&writes==3);
    kb.handle(R"({"type":"undo"})");check(ks.shortcuts[2]=="Ctrl+Shift+F7"&&writes==4);
  }
  {
    // Hotkeys page: every binding of common/hotkeys.h and its default; a change may not repeat any other binding.
    launcher::State ws;ws.base={{"dev","0"}};ws.values=st::preset_values(st::kHigh);
    launcher::web::Bridge wb(ws);const auto json=wb.state_json();
    for(size_t i=0;i<launcher::kShortcutCount;++i){
      check(json.find(std::string("\"")+bg3::hotkeys::ids[i]+"\":\""+ws.shortcuts[i]+"\"")!=std::string::npos);
      check(json.find(std::string("\"")+bg3::hotkeys::ids[i]+"\":\""+bg3::hotkeys::defaults[i]+"\"",json.find("\"shortcutDefaults\""))!=std::string::npos);
    }
    wb.handle(R"({"type":"shortcut","id":"hotkey_rt_toggle","value":"Ctrl+Shift+F8"})");check(ws.shortcuts[0]=="Ctrl+Shift+F9");
    check(wb.handle(R"({"type":"shortcut","id":"hotkey_unknown","value":"Alt+U"})").find("Unknown keyboard shortcut")!=std::string::npos);
  }
  // One option turns on what it needs: Strand hair on screen from all-off (Ray-tracing detail full) raises
  // Strand hair in RT lighting and Hair shadows; the saved file has the whole user-tested strand set.
  {
    launcher::State hs;hs.caps=~0u;hs.base=st::code_defaults();hs.base["dev"]="1";hs.preset=st::kLow;
    hs.values.assign(st::kKnobCount,0);hs.values[st::knob_index("rt_geometry")]=2;
    std::string file;launcher::web::Hooks hh;
    hh.save=[&](launcher::State& c,const st::KVList&,std::string&){file=st::render_user_ini("",c.preset,c.values,{},st::layers_dev(c.base,{}),&c.loaded);c.loaded=c.values;return true;};
    launcher::web::Bridge hb(hs,hh);
    hb.handle(R"({"type":"experimental","value":true})");
    const int primary=st::knob_index("exp_hair_groom_primary"),groom=st::knob_index("exp_hair_groom"),
              shadows=st::knob_index("exp_native_coverage");
    hb.handle(R"({"type":"set","id":"exp_hair_groom_primary","value":1})");
    check(hs.values[primary]==1&&hs.values[groom]==1&&hs.values[shadows]==1);
    check(hb.state_json().find("\"also\":[\"")!=std::string::npos);   // what was turned on with it is named
    auto saved=st::parse_ini(file);
    for(const auto& kv:st::find_knob("exp_hair_groom_primary")->choices[1].kv)if(kv.key)check(st::lookup(saved,kv.key)==kv.value);
    check(st::lookup(saved,"rt_native_hair")=="1"&&st::lookup(saved,"rt_native_scalp")=="1");
    // Developer mode: an explicitly saved part knob wins over the bundle (non-dev saves the bundle whole, see below).
    hb.handle(R"({"type":"set","id":"exp_hair_fibre","value":1})");
    check(st::lookup(st::parse_ini(file),"rt_hair_fibre")=="1");
    hb.handle(R"({"type":"set","id":"exp_hair_fibre","value":0})");
    {
      auto part=st::parse_ini(file);st::Map loaded=st::code_defaults();loaded["dev"]="1";st::apply_layer(loaded,part);
      check(st::lookup(part,"rt_hair_fibre")=="0"&&st::lookup(loaded,"rt_hair_fibre")=="0");
      check(st::lookup(loaded,"rt_hair_groom_primary")=="1"&&st::lookup(loaded,"rt_hair_groom")=="1"&&st::lookup(loaded,"rt_hair_capture")=="1");
      check(file.find("rt_hair_fibre=")==file.rfind("rt_hair_fibre="));   // the key is written once
    }
    // A need that cannot be met (no linear swept spheres for Strand hair in RT lighting): nothing is raised, the
    // option is not changed and the reply says what to check.
    launcher::State ns=hs;ns.caps=~0u&~st::kCapLinearSweptSpheres;ns.values.assign(st::kKnobCount,0);ns.values[st::knob_index("rt_geometry")]=2;
    launcher::web::Bridge nb(ns,hh);nb.handle(R"({"type":"experimental","value":true})");
    const std::string refused=nb.handle(R"({"type":"set","id":"exp_hair_fibre","value":1})");
    check(ns.values[st::knob_index("exp_hair_fibre")]==0&&ns.values[primary]==0&&ns.values[groom]==0&&ns.values[shadows]==0);
    check(refused.find("was not changed. ")!=std::string::npos);
    // Turning a prerequisite off lowers its dependents with it, at the click.
    hb.handle(R"({"type":"set","id":"exp_native_coverage","value":0})");
    check(hs.values[shadows]==0&&hs.values[groom]==0&&hs.values[primary]==0);
  }
  // User report, installed 0.9.6-hairtest8: RTX Hair could not be turned off. The file (tests/fixtures/launcher/
  // user_hairtest8.ini, the player's own, runtime path replaced) has the experimental opt-in with Strand hair edge
  // resolve on Analytic coverage, which needs RTX Hair On; Save and Play (the page always sends policy "enable")
  // turned RTX Hair back On to meet that need. Turning a prerequisite down now lowers what needs it instead.
  {
    auto slurp=[](const std::string& path){std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());};
    const std::string root=BG3RTX_SOURCE_DIR;
    const std::string user=slurp(root+"/tests/fixtures/launcher/user_hairtest8.ini");
    check(!user.empty());
    auto load=[&](launcher::State& u,const std::string& text){
      u.caps=~0u;u.base_text=slurp(root+"/bg3rtx.ini")+slurp(root+"/data/release.ini");u.base=st::parse_ini(u.base_text);
      u.user_text=text;u.user=st::parse_ini(text);st::Map raw;st::apply_layer(raw,u.base);st::apply_layer(raw,u.user);
      u.values=u.loaded=st::knob_values(raw);u.preset=st::preset_from_id(st::lookup(u.user,"preset"));};
    const int rtx=st::knob_index("exp_hair_groom_primary"),aa=st::knob_index("exp_hair_strand_aa");
    {
      launcher::State us;load(us,user);std::string file;int launched=0,writes=0;
      launcher::web::Hooks uh;
      uh.save=[&](launcher::State& c,const st::KVList& required,std::string&){++writes;file=st::render_user_ini(c.user_text,c.preset,c.values,required);c.user_text=file;c.user=st::parse_ini(file);return true;};
      uh.play=[&](std::string&){++launched;return true;};
      launcher::web::Bridge ub(us,uh);ub.set_can_play(true);
      check(us.preset==st::kUltra&&!ub.dev&&ub.experimental&&us.values[rtx]==1&&us.values[aa]==2);
      // One click, one write: RTX Hair Off and its dependent Off, named at once. It never comes back On by itself.
      const std::string reply=ub.handle(R"({"type":"set","id":"exp_hair_groom_primary","value":0})");
      check(writes==1&&us.values[rtx]==0&&us.values[aa]==0);
      check(reply.find("\"text\":\"RTX Hair: On \xE2\x86\x92 Off\"")!=std::string::npos&&reply.find("\"anchor\":\"exp_hair_groom_primary\"")!=std::string::npos);
      check(reply.find("\"also\":[\"Strand hair edge resolve: Analytic coverage \xE2\x86\x92 Off\"]")!=std::string::npos);
      // What the game and the next launcher start read back: Off, with no strand key left on.
      launcher::State again;load(again,file);
      check(again.values[rtx]==0&&again.values[aa]==0);
      st::Map raw;st::apply_layer(raw,again.base);st::apply_layer(raw,again.user);
      for(const auto& kv:st::kKnobs[rtx].choices[1].kv)if(kv.key)check(st::lookup(raw,kv.key)=="0");
      check(st::lookup(raw,"rr_hair_strand_aa")=="0"&&st::lookup(raw,"experimental_options")=="1");
      // Play launches with that file and writes nothing more.
      ub.handle(R"({"type":"play"})");check(launched==1&&writes==1&&us.values[rtx]==0&&us.values[aa]==0);
      // Turning it back on in the same session is one click; the dependent it took down comes back with it.
      const std::string back=ub.handle(R"({"type":"set","id":"exp_hair_groom_primary","value":1})");
      check(writes==2&&us.values[rtx]==1&&us.values[aa]==2&&back.find("Strand hair edge resolve: Off \xE2\x86\x92 Analytic coverage")!=std::string::npos);
      // Undo of the Off click restores both, in one write.
      ub.handle(R"({"type":"set","id":"exp_hair_groom_primary","value":0})");check(us.values[aa]==0);
      ub.handle(R"({"type":"undo"})");check(writes==4&&us.values[rtx]==1&&us.values[aa]==2);
    }
    // A dependent selected after the prerequisite was turned down raises it again (the newest choice wins).
    launcher::State us;load(us,user);launcher::web::Hooks uh;uh.save=[](launcher::State&,const st::KVList&,std::string&){return true;};
    launcher::web::Bridge ub(us,uh);
    ub.handle(R"({"type":"set","id":"exp_hair_groom_primary","value":0})");
    ub.handle(R"({"type":"set","id":"exp_hair_strand_aa","value":1})");
    check(us.values[rtx]==1&&us.values[aa]==1);
    // Unrelated unmet needs keep the "enable" policy: a file that already had the dependent without its prerequisite.
    std::vector<int> v=st::preset_values(st::kUltra),saved=v;v[st::knob_index("rt_geometry")]=0;saved=v;
    auto kept=st::preview_dependencies(v,st::code_defaults(),~0u,true,false,false,false,&saved);
    check(kept.values[st::knob_index("rt_geometry")]>=1);
    saved=st::preset_values(st::kUltra);
    auto lowered=st::preview_dependencies(v,st::code_defaults(),~0u,true,false,false,false,&saved);
    check(lowered.values[st::knob_index("rt_geometry")]==0&&lowered.values[st::knob_index("gi")]<=1&&lowered.blocked.empty());
    check(st::enforce_needs(lowered.values).empty());   // nothing left that needs what was turned off
  }
  // A failed write shows the host's own message; another change replaces the write that waits.
  {
    launcher::State ss;ss.caps=~0u;ss.base=st::code_defaults();ss.base["dev"]="0";ss.preset=st::kHigh;ss.values=st::preset_values(st::kHigh);
    int writes=0;bool fail=true;launcher::web::Hooks sh;
    sh.save=[&](launcher::State&,const st::KVList&,std::string& error){++writes;if(fail)error="Settings not saved. Nothing was changed.\nWindows error 32";return !fail;};
    launcher::web::Bridge sb(ss,sh);const int geometry=st::knob_index("rt_geometry");
    auto reply=sb.handle(R"({"type":"set","id":"rt_geometry","value":0})");
    check(writes==1&&ss.values[geometry]==2&&reply.find("Windows error 32")!=std::string::npos&&reply.find("\"retry\":true")!=std::string::npos);
    check(st::enforce_needs(ss.values).empty());   // none of its dependents moved either
    fail=false;reply=sb.handle(R"({"type":"set","id":"rt_intensity","value":90})");
    check(writes==2&&ss.values[st::knob_index("rt_intensity")]==90&&reply.find("\"writeError\":{\"text\":\"\",\"retry\":false}")!=std::string::npos);
    sb.handle(R"({"type":"retry"})");check(writes==2&&ss.values[geometry]==2);
  }
  // Displayed version: line 1 of the package's VERSION.txt (scripts/package.ps1), BOM or not; anything else is a
  // developer build of the base version. The bridge shows what the host read.
  {
    using launcher::display_version;
    check(display_version("BG3RTX 0.9.6-hairtest8\r\ncommit cb8b2360\r\nlayer lean\r\n")=="0.9.6-hairtest8");
    check(display_version("\xEF\xBB\xBF" "BG3RTX 1.0.0\n")=="1.0.0"&&display_version("BG3RTX beta-20261111-86ca572b-dirty")=="beta-20261111-86ca572b-dirty");
    const std::string dev=BG3RTX_PRODUCT_VERSION " (developer build)";
    check(display_version("")==dev&&display_version("BG3RTX ")==dev&&display_version("fixture-version\n")==dev&&display_version("BG3RTX <script>\n")==dev);
    launcher::State vs;vs.base={{"dev","0"}};vs.values=st::preset_values(st::kHigh);launcher::web::Bridge vb(vs);
    check(vb.state_json().find("\"version\":\""+dev+"\"")!=std::string::npos);
    vb.set_version(display_version("BG3RTX 0.9.6-hairtest8\n"));
    check(vb.state_json().find("\"version\":\"0.9.6-hairtest8\"")!=std::string::npos);
  }
  check(launcher::web::Bridge::is_collection_intent("{\"type\":\"prepareReport\"}"));
  {
    launcher::State broken;broken.base={{"dev","0"},{"hotkey_rt_toggle","broken"},{"hotkey_dlss5","also broken"}};
    broken.values=st::preset_values(st::kHigh);
    launcher::web::Bridge repair(broken);
    repair.handle(R"({"type":"resetShortcut","id":"hotkey_rt_toggle"})");
    check(broken.shortcuts[0]=="Ctrl+Shift+F9");
    check(repair.handle(R"({"type":"shortcut","id":"hotkey_rt_toggle","value":"Nonsense"})").find("Use Ctrl, Shift or Alt")!=std::string::npos);
    repair.handle(R"({"type":"resetShortcut","id":"hotkey_dlss5"})");
    check(broken.shortcuts[1]=="Ctrl+Shift+F8");
  }
  check(launcher::web::Bridge::is_export_intent("{\"type\":\"exportReport\"}"));
  check(!launcher::web::Bridge::is_export_intent("{\"type\":\"prepareReport\",\"issue\":\"exportReport\"}"));
  check(!launcher::web::Bridge::is_collection_intent("{\"type\":\"exportReport\",\"issue\":\"prepareReport\"}"));
  check(!launcher::web::Bridge::is_collection_intent("{\"type\":\"prepareReport\",\"type\":\"exportReport\"}"));
  check(launcher::web::Bridge::is_cancel_collection_intent("{\"type\":\"closeReport\"}"));
  check(launcher::web::Bridge::is_cancel_collection_intent("{\"type\":\"cancelReport\"}"));
  check(!launcher::web::Bridge::is_cancel_collection_intent("{\"type\":\"exportReport\",\"issue\":\"closeReport\"}"));
  launcher::State s;s.caps=~0u;s.preset=st::kLow;s.values=st::preset_values(st::kLow);s.loaded=s.values;s.base={{"dev","0"}};s.user={{"unmanaged","retained"}};
  launcher::web::Hooks h;
  h.save=[&](launcher::State& candidate,const st::KVList&,std::string&){++saves;check(candidate.user.at("unmanaged")=="retained");return true;};
  h.play=[&](std::string&){++plays;return true;};
  bool last_save=true;
  h.prepareReport=[&](const std::string& issue,bool logs,bool settings,bool system,bool save){++prepares;last_save=save;check(issue=="Line\n\"quoted\" \xe2\x98\x80");check(!logs&&settings&&system);launcher::diagnostics::Snapshot r;r.status=launcher::diagnostics::Status::Complete;r.files={{"ISSUE.txt",issue,""}};if(save)r.files.push_back({"saves/Fixture/Fixture.lsv",std::string("SAVEBYTES\0\xff",11),"Your save",true});r.bytes=issue.size();return r;};
  h.exportReport=[&](const launcher::diagnostics::Snapshot& r,std::string&){++exports;check(r.files[0].text=="Line\n\"quoted\" \xe2\x98\x80");return true;};
  // Retired controls stay absent even with developer/experimental access.
  {
    launcher::State retired; retired.caps=~0u; retired.base={{"dev","1"}};
    retired.values=st::preset_values(st::kUltra);
    launcher::web::Bridge rb(retired); rb.experimental=true;
    check(rb.dev);
    for (const char* id : {"rr_hair_resolve", "exp_cascade_static_only", "perf_hitmat_lists", "perf_hook_state", "perf_submit_batch"}) {
      check(rb.state_json().find(id)==std::string::npos);
      const auto before=retired.values;
      rb.handle(std::string("{\"type\":\"set\",\"id\":\"")+id+"\",\"value\":1}");
      check(retired.values==before);
    }
  }
  launcher::web::Bridge bridge(s,h);
  int visible=0;
  for(const auto& k:st::kKnobs){
    const bool shown=st::knob_visible(k,false);visible+=shown;
    check((bridge.state_json().find("\"id\":\""+std::string(k.id)+"\"")!=std::string::npos)==shown);
    if(!shown){const auto before=s.values;bridge.handle("{\"type\":\"set\",\"id\":\""+std::string(k.id)+"\",\"value\":1}");check(s.values==before);}
    if(shown)for(int c=0;c<st::choice_count(k);++c)if(!st::choice_visible(k,c,false)){
      const auto before=s.values;bridge.handle("{\"type\":\"set\",\"id\":\""+std::string(k.id)+"\",\"value\":"+std::to_string(c)+"}");check(s.values==before);
    }
  }
  check(visible==23);   // + scene_handout (Low-latency scene, 2026-11-14); 22 = 17 + exp_gi_rt_pipeline and exp_gi_rt_pipeline_reorder (2026-11-08) + RTX Hair (0.9.5 release) + its distance detail and motion stability (2026-11-10)
  // RTX Hair (0.9.5, user request): a fresh release profile on capable hardware turns it on with one click on the
  // Lighting page; every prerequisite is raised (none hidden left Off) and Save writes the whole strand bundle.
  {
    launcher::State fresh;fresh.caps=~0u;fresh.base=st::code_defaults();fresh.base["dev"]="0";
    fresh.preset=st::kHigh;fresh.values=st::preset_values(st::kHigh);
    std::string file;launcher::web::Hooks fh;
    fh.save=[&](launcher::State& c,const st::KVList&,std::string&){file=st::render_user_ini("",c.preset,c.values);return true;};
    launcher::web::Bridge fb(fresh,fh);
    const int rtx=st::knob_index("exp_hair_groom_primary");
    const std::string card=fb.state_json();
    check(card.find("\"id\":\"exp_hair_groom_primary\",\"title\":\"RTX Hair\"")!=std::string::npos&&card.find("\"category\":\"Lighting\"")!=std::string::npos);
    {  // 5m: RTX Hair applies while playing (its choices too); the restart banner covers a session started without it
      const auto at=card.find("\"id\":\"exp_hair_groom_primary\"");const auto rtx_card=card.substr(at,card.find("\"max\"",at)-at);
      check(rtx_card.find("\"apply\":\"Applies at once\"")!=std::string::npos&&rtx_card.find("Restart the game")==std::string::npos);
      st::Map baseline=st::code_defaults(),after=baseline;
      for(const auto& kv:st::kKnobs[rtx].choices[1].kv)if(kv.key)after[kv.key]=kv.value;
      check(launcher::ui_state::restart_count(baseline,after)==1);   // rt_hair_physical_origin's startup reservation
      st::Map started_on=after;check(launcher::ui_state::restart_count(started_on,baseline)==0);   // off live
      check(launcher::ui_state::restart_count(baseline,started_on)==1&&launcher::ui_state::restart_count(started_on,after)==0);
    }
    fb.handle("{\"type\":\"set\",\"id\":\"exp_hair_groom_primary\",\"value\":1}");
    check(fresh.values[rtx]==1);
    for(const auto& n:st::kKnobs[rtx].choices[1].need){if(!n.knob)continue;const int j=st::knob_index(n.knob);check(j>=0&&fresh.values[j]>=n.min_choice);}
    for(const char* id:{"rt_geometry","exp_native_coverage"}){const auto& k=st::kKnobs[st::knob_index(id)];
      for(const auto& n:st::kKnobs[st::knob_index("exp_hair_groom")].choices[1].need)if(n.knob&&std::string(n.knob)==id)check(fresh.values[st::knob_index(id)]>=n.min_choice&&st::knob_visible(k,false));}
    const auto saved=st::parse_ini(file);
    for(const char* key:{"rt_hair_groom_primary","rt_hair_groom","rt_hair_capture","rt_hair_fibre"}){
      if(st::lookup(saved,key)!="1")std::printf("RTX Hair save: %s=%s\n",key,st::lookup(saved,key).c_str());
      check(st::lookup(saved,key)=="1");
    }
  }
  // Support edits stay independent of the rendering preset and last-session evidence.
  {
    launcher::State ls;ls.caps=~0u;ls.base={{"dev","0"}};ls.values=st::preset_values(st::kHigh);ls.preset=st::kHigh;
    std::string last_hint="last session was quiet";launcher::web::Hooks lh;lh.reportHint=[&]{return last_hint;};
    launcher::web::Bridge lb(ls,lh);
    lb.handle("{\"type\":\"set\",\"id\":\"detailed_logging\",\"value\":1}");
    check(lb.state_json().find("\"custom\":false")!=std::string::npos);
    lb.handle("{\"type\":\"preset\",\"value\":2}");
    check(ls.values[st::knob_index("detailed_logging")]==1);
    auto json=lb.handle("{\"type\":\"report\"}");check(json.find("\"loggingHint\":\"last session was quiet\"")!=std::string::npos);
    last_hint.clear();lb.handle("{\"type\":\"set\",\"id\":\"detailed_logging\",\"value\":0}");
    json=lb.handle("{\"type\":\"report\"}");check(json.find("\"loggingHint\":\"\"")!=std::string::npos);
  }
  check(launcher::session_logging::enabled("00:00 [cfg] detailed logging: 0\n")==0);
  check(launcher::session_logging::enabled("00:00 [cfg] detailed logging: 1\r\n")==1);
  check(launcher::session_logging::enabled("[cfg] detailed logging: 10\n")==-1);
  check(launcher::session_logging::enabled("old log without marker")==-1);
  check(launcher::session_logging::enabled("[cfg] detailed logging: 0\n[cfg] detailed logging: 1\n")==1);
  // Release opt-in: the switch shows exactly settings::kOptInKnobs on their own page,
  // is saved with the settings and Off restores the qualified Off values.
  {
    launcher::State os;os.caps=~0u;os.preset=st::kHigh;os.values=st::preset_values(st::kHigh);os.base={{"dev","0"}};
    os.user_text="unmanaged=retained\nexperimental_options=0\n";std::string saved_text;int os_saves=0;
    launcher::web::Hooks oh;oh.save=[&](launcher::State& candidate,const st::KVList&,std::string&){++os_saves;saved_text=candidate.user_text;return true;};
    launcher::web::Bridge ob(os,oh);check(!ob.experimental);
    check(ob.state_json().find("\"id\":\"water_surface_fresnel\"")==std::string::npos);
    ob.handle("{\"type\":\"set\",\"id\":\"exp_hair_fibre_gi\",\"value\":1}");check(os.values[st::knob_index("exp_hair_fibre_gi")]<=0);
    auto shown=ob.handle("{\"type\":\"experimental\",\"value\":true}");
    check(ob.experimental&&os_saves==1&&saved_text.find("experimental_options=1\n")!=std::string::npos&&saved_text.find("experimental_options=0")==std::string::npos&&saved_text.find("unmanaged=retained")!=std::string::npos);
    for(const auto& k:st::kKnobs){
      bool listed=shown.find("\"id\":\""+std::string(k.id)+"\"")!=std::string::npos;
      check(listed==(st::knob_visible(k,false)||st::opt_in_knob(k)));
    }
    auto fresnel=shown.substr(shown.find("\"id\":\"water_surface_fresnel\""));
    check(fresnel.find("\"category\":\"Experimental\"")<fresnel.find("\"choices\"")&&fresnel.find("\"developer\":false")<fresnel.find("\"choices\""));
    check(shown.find("exp_compact_hit_state")==std::string::npos&&shown.find("exp_alpha_metadata_cache")==std::string::npos&&shown.find("exp_water_empty_tiles")==std::string::npos);
    os.values[st::knob_index("exp_water")]=1;
    ob.handle("{\"type\":\"set\",\"id\":\"water_surface_fresnel\",\"value\":1}");check(os.values[st::knob_index("water_surface_fresnel")]==1&&os_saves==2);
    // Hiding writes at once: the key goes, every opt-in option returns to Off and is named; Undo brings all back.
    const auto hidden=ob.handle("{\"type\":\"experimental\",\"value\":false}");
    check(!ob.experimental&&os.values[st::knob_index("water_surface_fresnel")]==0&&os_saves==3&&saved_text.find("experimental_options")==std::string::npos);
    check(hidden.find("Experimental options hidden and set to Off")!=std::string::npos&&hidden.find("Water surface reflections: On \xE2\x86\x92 Off")!=std::string::npos);
    check(hidden.find("\"id\":\"water_surface_fresnel\"")==std::string::npos);
    ob.handle("{\"type\":\"undo\"}");
    check(ob.experimental&&os.values[st::knob_index("water_surface_fresnel")]==1&&os_saves==4&&saved_text.find("experimental_options=1\n")!=std::string::npos);
    // Low-latency scene is a qualified Performance control: the opt-in switch leaves it alone, in both directions.
    const int scene=st::knob_index("scene_handout");
    ob.handle("{\"type\":\"set\",\"id\":\"scene_handout\",\"value\":1}");check(os.values[scene]==1);
    ob.handle("{\"type\":\"experimental\",\"value\":false}");check(os.values[scene]==1&&os.values[st::knob_index("water_surface_fresnel")]==0);
    ob.handle("{\"type\":\"set\",\"id\":\"scene_handout\",\"value\":0}");check(os.values[scene]==0);
    // A saved opt-in is restored on the next launch.
    launcher::State again=os;again.user={{"experimental_options","1"}};again.values[st::knob_index("water_surface_fresnel")]=1;
    launcher::web::Bridge ab2(again);check(ab2.experimental&&again.values[st::knob_index("water_surface_fresnel")]==1);
    // Release requirement copy: short, unmet only, no duplicates, no met hardware.
    launcher::State rq;rq.caps=~0u;rq.values=st::preset_values(st::kHigh);rq.values[st::knob_index("rt_geometry")]=0;
    launcher::web::Bridge rb2(rq);auto copy=rb2.state_json();
    check(copy.find("\"reason\":\"Needs Ray-tracing detail: Solid surfaces\"")!=std::string::npos);
    check(copy.find("\"reason\":\"Requires")==std::string::npos&&copy.find("\"requirements\":\"Requires")==std::string::npos&&copy.find("Required startup setting")==std::string::npos&&copy.find("GPU requirement")==std::string::npos);
    check(copy.find("Sets at startup: Ray-tracing support; Sets at startup: Ray-tracing support")==std::string::npos);
    check(copy.find("\"reason\":\"Needs hardware")==std::string::npos);
  }
  s.values[st::knob_index("exp_native_coverage")]=1;
  auto coverage=bridge.state_json();auto pos=coverage.find("\"id\":\"exp_native_coverage\"");
  check(pos!=std::string::npos);
  check(coverage.substr(pos,coverage.find("\"choices\"",pos)-pos).find("\"apply\":\"Restart the game to apply\"")!=std::string::npos);
  s.values[st::knob_index("exp_native_coverage")]=0;
  auto json=bridge.state_json();check(json.find("\"type\":\"state\"")!=std::string::npos);check(!bridge.dev);check(json.find("\"id\":\"ls_k\"")==std::string::npos);
  for(auto bad:{"", "[]", "{\"type\":\"discard\"} trailing", "{\"type\":\"discard\",\"type\":\"play\"}", "{\"type\":[]}","{\"type\":\"set\",\"value\":01}","{\"type\":\"set\",\"value\":1.2}","{\"type\":\"set\",\"value\":999999999999999}","{\"type\":\"\\uD800\"}"})check(bridge.handle(bad).find("Launcher request failed. Reopen the launcher.")!=std::string::npos);
  bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":1}");check(saves==1);check(s.values[st::knob_index("rr")]==1);
  bridge.handle("{\"type\":\"play\"}");check(plays==0);bridge.set_can_play(true);bridge.handle("{\"type\":\"play\"}");check(plays==1);check(saves==1);bridge.set_can_play(true);plays=0;saves=0;
  bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":999}");check(s.values[st::knob_index("rr")]==1);
  bridge.handle("{\"type\":\"undo\"}");check(s.values[st::knob_index("rr")]==0&&saves==1);
  // A preset applies at once, in one write, and Undo restores what was there.
  bridge.handle("{\"type\":\"preset\",\"value\":3}");check(s.preset==st::kUltra&&saves==2&&s.values[st::knob_index("gi")]==3);
  check(bridge.state_json().find("\"notice\":{\"text\":\"Preset Ultra applied\"")!=std::string::npos);
  bridge.handle("{\"type\":\"undo\"}");check(s.preset==st::kLow&&saves==3&&s.values[st::knob_index("gi")]==1);
  bridge.handle("{\"type\":\"preset\",\"value\":3}");check(s.preset==st::kUltra&&saves==4);saves=0;
  bridge.handle("{\"type\":\"play\"}");check(plays==1);bridge.handle("{\"type\":\"play\"}");check(plays==1);
  json=bridge.state_json();
  check(json.find("Off-screen bounce light")!=std::string::npos);
  check(json.find("Include hidden roofs")!=std::string::npos);
  check(json.find("Far-field sun bounce")==std::string::npos);
  check(json.find("Opacity micromaps")==std::string::npos);
  check(json.find("Measured: Not measured")==std::string::npos);
  check(json.find("\"cost\":\"Not measured\"")!=std::string::npos);
  check(json.find("Physical hair ray origins")==std::string::npos);
  auto saved=s.values;s.values[st::knob_index("rr")]=-1;bridge.handle("{\"type\":\"preset\",\"value\":0}");check(s.values[st::knob_index("rr")]==0);s.values=saved;
  bridge.handle("{\"type\":\"report\"}");check(prepares==0);bridge.handle("{\"type\":\"exportReport\"}");check(exports==0);
  json=bridge.handle("{\"type\":\"prepareReport\",\"issue\":\"Line\\n\\\"quoted\\\" \\u2600\",\"logs\":false,\"settings\":true,\"system\":true}");check(prepares==1);check(json.find("Line\\u000a\\\"quoted\\\"")!=std::string::npos);bridge.handle("{\"type\":\"exportReport\"}");check(exports==1);
  check(!last_save); // The latest save is opt-in: absent "save" means no save.
  // With the save ticked, its bytes go to the ZIP but never into the preview JSON.
  json=bridge.handle("{\"type\":\"prepareReport\",\"issue\":\"Line\\n\\\"quoted\\\" \\u2600\",\"logs\":false,\"settings\":true,\"system\":true,\"save\":true}");
  check(prepares==2&&last_save);check(json.find("SAVEBYTES")==std::string::npos);check(json.find("\"binary\":true")!=std::string::npos);
  bridge.handle("{\"type\":\"exportReport\"}");check(exports==2);
  launcher::GraphicsInspection p;bridge.set_compatibility(p);check(s.caps==0);bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":1}");check(bridge.state_json().find("\"reason\":\"Needs an NVIDIA RTX GPU with DLSS\"")!=std::string::npos);
  // Restart indicator: the names the host found, and how many startup keys belong to no visible setting.
  bridge.set_restart({"Ray-tracing detail","Water lighting"},1);json=bridge.state_json();
  check(json.find("\"restartNeeded\":true")!=std::string::npos&&json.find("\"restartCount\":3")!=std::string::npos);
  check(json.find("\"restartBanner\":\"Restart the game to apply: Ray-tracing detail, Water lighting and 1 more setting.\"")!=std::string::npos);
  check(json.find("\"restartNames\":[\"Ray-tracing detail\",\"Water lighting\"]")!=std::string::npos);
  bridge.handle("{\"type\":\"dismiss\"}");check(bridge.state_json().find("Restart the game to apply: Ray-tracing detail")!=std::string::npos);   // it stays until the game restarts
  bridge.set_restart({},2);check(bridge.state_json().find("\"restartBanner\":\"Restart the game to apply 2 changes.\"")!=std::string::npos);
  bridge.set_restart({});check(bridge.state_json().find("\"restartNeeded\":false")!=std::string::npos);
  bridge.handle("{\"type\":\"closeReport\"}");check(bridge.state_json().find("\"visible\":false")!=std::string::npos);
  check(bridge.handle(std::string(65537,'x')).find("Launcher request failed. Reopen the launcher.")!=std::string::npos);
  launcher::State dependencies;dependencies.caps=~0u;dependencies.preset=st::kLow;dependencies.values=st::preset_values(st::kLow);dependencies.values[st::knob_index("frame_generation")]=0;dependencies.values[st::knob_index("reflex")]=0;
  int writes=0;launcher::web::Hooks dh;dh.save=[&](launcher::State&,const st::KVList&,std::string&){++writes;return true;};
  launcher::web::Bridge db(dependencies,dh);
  db.handle("{\"type\":\"set\",\"id\":\"frame_generation\",\"value\":1}");
  // The selection raises Reflex with it, in the same write, and says so.
  check(dependencies.values[st::knob_index("reflex")]==1);check(db.state_json().find("NVIDIA Reflex: Off \xE2\x86\x92 On")!=std::string::npos);
  check(writes==1);
  db.handle("{\"type\":\"set\",\"id\":\"local_shadows\",\"value\":3}");
  int hidden_choice=st::knob_index("local_shadows");if(hidden_choice>=0)check(dependencies.values[hidden_choice]!=3);
  launcher::GraphicsInspection multiple;multiple.adapters.resize(2);multiple.adapters[0].gpu.name="First";multiple.adapters[1].gpu.name="Second";db.set_compatibility(multiple);check(!dependencies.have_gpu);db.handle("{\"type\":\"adapter\",\"value\":1}");check(dependencies.have_gpu&&dependencies.gpu.name=="Second");
  // Integrated AMD next to a discrete RTX (the user's PC after a driver reinstall): the RTX is chosen without asking.
  launcher::GraphicsInspection hybrid;hybrid.adapters.resize(2);hybrid.adapters[0].gpu.name="AMD Radeon(TM) Graphics";hybrid.adapters[0].gpu.vendor=0x1002;hybrid.adapters[0].gpu.ray_query=true;
  hybrid.adapters[1].gpu.name="NVIDIA GeForce RTX 5090";hybrid.adapters[1].gpu.vendor=0x10DE;hybrid.adapters[1].gpu.discrete=hybrid.adapters[1].gpu.ray_query=true;
  check(launcher::default_adapter(hybrid)==1);db.set_compatibility(hybrid);check(dependencies.have_gpu&&dependencies.gpu.name=="NVIDIA GeForce RTX 5090");check(db.state_json().find("\"selected\":1")!=std::string::npos);
  // Two equal discrete RTX cards stay a user choice; one adapter is always taken.
  launcher::GraphicsInspection twin;twin.adapters.resize(2);for(auto& a:twin.adapters){a.gpu.vendor=0x10DE;a.gpu.discrete=a.gpu.ray_query=true;}check(launcher::default_adapter(twin)==-1);
  launcher::GraphicsInspection single;single.adapters.resize(1);check(launcher::default_adapter(single)==0);check(launcher::default_adapter(launcher::GraphicsInspection{})==-1);
  if(hidden_choice<0)return 2;dependencies.values[hidden_choice]=3;launcher::web::Bridge legacy(dependencies);check(dependencies.values[hidden_choice]==-1);
  launcher::State pending;pending.caps=~0u;pending.preset=st::kHigh;
  pending.base=st::code_defaults();pending.base["rt_alpha"]="0";
  pending.values=st::preset_values(st::kHigh);pending.values[st::knob_index("rt_geometry")]=1;
  const auto process_baseline=pending.base;
  launcher::web::Bridge* pending_bridge=nullptr;bool fail_save=false;
  launcher::web::Hooks ph;
  ph.save=[&](launcher::State& candidate,const st::KVList&,std::string& error){
    if(fail_save){error="Fixture save failed";return false;}
    auto after=candidate.base;
    for(int i=0;i<st::kKnobCount;++i){st::KVList kv;st::knob_kvs(st::kKnobs[i],candidate.values[i],kv);for(auto& v:kv)after[v.first]=v.second;}
    int others=0;auto names=launcher::ui_state::restart_names(process_baseline,after,&others);
    check(int(names.size())+others==launcher::ui_state::restart_count(process_baseline,after));
    pending_bridge->set_restart(std::move(names),others);return true;
  };
  launcher::web::Bridge pb(pending,ph);pending_bridge=&pb;
  // The restart indicator names the setting, stays through later live changes and failed writes, and goes when the
  // file is back to what the game started with.
  json=pb.handle("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":2}");
  check(json.find("\"restartBanner\":\"Restart the game to apply: Ray-tracing detail.\"")!=std::string::npos&&json.find("\"restartNames\":[\"Ray-tracing detail\"]")!=std::string::npos);
  json=pb.handle("{\"type\":\"set\",\"id\":\"fps_cap\",\"value\":60}");
  check(json.find("Restart the game to apply: Ray-tracing detail.")!=std::string::npos);
  fail_save=true;json=pb.handle("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":1}");
  check(pending.values[st::knob_index("rt_geometry")]==2&&json.find("Fixture save failed")!=std::string::npos&&json.find("Restart the game to apply: Ray-tracing detail.")!=std::string::npos);
  fail_save=false;json=pb.handle("{\"type\":\"retry\"}");
  check(pending.values[st::knob_index("rt_geometry")]==1&&json.find("\"restartNeeded\":false")!=std::string::npos&&json.find("\"restartNames\":[]")!=std::string::npos);
  // The last Experimental edit travels with the write that carries it, never with a failed one.
  launcher::State trial;trial.base={{"dev","1"}};trial.caps=~0u;trial.preset=st::kUltra;trial.values=st::preset_values(st::kUltra);
  st::KVList written;bool reject=false;launcher::web::Hooks th;
  th.save=[&](launcher::State&,const st::KVList& keys,std::string&){if(reject)return false;written=keys;return true;};
  launcher::web::Bridge tb(trial,th);tb.experimental=true;
  reject=true;tb.handle("{\"type\":\"set\",\"id\":\"exp_hair_groom\",\"value\":1}");
  check(written.empty()&&trial.values[st::knob_index("exp_hair_groom")]==0);
  reject=false;tb.handle("{\"type\":\"retry\"}");check(trial.values[st::knob_index("exp_hair_groom")]==1);
  st::Map ab=st::code_defaults();for(auto& kv:written)ab[kv.first]=kv.second;
  check(written.empty()); // Developer-only choices are not release A/B targets.
  // Both hotkeys share the gate; F9 wins even with a saved Experimental target
  // or when both edges arrive together. Target validity never routes F9 to flip.
  using Action=bg3::experimental_ab::HotkeyAction;
  for(bool enabled:{false,true})for(bool master:{false,true})for(bool target:{false,true})
    check(bg3::experimental_ab::hotkey_action(master,target,enabled)==
          (!enabled?Action::None:master?Action::Master:target?Action::Experimental:Action::None));
  check(tb.state_json().find("Live settings apply automatically")==std::string::npos);
  const auto before_master=ab;
  check(bg3::experimental_ab::hotkey_action(true,false,true)==Action::Master);
  check(ab==before_master);
  check(bg3::experimental_ab::hotkey_action(false,true,true)==Action::Experimental);
  std::string line;
  ab["ab_target"]="exp_hair_groom";ab["ab_choice"]="1";
  check(!bg3::experimental_ab::flip(ab,line));
  ab["ab_target"]="rr_post";ab["ab_choice"]="1";
  check(!bg3::experimental_ab::flip(ab,line));
  ab.erase("ab_target");const auto no_target=ab;
  check(!bg3::experimental_ab::flip(ab,line));check(ab==no_target);
  check(bg3::experimental_ab::hotkey_action(true,false,true)==Action::Master);
  ab["ab_target"]="exp_hair_gi_mean";check(!bg3::experimental_ab::flip(ab,line));
  ab["ab_target"]="rr";check(!bg3::experimental_ab::flip(ab,line));
  ab["ab_target"]="exp_native_coverage";ab["ab_choice"]="2";check(!bg3::experimental_ab::flip(ab,line));   // qualified (non-experimental) knobs are never A/B targets
  written.clear();tb.handle("{\"type\":\"set\",\"id\":\"fps_cap\",\"value\":60}");
  bool target_written=false;for(auto& kv:written)target_written|=kv.first=="ab_target";check(!target_written);

  // Unsupported strand activation is rejected independently for every required
  // device/file capability, and the missing prerequisite is visible on the card.
  const auto& primary=st::kKnobs[st::knob_index("exp_hair_groom_primary")];
  for (auto missing : {st::kCapRayQuery, st::kCapLinearSweptSpheres, st::kCapShaderInt64,
                       st::kCapFragmentStores}) {   // no local groom files needed (capture model)
    launcher::State hair;hair.base={{"dev","1"}};hair.caps=~unsigned(missing);hair.values=st::preset_values(st::kUltra);
    int calls=0;launcher::web::Hooks hh;hh.save=[&](launcher::State&,const st::KVList&,std::string&){++calls;return true;};
    launcher::web::Bridge hb(hair,hh);hb.experimental=true;
    hb.handle("{\"type\":\"set\",\"id\":\"exp_hair_groom_primary\",\"value\":1}");
    check(hair.values[st::knob_index(primary.id)]==0);
    auto text=hb.state_json();
    const char* reason=missing==st::kCapLinearSweptSpheres?"linear swept spheres":missing==st::kCapShaderInt64?"shaderInt64":missing==st::kCapFragmentStores?"fragmentStoresAndAtomics":missing==st::kCapHairGrooms?"local hair groom files":"hardware ray queries";
    check(text.find(reason)!=std::string::npos);
    // A file that already has it on: Play neither writes nor launches, and says why.
    hair.values[st::knob_index(primary.id)]=1;hb.set_can_play(true);
    check(hb.handle("{\"type\":\"play\"}").find("The game was not started.")!=std::string::npos);
    check(calls==0);
  }
  launcher::State hair;hair.caps=~0u;hair.base=st::code_defaults();hair.base["dev"]="1";hair.base["sharc_device"]="0";hair.values=st::preset_values(st::kUltra);
  hair.values[st::knob_index(primary.id)]=1;st::KVList startup;
  launcher::web::Hooks hh;hh.save=[&](launcher::State&,const st::KVList& keys,std::string&){startup=keys;return true;};
  launcher::web::Bridge hb(hair,hh);hb.experimental=true;
  // A startup key the file lacks is written before Play, and named.
  hb.set_can_play(true);auto hair_preview=hb.handle("{\"type\":\"play\"}");
  check(hair_preview.find("64-bit shader integers at startup")!=std::string::npos&&hair_preview.find("Settings completed before Play")!=std::string::npos);
  check(std::find(startup.begin(),startup.end(),std::make_pair(std::string("sharc_device"),std::string("1")))!=startup.end());
  // Each Frame Generation choice says how it applies from the current value (on or off: restart; 2x/3x/4x: at once).
  for (int from=0;from<4;++from){
    launcher::State fg;fg.caps=~0u;fg.values=st::preset_values(st::kUltra);auto i=st::knob_index("frame_generation");fg.values[i]=from;
    launcher::web::Bridge fb(fg);auto text=fb.state_json();auto start=text.find("\"id\":\"frame_generation\"");
    auto card=text.substr(start,text.find("\"min\"",start)-start);
    for(int to=0;to<4;++to)
      check(card.find("{\"value\":"+std::to_string(to)+",\"label\":\""+st::kKnobs[i].choices[to].label+"\",\"apply\":\""+apply_text(st::transition_apply(st::kKnobs[i],from,to),true)+"\"")!=std::string::npos);
  }

  // Every serialized card uses the current choice's Apply, and every
  // prerequisite is surfaced even when already satisfied.
  for(int i=0;i<st::kKnobCount;++i){
    auto& k=st::kKnobs[i];
    for(int c=0;c<st::choice_count(k);++c){
      launcher::State qs;qs.caps=~0u;qs.base={{"dev","1"}};qs.values=st::preset_values(st::kUltra);qs.values[i]=c;
      launcher::web::Bridge qb(qs);qb.experimental=true;auto text=qb.state_json();
      auto start=text.find("\"id\":\""+std::string(k.id)+"\"");check(start!=std::string::npos);
      auto end=text.find("\"choices\":[",start);auto card=text.substr(start,end-start);
      check(card.find("\"apply\":\""+apply_text(st::transition_apply(k,c,c),!k.dev_only)+"\"")!=std::string::npos);
      for(auto& n:k.choices[c].need)if(n.knob)check(text.substr(end,text.find("],\"min\"",end)-end).find(k.dev_only?"needs "+std::string(st::kKnobs[st::knob_index(n.knob)].label):std::string(st::kKnobs[st::knob_index(n.knob)].label)+": ")!=std::string::npos);
      for(auto& n:k.choices[c].need)if(n.knob){
        qs.values[st::knob_index(n.knob)]=0;
        int writes=0;launcher::web::Hooks hooks;hooks.save=[&](launcher::State& candidate,const st::KVList&,std::string&){++writes;check(st::unmet_need(k,c,candidate.values).empty());return true;};
        // A file with the prerequisite missing: Play writes it, once, with the need met.
        launcher::web::Bridge dependencies(qs,hooks);dependencies.experimental=true;dependencies.set_can_play(true);
        dependencies.handle("{\"type\":\"play\"}");check(writes==1);
      }
    }
  }
  // A preset replaces every visible custom value at once, retains hidden controls,
  // and matches the central table on fully supported hardware.
  for(int p=0;p<st::kPresetCount;++p){
    launcher::State ps;ps.caps=~0u;ps.preset=st::kLow;ps.values=st::preset_values(st::kLow);
    launcher::web::Bridge qb(ps);ps.values[st::knob_index("rr")]=-1;
    check(qb.state_json().find("\"custom\":true")!=std::string::npos);
    auto old=ps.values;qb.handle("{\"type\":\"preset\",\"value\":"+std::to_string(p)+"}");
    for(int i=0;i<st::kKnobCount;++i)check(ps.values[i]==(st::knob_visible(st::kKnobs[i],false)?st::kKnobs[i].preset[p]:old[i]));
    check(qb.state_json().find("\"custom\":false")!=std::string::npos);
    qb.handle("{\"type\":\"undo\"}");check(ps.values==old&&ps.preset==st::kLow);
  }
  // Preparation: its object is in the state only from the hook; actions go to the hook, never to the settings
  // (nothing is written), and an unknown action or no host reports unavailability.
  {
    launcher::State ps; ps.values=st::preset_values(st::kHigh); ps.preset=st::kHigh;
    std::string got;int value=-1;
    launcher::web::Hooks qh;
    qh.preparation=[]{return std::string("{\"items\":[],\"convertAll\":false,\"whilePlaying\":false,\"paused\":false,\"pausable\":true}");};
    qh.prepare=[&](const std::string& action,int v){got=action;value=v;return action!="bogus";};
    launcher::web::Bridge qb(ps,qh);
    const std::string state=qb.handle("{\"type\":\"prep\",\"action\":\"convertAll\",\"value\":1}");
    check(got=="convertAll"&&value==1&&state.find("\"preparation\":{\"items\":[]")!=std::string::npos);
    check(qb.handle("{\"type\":\"prep\",\"action\":\"bogus\"}").find("Preparation is unavailable")!=std::string::npos);
    launcher::web::Bridge nb(ps);
    check(nb.state_json().find("\"preparation\"")==std::string::npos);
    check(nb.handle("{\"type\":\"prep\",\"action\":\"pause\"}").find("Preparation is unavailable")!=std::string::npos);
  }
  std::printf("%d checks, %d failed\n",checks,failed);return failed?1:0;
}



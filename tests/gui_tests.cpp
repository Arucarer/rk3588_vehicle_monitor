#include "vehicle/gui.hpp"
#include "lvgl.h"
#include <filesystem>
#include <iostream>
using namespace vehicle;
using namespace std::chrono_literals;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
bool contains(const GuiWindow& gui,const std::string& wanted) {
    for(const auto& text:gui.visible_text())if(text.find(wanted)!=std::string::npos)return true;
    return false;
}
int main(int argc,char** argv){try {
    std::string directory=argc>1?argv[1]:"gui-captures";std::filesystem::create_directories(directory);
    auto now=Clock::now();GuiState state;state.updated=now;state.config=Config{};
    state.sensors.speed={72.5,true,now,"km/h","simulation",0};
    state.sensors.temperature={24.0,true,now,"degC","simulation",0};
    state.sensors.humidity={55.0,true,now,"%RH","simulation",0};
    state.sensors.pitch={15.0,true,now,"deg","simulation",0};state.sensors.roll={-2.0,true,now,"deg","simulation",0};
    state.sensors.vibration={0.05,true,now,"g","simulation",0};
    state.sensors.analog["bus"]={24,true,now,"V","simulation",0};state.sensors.digital["liquid"]={0,true,now,"bool","simulation",0};
    state.network_enabled=true;state.network.connected=true;state.can_enabled=true;state.settings_writable=true;
    state.nodes={{1,0,10,true,true,now},{2,2,20,false,true,now}};
    state.faults={{"tilt",FaultState::fault,Severity::fault,false}};
    for(int i=1;i<=6;++i){LogRecord r;r.sequence=i;r.utc_ms=1792483200000;r.type=RecordType::fault;r.payload="fault=speed state="+std::to_string(i%4);r.config_version="fixture-config";state.history.push_back(r);}
    int acknowledged=0;std::string setting;
    GuiWindow gui(true,{[&]{++acknowledged;return true;},[&](const std::string& text){setting=text;return true;}});
    gui.update(state,now);gui.step(50);
    check(contains(gui,"72.5"),"speed missing");check(contains(gui,"告警 1"),"alarm status missing");
    for(unsigned page=0;page<5;++page) {
        gui.navigate(page);gui.update(state,now);gui.step(50);check(gui.page()==page&&gui.layout_valid(),"viewport overflow");
        gui.save_ppm(directory+"/page-"+std::to_string(page)+".ppm");
    }
    gui.navigate(1);gui.update(state,now);check(contains(gui,"24.0 V")&&contains(gui,"0.0 bool"),"analog/digital missing");
    gui.navigate(2);gui.update(state,now);check(contains(gui,"在线")&&contains(gui,"离线"),"CAN states missing");
    gui.navigate(3);gui.update(state,now);gui.step(50);check(contains(gui,"#6"),"latest history missing");
    gui.click(198,263);check(contains(gui,"#4"),"history next page input failed");
    gui.click(30,80);check(contains(gui,"时间未同步"),"history detail failed");
    gui.click(198,68);gui.click(110,263);check(acknowledged==1,"ack button callback failed");
    gui.navigate(4);gui.update(state,now);gui.step(50);
    gui.click(198,263);gui.click(110,263);check(setting=="overspeed_kmh=81","parameter edit/save failed");
    state.settings_writable=false;gui.update(state,now);setting.clear();gui.click(110,263);
    check(setting.empty()&&contains(gui,"配置只读"),"readonly settings accepted");
    gui.navigate(0);
    state.faults[0].acknowledged=true;gui.update(state,now);check(contains(gui,"已确认 1"),"ack status missing");
    gui.navigate(0);gui.update(state,now+6s);check(contains(gui,"无效")&&!contains(gui,"72.5"),"stale display treated as valid");
    gui.step(50);gui.save_ppm(directory+"/invalid.ppm");
    // Pointer navigation exercises LVGL hit testing, not just direct page selection.
    gui.click(120,302);check(gui.page()==2,"navigation pointer input failed");
    gui.navigate(2);gui.update(state,now);gui.step(40);
    lv_mem_monitor_t before{},after{};lv_mem_monitor(&before);
    for(int i=0;i<500;++i){gui.navigate(i%5);gui.update(state,now);gui.step(40);}
    gui.navigate(2);gui.update(state,now);gui.step(40);
    lv_mem_monitor(&after);check(after.free_size+2048>=before.free_size,"page navigation leaked LVGL memory");
    std::cout<<"GUI pages, pointer input, history, settings, stale values and memory checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

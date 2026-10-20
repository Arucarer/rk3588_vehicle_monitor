#include "vehicle/communication.hpp"
#include "vehicle/diagnosis.hpp"
#include <cstdio>
#include <iostream>
#include <unistd.h>
using namespace vehicle;
using namespace std::chrono_literals;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
Frame request(uint8_t command,uint32_t sequence,const std::string& data={}){return {command,sequence,{data.begin(),data.end()}};}
int main(){try {
    auto t=Clock::time_point{}+10s;NodeManager nodes;
    check(nodes.receive(heartbeat(1,65535,0),t),"node1 heartbeat");
    check(nodes.receive(heartbeat(2,1,1),t),"node2 heartbeat");
    check(nodes.snapshot()[0].online&&nodes.snapshot()[1].online,"two nodes not online");
    check(!nodes.receive(heartbeat(1,65535,0),t+1s),"replay kept node alive");
    check(nodes.receive(heartbeat(1,0,0),t+1s),"16bit wrap rejected");
    auto malformed=heartbeat(2,2,0);malformed.length=7;check(!nodes.receive(malformed,t+1s),"bad DLC accepted");
    malformed=heartbeat(2,2,0);malformed.remote=true;check(!nodes.receive(malformed,t+1s),"RTR accepted");
    malformed.remote=false;malformed.extended=true;check(!nodes.receive(malformed,t+1s),"extended accepted");
    malformed.extended=false;malformed.error=true;check(!nodes.receive(malformed,t+1s),"error frame accepted");
    malformed=heartbeat(2,2,0);malformed.data[1]=1;check(!nodes.receive(malformed,t+1s),"ID mismatch");
    malformed=heartbeat(2,2,0);malformed.data[0]=2;check(!nodes.receive(malformed,t+1s),"bad version");
    malformed=heartbeat(2,2,0);malformed.data[7]=1;check(!nodes.receive(malformed,t+1s),"reserved bytes accepted");
    check(!nodes.receive(heartbeat(3,1,0),t+1s),"unknown node accepted");
    auto offline=nodes.expire(t+3s);check(offline.size()==1&&offline[0].id==2,"node isolation or timeout boundary");
    check(nodes.snapshot()[0].online&&!nodes.snapshot()[1].online,"other node affected");
    check(nodes.expire(t+3s).empty(),"repeated offline event");
    check(nodes.receive(heartbeat(2,0,0),t+3100ms),"node restart not accepted");
    nodes.invalidate();check(!nodes.snapshot()[0].online,"bus failure did not invalidate");

    Diagnosis live;
    live.add({"speed",80,5,false,false,Severity::warning,0ms,0ms,500ms});
    check(live.evaluate({{"speed",{60,true,t,"km/h","test",0}}},t).empty(),"initial threshold");
    live.configure({"speed",50,5,false,false,Severity::warning,0ms,0ms,500ms});
    check(live.evaluate({{"speed",{60,true,t+1ms,"km/h","test",0}}},t+1ms).size()==1,"runtime threshold not applied");
    live.configure({"speed",100,5,false,false,Severity::warning,0ms,0ms,500ms});
    auto restored=live.evaluate({{"speed",{60,true,t+2ms,"km/h","test",0}}},t+2ms);
    check(restored.size()==1&&restored[0].status.state==FaultState::recovered,"configuration erased active fault without recovery");
    Config initial;ParameterStore readonly(initial);
    CommandHandler locked(readonly,[]{return "simulation=1\nspeed=60\n";});
    check(locked.handle(request(0x01,1)).payload==std::vector<uint8_t>({1,0}),"heartbeat ACK layout");
    check(locked.handle(request(0xff,2)).payload[1]==1,"unknown command");
    check(locked.handle(request(0x20,3,"unexpected")).payload[1]==2,"payload validation");
    check(locked.handle(request(0x21,4,"overspeed_kmh=100")).payload[1]==3,"readonly gate");
    auto status=locked.handle(request(0x10,5));check(status.command==0x7e&&status.payload.size()>2,"state query");
    check(locked.handle(request(0x20,5)).payload[1]==5,"sequence conflict");

    std::string path="/tmp/vehicle-parameters-"+std::to_string(getpid());std::remove(path.c_str());
    ParameterStore writable(initial,path);CommandHandler handler(writable,[]{return "state";});
    auto change=request(0x21,10,"overspeed_kmh=100");auto ack=handler.handle(change);
    check(ack.command==0x7e&&ack.payload[1]==0,"SET failed");
    check(writable.snapshot().config.overspeed_kmh==100&&Config::load(path).overspeed_kmh==100,"not persisted before publishing");
    auto revision=writable.snapshot().revision;handler.handle(change);
    check(writable.snapshot().revision==revision,"duplicate SET reapplied");
    uint32_t sequence=11;
    for(const auto* invalid:{"overspeed_kmh=0","overspeed_kmh=301","overspeed_kmh=nan",
        "stale_ms=2","stale_ms=100.5","pulses_per_rev=2.5","circumference_m=20",
        "hysteresis_kmh=100","unknown=1","overspeed_kmh=80\n","overspeed_kmh=80x"})
        check(handler.handle(request(0x21,sequence++,invalid)).payload[1]==2,"invalid parameter accepted");
    ParameterStore broken(initial,"/nonexistent-vehicle-dir/config");check(broken.set("overspeed_kmh=100")==4,"write failure hidden");
    check(broken.snapshot().config.overspeed_kmh==80,"failed write published");
    for(uint32_t i=100;i<200;++i)handler.handle(request(0x01,i));
    check(handler.handle(request(0x20,10)).payload[1]==0,"cache bound eviction");
    std::remove(path.c_str());
    bool rejected=false;try{NetworkConfig invalid;invalid.address="hostname";invalid.validate();}catch(...){rejected=true;}
    check(rejected,"non-numeric endpoint accepted");
    {CanService unavailable("vm-no-can");std::this_thread::sleep_for(50ms);check(!unavailable.status().connected&&unavailable.status().errors>0,"missing CAN interface retry");}
    std::cout<<"CAN injection and command/parameter checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

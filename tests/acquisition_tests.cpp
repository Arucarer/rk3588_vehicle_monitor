#include "vehicle/acquisition.hpp"
#include "vehicle/diagnosis.hpp"
#include <iostream>
#include <stdexcept>
using namespace vehicle;
using namespace std::chrono_literals;
void check(bool b,const char* what){if(!b)throw std::runtime_error(what);}
void near(double a,double b,double tolerance,const char* what){check(std::abs(a-b)<=tolerance,what);}
int main(){try {
    auto t=Clock::time_point{}+10s;
    SpeedEstimator speed({2,1,100us,2000ms});
    check(!speed.sample(t).valid,"initial speed must be invalid");
    for(int i=1;i<=10;++i)check(speed.pulse(t+i*10ms),"pulse rejected");
    near(speed.sample(t+100ms).value,720,1e-6,"count speed");
    near(speed.sample(t+200ms).value,72,1e-6,"period speed decay");
    near(speed.sample(t+2200ms).value,0,0,"stationary timeout");
    check(!speed.sample(t+2300ms,false).valid,"failed counter masked");
    SpeedEstimator debounced({2,1,1000us,2000ms});
    debounced.sample(t);check(debounced.pulse(t+1ms),"first edge");
    check(!debounced.pulse(t+1500us),"bounce accepted");
    check(debounced.pulse(t+2ms),"boundary edge rejected");
    SpeedEstimator slow({2,1,0us,5000ms});slow.sample(t);slow.pulse(t+1s);slow.sample(t+1100ms);slow.pulse(t+3s);
    near(slow.sample(t+3100ms).value,3.6,1e-6,"low speed period");

    constexpr double pi=3.14159265358979323846;
    for(double angle:{-30.,-15.,0.,15.,30.}) {
        AttitudeEstimator attitude;
        auto a=angle*pi/180;
        auto result=attitude.update({{-std::sin(a),0,std::cos(a)},{0,0,0},t,true});
        near(result.pitch.value,angle,1e-6,"pitch fixture");
        near(result.vibration.value,0,1e-6,"static gravity vibration");
        AttitudeEstimator roll;
        near(roll.update({{0,std::sin(a),std::cos(a)},{0,0,0},t,true}).roll.value,angle,1e-6,"roll fixture");
    }
    AttitudeEstimator calibrated;
    std::vector<ImuSample> calibration(20,{{0,0,1},{1,2,0},t,true});
    calibrated.calibrate(calibration);
    calibrated.update({{0,0,1},{1,2,0},t,true});
    auto result=calibrated.update({{0,0,1},{1,2,0},t+20ms,true});
    near(result.pitch.value,0,1e-6,"gyro bias");
    check(!calibrated.update({{}, {},t+40ms,false}).pitch.valid,"IMU disconnect");
    near(calibrated.update({{0,0,2},{0,0,0},t+60ms,true}).vibration.value,1,1e-6,"impact g");

    AnalogChannel adc({0,100,0.24,1,1,"V","bus"});
    for(double raw:{0.,25.,50.,75.,100.})near(adc.update(raw,true,t).value,raw*0.24+1,1e-9,"ADC five points");
    check(!adc.update(101,true,t).valid,"out of range ADC");
    AnalogChannel filter({0,100,1,0,0.5,"A","motor"});
    filter.update(0,true,t);near(filter.update(10,true,t).value,5,1e-9,"EMA");
    filter.update(0,false,t);near(filter.update(20,true,t).value,20,1e-9,"invalid resets filter");
    DigitalDebouncer digital(false,20ms);
    check(!digital.update(true,true,t).valid,"DI startup settling");
    near(digital.update(true,true,t+20ms).value,0,0,"active low normal");
    digital.update(false,true,t+21ms);digital.update(true,true,t+25ms);
    near(digital.update(false,true,t+30ms).value,0,0,"bounce changed stable state");
    near(digital.update(false,true,t+50ms).value,1,0,"DI active");
    check(!digital.update(false,false,t+51ms).valid,"DI failure");

    EnvironmentProcessor env;
    check(env.due(t),"DHT initial due");env.attempted(t);
    check(!env.due(t+1999ms)&&env.due(t+2s),"DHT retry rate");
    auto climate=env.decode({55,0,25,0,80},true,t);
    check(climate.first.valid&&climate.second.valid,"DHT checksum valid");
    near(climate.first.value,25,0,"DHT temperature");
    check(!env.decode({55,0,25,0,81},true,t).first.valid,"bad DHT checksum");
    check(!env.decode({55,0,25,0,80},false,t).first.valid,"DHT IO failure");

    Diagnosis diagnosis;
    diagnosis.add({"speed",80,5,false,false,Severity::warning,100ms,200ms,500ms});
    diagnosis.add({"tilt",15,2,true,false,Severity::fault,100ms,200ms,500ms});
    std::map<std::string,Measurement> inputs;
    auto set=[&](double v,double tilt,Clock::time_point now){inputs["speed"]={v,true,now,"km/h","test",0};inputs["tilt"]={tilt,true,now,"deg","test",0};};
    set(90,-20,t);check(diagnosis.evaluate(inputs,t).empty(),"trigger duration ignored");
    set(90,-20,t+100ms);auto events=diagnosis.evaluate(inputs,t+100ms);
    check(events.size()==2,"concurrent faults lost");
    check(events[0].sequence+1==events[1].sequence,"event sequence");
    auto output=alarm_output(diagnosis.statuses(),t+100ms);
    check(output.severity==Severity::fault&&output.buzzer,"fault priority");
    diagnosis.acknowledge("tilt");diagnosis.acknowledge("speed");
    check(!alarm_output(diagnosis.statuses(),t+100ms).buzzer,"ack did not silence");
    set(78,-14,t+150ms);check(diagnosis.evaluate(inputs,t+150ms).empty(),"hysteresis chatter");
    check(diagnosis.statuses()[1].state==FaultState::fault,"ack cleared fault");
    set(75,0,t+200ms);check(diagnosis.evaluate(inputs,t+200ms).empty(),"recovery duration ignored");
    set(75,0,t+400ms);check(diagnosis.evaluate(inputs,t+400ms).size()==2,"recovery events missing");
    set(75,0,t+450ms);diagnosis.evaluate(inputs,t+450ms);
    check(diagnosis.evaluate(inputs,t+1s).size()==2,"stale sensor faults missing");
    check(diagnosis.evaluate(inputs,t+1100ms).empty(),"duplicate fault events");
    set(75,0,t+1200ms);diagnosis.evaluate(inputs,t+1200ms);
    set(75,0,t+1400ms);check(diagnosis.evaluate(inputs,t+1400ms).size()==2,"sensor recovery");
    Diagnosis low;low.add({"supply",10,1,false,true,Severity::fault,0ms,0ms,500ms});
    check(low.evaluate({{"supply",{9,true,t,"V","test",0}}},t).size()==1,"low limit");
    std::cout<<"acquisition and diagnosis checks passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

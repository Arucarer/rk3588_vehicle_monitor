#include "vehicle/core.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
namespace { volatile std::sig_atomic_t stopping=0; void stop(int){stopping=1;} }
int main(int argc,char** argv){try{bool simulation=false;double seconds=5;vehicle::Config config;
 for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--simulate")simulation=true;else if(arg=="--duration"&&i+1<argc)seconds=std::stod(argv[++i]);else if(arg=="--config"&&i+1<argc)config=vehicle::Config::load(argv[++i]);else if(arg=="--version"){std::cout<<"vehicle-monitor 0.1.0\n";return 0;}else throw std::invalid_argument("usage: vehicle-monitor --simulate [--duration seconds] [--config path]");}
 if(!simulation)throw std::runtime_error("hardware backend not commissioned; use --simulate explicitly");
 if(!std::isfinite(seconds)||seconds<=0||seconds>86400)throw std::invalid_argument("duration must be 0..86400 seconds");
 config.validate();
 std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
 vehicle::StateStore store;vehicle::BoundedQueue<std::string> logs(128);std::atomic<bool> running{true};
 std::thread logger([&]{while(auto line=logs.pop())std::cout<<*line<<std::endl;});
 std::thread acquisition([&]{auto next=vehicle::Clock::now();unsigned tick=0;while(running){vehicle::Snapshot s;auto now=vehicle::Clock::now();s.speed={tick++%30<15?60.0:90.0,true,now,"km/h","simulation",0};store.publish(s);next+=std::chrono::milliseconds(100);std::this_thread::sleep_until(next);}});
 vehicle::ThresholdFault overspeed(config.overspeed_kmh,config.hysteresis_kmh);auto end=vehicle::Clock::now()+std::chrono::duration<double>(seconds);
 while(!stopping&&vehicle::Clock::now()<end){auto s=store.snapshot();if(s.sequence&&overspeed.update(s.speed.value,s.speed.fresh(vehicle::Clock::now(),std::chrono::milliseconds(config.stale_ms))))logs.push("SIMULATION overspeed state="+std::to_string(static_cast<int>(overspeed.state()))+" sequence="+std::to_string(s.sequence));std::this_thread::sleep_for(std::chrono::milliseconds(50));}
 running=false;acquisition.join();logs.close();logger.join();std::cout<<"SIMULATION stopped; log_dropped="<<logs.dropped()<<"\n";return logs.dropped()?2:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}

#include "vehicle/core.hpp"
#include <iostream>
#include <stdexcept>
using namespace vehicle;
void check(bool b){if(!b)throw std::runtime_error("check failed");}
int main(){try{
 Config c;check(std::abs(speed_kmh(10,1,c)-72)<1e-9);bool rejected=false;try{speed_kmh(1,0,c);}catch(...){rejected=true;}check(rejected);
 ThresholdFault f(80,5);check(f.update(90,true));f.acknowledge();check(f.state()==FaultState::warning);check(!f.update(78,true));check(f.state()==FaultState::warning);check(f.update(75,true));check(f.state()==FaultState::recovered);check(f.update(0,false));check(f.state()==FaultState::fault);
 const std::string vector="123456789";check(crc32(reinterpret_cast<const uint8_t*>(vector.data()),vector.size())==0xcbf43926);
 auto bytes=encode({0x10,123,{1,2,3}});StreamDecoder d;std::vector<Frame> frames;
 for(auto b:bytes){auto part=d.feed(&b,1);frames.insert(frames.end(),part.begin(),part.end());}check(frames.size()==1&&frames[0].sequence==123&&frames[0].payload.size()==3);
 auto corrupt=bytes;corrupt.back()^=1;corrupt.insert(corrupt.end(),bytes.begin(),bytes.end());corrupt.insert(corrupt.end(),bytes.begin(),bytes.end());check(d.feed(corrupt.data(),corrupt.size()).size()==2);
 auto invalid=bytes;invalid[8]=0xff;invalid.insert(invalid.end(),bytes.begin(),bytes.end());check(d.feed(invalid.data(),invalid.size()).size()==1);
 std::vector<uint8_t> noise(100000,0xff);d.feed(noise.data(),noise.size());check(d.buffered()<12);
 BoundedQueue<int> q(1);check(q.push(3));check(!q.push(4));check(q.dropped()==1);q.close();check(q.pop().value()==3);check(!q.pop());check(!q.push(5));
 StateStore store;Snapshot s;store.publish(s);check(store.snapshot().sequence==1);auto now=Clock::now();Measurement m{2,true,now,"V","test",0};check(m.fresh(now,std::chrono::milliseconds(10)));check(!m.fresh(now+std::chrono::milliseconds(11),std::chrono::milliseconds(10)));
 std::cout<<"core checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

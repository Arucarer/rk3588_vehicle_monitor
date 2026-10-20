#include "vehicle/core.hpp"
#include <fstream>
#include <map>
#include <stdexcept>
namespace vehicle {
void StateStore::publish(Snapshot s){std::lock_guard<std::mutex> l(mutex_);s.sequence=state_.sequence+1;state_=std::move(s);}
Snapshot StateStore::snapshot()const{std::lock_guard<std::mutex> l(mutex_);return state_;}
void Config::validate()const { if(!std::isfinite(circumference_m)||circumference_m<=0||!std::isfinite(pulses_per_rev)||pulses_per_rev<=0||!std::isfinite(overspeed_kmh)||overspeed_kmh<=0||!std::isfinite(hysteresis_kmh)||hysteresis_kmh<0||hysteresis_kmh>=overspeed_kmh||stale_ms<100||stale_ms>60000) throw std::invalid_argument("invalid configuration range"); }
Config Config::load(const std::string& path){Config c;std::ifstream f(path);if(!f)throw std::runtime_error("cannot open configuration: "+path);std::string line;std::map<std::string,bool> seen;
 while(std::getline(f,line)){if(line.empty()||line[0]=='#')continue;auto pos=line.find('=');if(pos==std::string::npos)throw std::invalid_argument("expected key=value");auto k=line.substr(0,pos),v=line.substr(pos+1);if(seen[k])throw std::invalid_argument("duplicate key: "+k);seen[k]=true;size_t n=0;double d=std::stod(v,&n);if(n!=v.size()||!std::isfinite(d))throw std::invalid_argument("invalid number");
 if(k=="circumference_m")c.circumference_m=d;else if(k=="pulses_per_rev")c.pulses_per_rev=d;else if(k=="overspeed_kmh")c.overspeed_kmh=d;else if(k=="hysteresis_kmh")c.hysteresis_kmh=d;else if(k=="stale_ms"){if(d<100||d>60000||std::floor(d)!=d)throw std::invalid_argument("invalid stale_ms");c.stale_ms=static_cast<unsigned>(d);}else throw std::invalid_argument("unknown key: "+k);
 }c.validate();return c;}
double speed_kmh(uint64_t pulses,double elapsed_s,const Config& c){c.validate();if(!std::isfinite(elapsed_s)||elapsed_s<=0)throw std::invalid_argument("invalid sampling interval");return pulses*c.circumference_m*3.6/c.pulses_per_rev/elapsed_s;}
bool ThresholdFault::update(double value,bool valid){auto old=state_;if(!valid||!std::isfinite(value))state_=FaultState::fault;else if(value>limit_)state_=FaultState::warning;else if(value<=limit_-hysteresis_)state_=(old==FaultState::warning||old==FaultState::fault)?FaultState::recovered:FaultState::normal; if(state_!=old)acknowledged_=false;return old!=state_;}
uint32_t crc32(const uint8_t* p,size_t n){uint32_t c=0xffffffff;for(size_t i=0;i<n;++i){c^=p[i];for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}return c^0xffffffff;}
static void put32(std::vector<uint8_t>& b,uint32_t n){for(int i=3;i>=0;--i)b.push_back(static_cast<uint8_t>(n>>(8*i)));}
static uint32_t get32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
std::vector<uint8_t> encode(const Frame& f){if(f.payload.size()>4096)throw std::invalid_argument("payload too large");std::vector<uint8_t>b{0x56,0x4d,1,f.command};put32(b,f.sequence);put32(b,static_cast<uint32_t>(f.payload.size()));b.insert(b.end(),f.payload.begin(),f.payload.end());put32(b,crc32(b.data(),b.size()));return b;}
std::vector<Frame> StreamDecoder::feed(const uint8_t* p,size_t n){std::vector<Frame> out;for(size_t i=0;i<n;++i){pending_.push_back(p[i]);while(pending_.size()>=12){if(pending_[0]!=0x56||pending_[1]!=0x4d||pending_[2]!=1){pending_.erase(pending_.begin());continue;}auto len=get32(pending_.data()+8);if(len>4096){pending_.erase(pending_.begin());continue;}size_t total=16+len;if(pending_.size()<total)break;if(crc32(pending_.data(),total-4)!=get32(pending_.data()+total-4)){pending_.erase(pending_.begin());continue;}out.push_back({pending_[3],get32(pending_.data()+4),{pending_.begin()+12,pending_.begin()+12+len}});pending_.erase(pending_.begin(),pending_.begin()+total);}}return out;}
}

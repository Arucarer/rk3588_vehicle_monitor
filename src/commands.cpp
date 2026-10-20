#include "vehicle/communication.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sstream>
#include <iomanip>
#include <cerrno>

namespace vehicle {
namespace {
void persist(const std::string& path,const std::string& text) {
    auto separator=path.find_last_of('/');
    std::string directory=separator==std::string::npos?".":path.substr(0,separator);
    if(directory.empty())directory="/";
    std::string name=separator==std::string::npos?path:path.substr(separator+1);
    if(name.empty()||name=="."||name=="..")throw std::invalid_argument("invalid parameter path");
    int dir=open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir<0)throw std::runtime_error("parameter directory unavailable");
    std::string temp=name+".tmp-"+std::to_string(getpid());
    int fd=openat(dir,temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0){close(dir);throw std::runtime_error("parameter temporary file unavailable");}
    try {
        size_t done=0;
        while(done<text.size()) {
            auto n=write(fd,text.data()+done,text.size()-done);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw std::runtime_error("parameter write failed");
            done+=size_t(n);
        }
        if(fsync(fd)<0)throw std::runtime_error("parameter sync failed");
        if(renameat(dir,temp.c_str(),dir,name.c_str())<0)throw std::runtime_error("parameter rename failed");
        if(fsync(dir)<0)throw std::runtime_error("parameter directory sync failed");
        close(fd);close(dir);
    }catch(...){close(fd);unlinkat(dir,temp.c_str(),0);close(dir);throw;}
}
Frame reply(const Frame& request,uint8_t error,const std::string& body={}) {
    Frame response{uint8_t(error?0x7f:0x7e),request.sequence,{request.command,error}};
    response.payload.insert(response.payload.end(),body.begin(),body.end());return response;
}
}
std::string config_text(const Config& c) {
    std::ostringstream s;s<<std::setprecision(17)
        <<"circumference_m="<<c.circumference_m<<'\n'<<"pulses_per_rev="<<c.pulses_per_rev<<'\n'
        <<"overspeed_kmh="<<c.overspeed_kmh<<'\n'<<"hysteresis_kmh="<<c.hysteresis_kmh<<'\n'
        <<"stale_ms="<<c.stale_ms<<'\n';return s.str();
}
ParameterStore::ParameterStore(Config initial,std::string path):state_{initial,0},writable_path_(std::move(path)){initial.validate();}
ParameterSnapshot ParameterStore::snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return state_;}
uint8_t ParameterStore::set(const std::string& text) {
    if(writable_path_.empty())return 3;
    std::lock_guard<std::mutex> update(update_mutex_);
    if(text.empty()||text.size()>128||text.find_first_of("\n\r\t ")!=std::string::npos)return 2;
    auto equal=text.find('=');if(equal==std::string::npos)return 2;
    auto key=text.substr(0,equal),raw=text.substr(equal+1);
    auto candidate=snapshot().config;size_t used=0;double value;
    try {
        value=std::stod(raw,&used);if(used!=raw.size()||!std::isfinite(value))return 2;
        if(key=="overspeed_kmh") {if(value<1||value>300)return 2;candidate.overspeed_kmh=value;}
        else if(key=="hysteresis_kmh"){if(value<0||value>50)return 2;candidate.hysteresis_kmh=value;}
        else if(key=="circumference_m"){if(value<0.1||value>10)return 2;candidate.circumference_m=value;}
        else if(key=="pulses_per_rev"){if(value<1||value>10000||std::floor(value)!=value)return 2;candidate.pulses_per_rev=value;}
        else if(key=="stale_ms"){if(value<100||value>60000||std::floor(value)!=value)return 2;candidate.stale_ms=static_cast<unsigned>(value);}
        else return 2;
        candidate.validate();
    }catch(...){return 2;}
    try{persist(writable_path_,config_text(candidate));}catch(...){return 4;}
    {std::lock_guard<std::mutex> lock(mutex_);state_.config=candidate;++state_.revision;}
    return 0;
}
CommandHandler::CommandHandler(ParameterStore& parameters,std::function<std::string()> state):parameters_(parameters),state_(std::move(state)){}
Frame CommandHandler::handle(const Frame& r) {
    for(const auto& item:cache_)if(item.first.sequence==r.sequence) {
        if(item.first.command==r.command&&item.first.payload==r.payload)return item.second;
        return reply(r,5);
    }
    Frame response;
    if(r.payload.size()>4096)response=reply(r,2);
    else if(r.command==0x01)response=reply(r,r.payload.empty()?0:2);
    else if(r.command==0x10) {
        if(!r.payload.empty())response=reply(r,2);
        else {auto state=state_();response=state.size()<=4094?reply(r,0,state):reply(r,6);}
    }else if(r.command==0x20)response=r.payload.empty()?reply(r,0,config_text(parameters_.snapshot().config)):reply(r,2);
    else if(r.command==0x21) {
        auto error=parameters_.set(std::string(r.payload.begin(),r.payload.end()));
        response=reply(r,error,error?"":config_text(parameters_.snapshot().config));
    }else response=reply(r,1);
    cache_.push_back({r,response});if(cache_.size()>64)cache_.pop_front();return response;
}
std::string snapshot_text(const Snapshot& s,Clock::time_point now,std::chrono::milliseconds ttl) {
    std::ostringstream text;text<<std::setprecision(10)<<"format=1\nsnapshot="<<s.sequence<<'\n';
    auto add=[&](const std::string& name,const Measurement& m) {
        bool valid=m.fresh(now,ttl);text<<name<<"=";
        if(valid)text<<m.value;else text<<"invalid";
        text<<",valid="<<valid<<",unit="<<m.unit<<",source="<<m.source<<",error="<<m.error
            <<",sample_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(m.sampled.time_since_epoch()).count()<<'\n';
    };
    add("speed",s.speed);add("pitch",s.pitch);add("roll",s.roll);add("vibration",s.vibration);
    add("temperature",s.temperature);add("humidity",s.humidity);
    for(const auto& p:s.analog)add("analog."+p.first,p.second);
    for(const auto& p:s.digital)add("digital."+p.first,p.second);
    return text.str();
}
}

#include "vehicle/runtime.hpp"
#include "vehicle/version.hpp"
#include <cerrno>
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vehicle {
std::shared_ptr<FaultInjection> FaultInjection::parse(const std::string& text) {
    auto colon=text.find(':');if(colon==std::string::npos)throw std::invalid_argument("inject format: NAME:AFTER_MS");
    auto fault=std::make_shared<FaultInjection>();fault->name=text.substr(0,colon);
    const std::vector<std::string> allowed={"acquisition-stall","diagnosis-stall","sensor-dropout","network-stall","storage-stall","storage-fail","gui-stall"};
    if(std::find(allowed.begin(),allowed.end(),fault->name)==allowed.end())throw std::invalid_argument("unknown fault injection");
    auto value=text.substr(colon+1);size_t used=0;auto ms=std::stoul(value,&used);
    if(used!=value.size()||ms<100||ms>60000)throw std::invalid_argument("invalid fault time");
    fault->after=std::chrono::milliseconds(ms);return fault;
}
namespace {
void output(int fd,const std::string& text) {
    size_t done=0;
    while(done<text.size()) {
        auto n=write(fd,text.data()+done,text.size()-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)throw std::runtime_error("metrics write failed");
        done+=size_t(n);
    }
    if(fdatasync(fd)<0)throw std::runtime_error("metrics sync failed");
}
long rss_kb() {
    std::ifstream f("/proc/self/status");std::string line;
    while(std::getline(f,line))if(line.find("VmRSS:")==0){std::istringstream s(line.substr(6));long n=0;s>>n;return n;}
    return -1;
}
std::string temperature() {
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");long value;
    if(f>>value)return std::to_string(value/1000.0);
    return "null";
}
}
RuntimeSupervisor::RuntimeSupervisor(std::function<RuntimeMetrics()> inspect,std::function<void()> trip,std::string path)
    :inspect_(std::move(inspect)),trip_(std::move(trip)),metrics_path_(std::move(path)) {
    worker_=std::thread(&RuntimeSupervisor::run,this);
}
RuntimeSupervisor::~RuntimeSupervisor(){stop_=true;if(worker_.joinable())worker_.join();}
void RuntimeSupervisor::run()noexcept {
    int fd=-1;
    try {
        if(!metrics_path_.empty()) {
            fd=open(metrics_path_.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(fd<0)throw std::runtime_error("metrics output must be a new file");
        }
        auto start=Clock::now(),next=start;uint64_t eligible=0;
        while(!stop_) {
            auto metrics=inspect_();auto now=Clock::now();
            bool healthy=metrics.unhealthy.empty();if(healthy&&!tripped_)++eligible;
            if(!healthy&&!tripped_.exchange(true)) {
                for(const auto& name:metrics.unhealthy)std::cerr<<"HEALTH stalled="<<name<<'\n';
                trip_();
            }
            if(fd>=0&&(now>=next||!healthy)) {
                rusage usage{};getrusage(RUSAGE_SELF,&usage);
                double cpu=usage.ru_utime.tv_sec+usage.ru_utime.tv_usec/1e6+usage.ru_stime.tv_sec+usage.ru_stime.tv_usec/1e6;
                std::ostringstream s;
                s<<"{\"simulation\":true,\"software_version\":\""<<software_version<<"\",\"elapsed_ms\":"
                 <<std::chrono::duration_cast<std::chrono::milliseconds>(now-start).count()
                 <<",\"cpu_seconds\":"<<cpu<<",\"rss_kb\":"<<rss_kb()<<",\"temperature_c\":"<<temperature()
                 <<",\"temperature_source\":\"thermal_zone0\",\"snapshots\":"<<metrics.snapshots
                 <<",\"active_faults\":"<<metrics.faults<<",\"log_pending\":"<<metrics.log_pending
                 <<",\"critical_dropped\":"<<metrics.log_critical_dropped<<",\"samples_dropped\":"<<metrics.log_sample_dropped
                 <<",\"network_connected\":"<<(metrics.network_connected?"true":"false")
                 <<",\"healthy\":"<<(healthy?"true":"false")<<",\"watchdog_eligible_ticks\":"<<eligible
                 <<",\"hardware_watchdog_enabled\":false,\"unhealthy\":[";
                for(size_t i=0;i<metrics.unhealthy.size();++i){if(i)s<<',';s<<'"'<<metrics.unhealthy[i]<<'"';}
                s<<"]}\n";output(fd,s.str());next=now+std::chrono::seconds(1);
            }
            if(tripped_)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }catch(const std::exception& e){std::cerr<<"HEALTH supervisor failed: "<<e.what()<<'\n';tripped_=true;trip_();}
    catch(...){tripped_=true;trip_();}
    if(fd>=0)close(fd);
}
}

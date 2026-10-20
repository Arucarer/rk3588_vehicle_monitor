#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct Device {
    int fd=-1;
    std::string path,name;
    std::array<int,3> axes{};
    unsigned known=0;
    bool dropped=false;
    uint64_t events=0,reports=0,lost=0;
};
int main(int argc,char** argv) {
    std::vector<Device> devices;
    auto close_all=[&]{for(auto& d:devices)if(d.fd>=0)close(d.fd);};
    try {
        if(argc<3)throw std::invalid_argument("usage: vehicle-input-probe SECONDS(1..60) /dev/input/eventN [...]\nRead-only raw events; never enables a sensor or claims calibrated units.");
        char* end=nullptr;long seconds=std::strtol(argv[1],&end,10);
        if(!end||*end||seconds<1||seconds>60)throw std::invalid_argument("seconds must be 1..60");
        for(int i=2;i<argc;++i) {
            if(devices.size()>=8)throw std::invalid_argument("maximum 8 devices");
            Device d;d.path=argv[i];d.fd=open(argv[i],O_RDONLY|O_NONBLOCK|O_CLOEXEC);
            if(d.fd<0)throw std::runtime_error("cannot open "+d.path);
            devices.push_back(d);auto& stored=devices.back();
            char name[256]{};
            if(ioctl(stored.fd,EVIOCGNAME(sizeof(name)),name)<0)throw std::runtime_error("not a readable evdev device: "+stored.path);
            stored.name=name;
            std::cout<<"device="<<stored.path<<" name="<<stored.name<<" units=RAW_UNCALIBRATED\n";
        }
        std::vector<pollfd> pollers;for(const auto& d:devices)pollers.push_back({d.fd,POLLIN,0});
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        while(std::chrono::steady_clock::now()<deadline) {
            int ready=poll(pollers.data(),pollers.size(),100);
            if(ready<0&&errno==EINTR)continue;
            if(ready<0)throw std::runtime_error("poll failed");
            for(size_t i=0;i<devices.size();++i) {
                auto& d=devices[i];
                if(pollers[i].revents&(POLLERR|POLLHUP|POLLNVAL))throw std::runtime_error("input disconnected: "+d.path);
                if(!(pollers[i].revents&POLLIN))continue;
                input_event batch[64];auto n=read(d.fd,batch,sizeof(batch));
                if(n<0&&(errno==EAGAIN||errno==EINTR))continue;
                if(n<=0||size_t(n)%sizeof(input_event))throw std::runtime_error("invalid input read: "+d.path);
                for(size_t j=0;j<size_t(n)/sizeof(input_event);++j) {
                    const auto& e=batch[j];++d.events;
                    if(e.type==EV_SYN&&e.code==SYN_DROPPED){++d.lost;d.dropped=true;d.known=0;continue;}
                    if(d.dropped){if(e.type==EV_SYN&&e.code==SYN_REPORT)d.dropped=false;continue;}
                    if(e.type==EV_ABS) {
                        int axis=-1;
                        if(d.name=="gsensor"&&e.code<=ABS_Z)axis=e.code-ABS_X;
                        if(d.name=="gyro"&&e.code>=ABS_RX&&e.code<=ABS_RZ)axis=e.code-ABS_RX;
                        if(axis>=0&&axis<3){d.axes[axis]=e.value;d.known|=1u<<axis;}
                    }
                    if(e.type==EV_SYN&&e.code==SYN_REPORT) {
                        ++d.reports;
                        if(d.reports<=10) {
                            std::cout<<d.name<<" report="<<d.reports<<" raw=[";
                            for(unsigned axis=0;axis<3;++axis){if(axis)std::cout<<',';if(d.known&(1u<<axis))std::cout<<d.axes[axis];else std::cout<<'?';}
                            std::cout<<"] known_mask="<<d.known<<'\n';
                        }
                    }
                }
            }
        }
        for(const auto& d:devices)std::cout<<"SUMMARY name="<<d.name<<" events="<<d.events<<" reports="<<d.reports<<" syn_dropped="<<d.lost<<" live_data="<<(d.reports>0&&d.known!=0?"yes":"NO")<<'\n';
        std::cout<<"No events may mean the vendor sensor is not enabled; no enable/ioctl-write was attempted.\n";
        close_all();return 0;
    }catch(const std::exception& e){close_all();std::cerr<<e.what()<<'\n';return 1;}
}

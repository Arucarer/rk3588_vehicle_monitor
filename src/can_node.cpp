#include "vehicle/communication.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vehicle {
CanMessage heartbeat(uint8_t node,uint16_t sequence,uint8_t state) {
    if(node<1||node>127||state>3)throw std::invalid_argument("invalid CAN heartbeat");
    CanMessage f;f.id=0x700+node;f.data={1,node,uint8_t(sequence>>8),uint8_t(sequence),state,0,0,0};return f;
}
NodeManager::NodeManager(std::vector<uint8_t> nodes,std::chrono::milliseconds timeout):timeout_(timeout) {
    if(nodes.empty()||timeout.count()<=0)throw std::invalid_argument("invalid CAN nodes/timeout");
    for(auto id:nodes) {
        if(id<1||id>=127||nodes_.count(id))throw std::invalid_argument("invalid or duplicate downstream node");
        NodeStatus status;status.id=id;nodes_.emplace(id,status);
    }
}
bool NodeManager::receive(const CanMessage& f,Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto invalid=[&]{++rejected_;return false;};
    if(f.extended||f.remote||f.error||f.length!=8||f.id<0x701||f.id>0x77e||f.data[0]!=1||
       f.id!=uint32_t(0x700+f.data[1])||f.data[4]>3||f.data[5]||f.data[6]||f.data[7])return invalid();
    auto found=nodes_.find(f.data[1]);if(found==nodes_.end())return invalid();
    auto& node=found->second;uint16_t seq=uint16_t(f.data[2])<<8|f.data[3];
    if(node.seen && now<node.last_seen)return invalid();
    // Repeated/backwards frames must not keep an online node alive; allow restart after timeout.
    if(node.seen && now-node.last_seen<timeout_) {
        uint16_t delta=static_cast<uint16_t>(seq-node.sequence);
        if(delta==0||delta>=0x8000)return invalid();
    }
    node.sequence=seq;node.state=f.data[4];node.last_seen=now;node.seen=true;node.online=true;return true;
}
std::vector<NodeEvent> NodeManager::expire(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);std::vector<NodeEvent> events;
    for(auto& p:nodes_)if(p.second.online && now>=p.second.last_seen && now-p.second.last_seen>=timeout_) {
        p.second.online=false;events.push_back({p.first,false});
    }
    return events;
}
std::vector<NodeStatus> NodeManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);std::vector<NodeStatus> out;for(const auto& p:nodes_)out.push_back(p.second);return out;
}
uint64_t NodeManager::rejected()const{std::lock_guard<std::mutex> lock(mutex_);return rejected_;}
void NodeManager::invalidate(){std::lock_guard<std::mutex> lock(mutex_);for(auto& p:nodes_)p.second.online=false;}
SocketCan::SocketCan(const std::string& interface) {
    if(interface.empty()||interface.size()>=IFNAMSIZ)throw std::invalid_argument("invalid CAN interface name");
    fd_=socket(PF_CAN,SOCK_RAW|SOCK_NONBLOCK|SOCK_CLOEXEC,CAN_RAW);
    if(fd_<0)throw std::runtime_error("SocketCAN socket unavailable");
    try {
        struct ifreq request{};std::memcpy(request.ifr_name,interface.c_str(),interface.size()+1);
        if(ioctl(fd_,SIOCGIFINDEX,&request)<0)throw std::runtime_error("CAN interface missing");
        can_err_mask_t mask=CAN_ERR_MASK;
        if(setsockopt(fd_,SOL_CAN_RAW,CAN_RAW_ERR_FILTER,&mask,sizeof(mask))<0)throw std::runtime_error("CAN error filter failed");
        sockaddr_can address{};address.can_family=AF_CAN;address.can_ifindex=request.ifr_ifindex;
        if(bind(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0)throw std::runtime_error("CAN bind failed");
    }catch(...){close(fd_);fd_=-1;throw;}
}
SocketCan::~SocketCan(){if(fd_>=0)close(fd_);}
std::optional<CanMessage> SocketCan::receive() {
    can_frame f{};ssize_t n=read(fd_,&f,sizeof(f));
    if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return {};
    if(n!=sizeof(f))throw std::runtime_error("CAN read failed");
    CanMessage out;out.extended=f.can_id&CAN_EFF_FLAG;out.remote=f.can_id&CAN_RTR_FLAG;out.error=f.can_id&CAN_ERR_FLAG;
    out.id=f.can_id&CAN_EFF_MASK;out.length=f.can_dlc;std::copy_n(f.data,8,out.data.begin());return out;
}
bool SocketCan::send(const CanMessage& message) {
    if(message.id>CAN_SFF_MASK||message.length>8||message.extended||message.remote||message.error)
        throw std::invalid_argument("only classic standard data CAN frames supported");
    can_frame f{};f.can_id=message.id;f.can_dlc=message.length;std::copy(message.data.begin(),message.data.end(),f.data);
    auto n=write(fd_,&f,sizeof(f));
    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==ENOBUFS||errno==EINTR))return false;
    if(n!=sizeof(f))throw std::runtime_error("CAN write failed");
    return true;
}
CanService::CanService(std::string interface):interface_(std::move(interface)) {
    if(interface_.empty()||interface_.size()>=IFNAMSIZ)throw std::invalid_argument("invalid CAN interface");
    worker_=std::thread(&CanService::run,this);
}
CanService::~CanService(){stop_=true;if(worker_.joinable())worker_.join();}
CanServiceStatus CanService::status()const{std::lock_guard<std::mutex> lock(mutex_);return status_;}
void CanService::run()noexcept {
    std::unique_ptr<SocketCan> socket;
    auto retry=Clock::now(),next_heartbeat=retry;uint16_t sequence=0;
    while(!stop_) {
        auto now=Clock::now();
        try {
            if(!socket && now>=retry) {
                socket=std::make_unique<SocketCan>(interface_);
                std::lock_guard<std::mutex> lock(mutex_);status_.connected=true;++status_.reconnects;
                next_heartbeat=now;
            }
            if(socket) {
                for(int i=0;i<64;++i) {
                    auto f=socket->receive();if(!f)break;
                    if(f->error)throw std::runtime_error("CAN controller error frame");
                    nodes_.receive(*f,now);
                    std::lock_guard<std::mutex> lock(mutex_);++status_.received;
                }
                if(now>=next_heartbeat) {
                    if(!socket->send(heartbeat(127,sequence++,0)))throw std::runtime_error("CAN transmit congested");
                    next_heartbeat=now+std::chrono::seconds(1);
                }
            }
            nodes_.expire(now);
        }catch(...) {
            socket.reset();nodes_.invalidate();retry=now+std::chrono::seconds(1);
            std::lock_guard<std::mutex> lock(mutex_);status_.connected=false;++status_.errors;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
}

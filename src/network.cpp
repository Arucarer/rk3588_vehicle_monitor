#include "vehicle/communication.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vehicle {
namespace {
class Descriptor {
public:
    int fd=-1;
    ~Descriptor(){if(fd>=0)close(fd);}
};
struct Outgoing {std::vector<uint8_t> bytes;size_t offset=0;};
struct PendingAck {uint8_t command;Clock::time_point deadline;};
}
void NetworkConfig::validate()const {
    in_addr addr{};
    if(inet_pton(AF_INET,address.c_str(),&addr)!=1||port==0||
       report.count()<50||heartbeat.count()<50||ack_timeout.count()<100||reconnect.count()<50||
       report.count()>60000||heartbeat.count()>60000||ack_timeout.count()>60000||reconnect.count()>5000)
        throw std::invalid_argument("invalid TCP configuration (numeric IPv4 required)");
}
TcpClient::TcpClient(NetworkConfig config,ParameterStore& p,std::function<std::string()> state):config_(std::move(config)),parameters_(p),state_(std::move(state)) {
    config_.validate();worker_=std::thread(&TcpClient::run,this);
}
TcpClient::~TcpClient(){stop_=true;if(worker_.joinable())worker_.join();}
NetworkStatus TcpClient::status()const{std::lock_guard<std::mutex> lock(mutex_);return status_;}
void TcpClient::run()noexcept {
    uint32_t sequence=1;
    auto retry=Clock::now();
    while(!stop_) {
        if(Clock::now()<retry){std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}
        try {
            Descriptor socket;
            socket.fd=::socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
            if(socket.fd<0)throw std::runtime_error("TCP socket failed");
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(config_.port);
            inet_pton(AF_INET,config_.address.c_str(),&address.sin_addr);
            int result=connect(socket.fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
            if(result<0&&errno!=EINPROGRESS)throw std::runtime_error("TCP connect failed");
            if(result<0) {
                auto deadline=Clock::now()+std::chrono::seconds(3);bool connected=false;
                while(!stop_ && Clock::now()<deadline) {
                    pollfd event{socket.fd,POLLOUT,0};int ready=poll(&event,1,50);
                    if(ready<0&&errno==EINTR)continue;
                    if(ready<0)throw std::runtime_error("TCP connect poll failed");
                    if(ready>0) {
                        int error=0;socklen_t length=sizeof(error);
                        if(getsockopt(socket.fd,SOL_SOCKET,SO_ERROR,&error,&length)<0||error)throw std::runtime_error("TCP connection rejected");
                        connected=true;break;
                    }
                }
                if(stop_)break;
                if(!connected)throw std::runtime_error("TCP connect timeout");
            }
            {std::lock_guard<std::mutex> lock(mutex_);status_.connected=true;++status_.connections;}
            CommandHandler commands(parameters_,state_);
            StreamDecoder decoder;
            std::deque<Outgoing> outgoing;
            std::map<uint32_t,PendingAck> pending;
            auto next_report=Clock::now(),next_heartbeat=next_report;
            auto enqueue=[&](const Frame& frame,bool track) {
                if(outgoing.size()>=32|| (track&&pending.size()>=32))throw std::runtime_error("TCP bounded queue full");
                outgoing.push_back({encode(frame),0});
                if(track)pending.emplace(frame.sequence,PendingAck{frame.command,Clock::now()+config_.ack_timeout});
            };
            while(!stop_) {
                auto now=Clock::now();
                if(now>=next_heartbeat){enqueue({0x01,sequence++,{}},true);next_heartbeat=now+config_.heartbeat;}
                if(now>=next_report) {
                    auto state=state_();if(state.size()>4096)throw std::runtime_error("status payload too large");
                    enqueue({0x10,sequence++,{state.begin(),state.end()}},true);next_report=now+config_.report;
                }
                for(const auto& item:pending)if(now>=item.second.deadline)throw std::runtime_error("TCP ACK timeout");
                pollfd event{socket.fd,static_cast<short>(POLLIN|(outgoing.empty()?0:POLLOUT)),0};
                int ready=poll(&event,1,50);
                if(ready<0&&errno==EINTR)continue;
                if(ready<0||event.revents&(POLLERR|POLLNVAL))throw std::runtime_error("TCP poll error");
                if(event.revents&POLLIN) {
                    uint8_t bytes[8192];auto n=recv(socket.fd,bytes,sizeof(bytes),0);
                    if(n==0)throw std::runtime_error("TCP peer closed");
                    if(n<0 && errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)throw std::runtime_error("TCP receive failed");
                    if(n>0)for(const auto& frame:decoder.feed(bytes,size_t(n))) {
                        {std::lock_guard<std::mutex> lock(mutex_);++status_.received;}
                        if(frame.command==0x7e||frame.command==0x7f) {
                            auto match=pending.find(frame.sequence);
                            bool ok=match!=pending.end() && frame.payload.size()==2 &&
                                frame.payload[0]==match->second.command && frame.payload[1]==0 && frame.command==0x7e;
                            if(ok)pending.erase(match);
                            else {std::lock_guard<std::mutex> lock(mutex_);++status_.rejected_acks;}
                        } else enqueue(commands.handle(frame),false);
                    }
                }
                if(event.revents&POLLOUT && !outgoing.empty()) {
                    auto& front=outgoing.front();
                    auto n=send(socket.fd,front.bytes.data()+front.offset,front.bytes.size()-front.offset,MSG_NOSIGNAL);
                    if(n<0 && errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)throw std::runtime_error("TCP send failed");
                    if(n>0) {
                        front.offset+=size_t(n);
                        if(front.offset==front.bytes.size()) {
                            outgoing.pop_front();std::lock_guard<std::mutex> lock(mutex_);++status_.sent;
                        }
                    }
                }
                if(event.revents&POLLHUP)throw std::runtime_error("TCP hangup");
            }
        }catch(...) {
            std::lock_guard<std::mutex> lock(mutex_);status_.connected=false;++status_.failures;
        }
        retry=Clock::now()+config_.reconnect;
    }
    std::lock_guard<std::mutex> lock(mutex_);status_.connected=false;
}
}

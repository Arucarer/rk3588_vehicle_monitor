#pragma once
#include "vehicle/core.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace vehicle {
struct CanMessage {
    uint32_t id=0;
    uint8_t length=8;
    bool extended=false, remote=false, error=false;
    std::array<uint8_t,8> data{};
};
CanMessage heartbeat(uint8_t node,uint16_t sequence,uint8_t state);
struct NodeStatus {
    uint8_t id=0, state=0;
    uint16_t sequence=0;
    bool online=false, seen=false;
    Clock::time_point last_seen{};
};
struct NodeEvent { uint8_t id; bool online; };
class NodeManager {
    mutable std::mutex mutex_;
    std::map<uint8_t,NodeStatus> nodes_;
    std::chrono::milliseconds timeout_;
    uint64_t rejected_=0;
public:
    explicit NodeManager(std::vector<uint8_t> nodes={1,2},
                         std::chrono::milliseconds timeout=std::chrono::milliseconds(3000));
    bool receive(const CanMessage& frame,Clock::time_point now);
    std::vector<NodeEvent> expire(Clock::time_point now);
    std::vector<NodeStatus> snapshot() const;
    uint64_t rejected() const;
    void invalidate();
};
class SocketCan {
    int fd_=-1;
public:
    explicit SocketCan(const std::string& interface);
    ~SocketCan();
    SocketCan(const SocketCan&)=delete;
    SocketCan& operator=(const SocketCan&)=delete;
    std::optional<CanMessage> receive();
    bool send(const CanMessage& frame);
};
struct CanServiceStatus { bool connected=false; uint64_t received=0, errors=0, reconnects=0; };
class CanService {
    NodeManager nodes_;
    std::string interface_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    CanServiceStatus status_;
    std::thread worker_;
    void run() noexcept;
public:
    explicit CanService(std::string interface);
    ~CanService();
    std::vector<NodeStatus> nodes() const {return nodes_.snapshot();}
    CanServiceStatus status() const;
};
std::string config_text(const Config& config);
struct ParameterSnapshot { Config config; uint64_t revision=0; };
class ParameterStore {
    mutable std::mutex mutex_;
    std::mutex update_mutex_;
    ParameterSnapshot state_;
    std::string writable_path_;
public:
    ParameterStore(Config initial,std::string writable_path={});
    ParameterSnapshot snapshot() const;
    // Only explicitly whitelisted parameters. Durable write precedes publication.
    uint8_t set(const std::string& key_value);
};
class CommandHandler {
    ParameterStore& parameters_;
    std::function<std::string()> state_;
    std::deque<std::pair<Frame,Frame>> cache_;
public:
    CommandHandler(ParameterStore& parameters,std::function<std::string()> state);
    Frame handle(const Frame& request);
    void new_session(){cache_.clear();}
};
struct NetworkConfig {
    std::string address="127.0.0.1";
    uint16_t port=9000;
    std::chrono::milliseconds report{1000}, heartbeat{1000}, ack_timeout{3000}, reconnect{1000};
    void validate() const;
};
struct NetworkStatus {
    bool connected=false;
    uint64_t connections=0, failures=0, received=0, sent=0, rejected_acks=0;
};
class TcpClient {
    NetworkConfig config_;
    ParameterStore& parameters_;
    std::function<std::string()> state_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    NetworkStatus status_;
    std::thread worker_;
    void run() noexcept;
public:
    TcpClient(NetworkConfig config,ParameterStore& parameters,std::function<std::string()> state);
    ~TcpClient();
    NetworkStatus status() const;
};
std::string snapshot_text(const Snapshot& state,Clock::time_point now,
                          std::chrono::milliseconds ttl);
}

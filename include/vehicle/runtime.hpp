#pragma once
#include "vehicle/core.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace vehicle {
// Waiting for work is healthy; active work must update progress within its deadline.
class WorkerProgress {
    std::atomic<int64_t> time_{0};
    std::atomic<bool> active_{false};
public:
    void beat(Clock::time_point now=Clock::now()) {
        time_=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        active_=true;
    }
    void idle(){active_=false;}
    bool healthy(std::chrono::milliseconds timeout,Clock::time_point now)const {
        if(!active_)return true;
        const auto recorded=time_.load();
        auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()-recorded;
        return elapsed>=0 && elapsed<=std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
    }
    bool healthy(std::chrono::milliseconds timeout)const {
        if(!active_)return true;
        // Read the producer timestamp BEFORE reading now: the producer can update concurrently.
        auto recorded=time_.load();
        auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()-recorded;
        return elapsed>=0 && elapsed<=std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
    }
};
struct FaultInjection {
    std::string name;
    std::chrono::milliseconds after{1000};
    Clock::time_point start=Clock::now();
    bool active(const std::string& target)const{return name==target && Clock::now()-start>=after;}
    static std::shared_ptr<FaultInjection> parse(const std::string& value);
};
struct RuntimeMetrics {
    uint64_t snapshots=0, faults=0, log_pending=0, log_critical_dropped=0, log_sample_dropped=0;
    bool network_connected=false;
    std::vector<std::string> unhealthy;
};
class RuntimeSupervisor {
    std::function<RuntimeMetrics()> inspect_;
    std::function<void()> trip_;
    std::string metrics_path_;
    std::atomic<bool> stop_{false},tripped_{false};
    std::thread worker_;
    void run()noexcept;
public:
    // No Linux watchdog is opened. eligible ticks model the health-gated feeding policy only.
    RuntimeSupervisor(std::function<RuntimeMetrics()> inspect,std::function<void()> trip,
                      std::string metrics_path={});
    ~RuntimeSupervisor();
    bool tripped()const{return tripped_;}
};
}

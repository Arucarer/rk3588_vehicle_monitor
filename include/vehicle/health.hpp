#pragma once
#include "vehicle/core.hpp"
#include <map>
#include <stdexcept>

namespace vehicle {
// Callers report progress only after completing useful work.
class HealthMonitor {
    struct Worker {
        std::chrono::milliseconds timeout;
        Clock::time_point progress;
    };
    mutable std::mutex mutex_;
    std::map<std::string, Worker> workers_;
public:
    void add(const std::string& name, std::chrono::milliseconds timeout,
             Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (name.empty() || timeout.count() <= 0 || workers_.count(name))
            throw std::invalid_argument("invalid or duplicate worker");
        workers_.emplace(name, Worker{timeout, now});
    }
    void progress(const std::string& name, Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& worker = workers_.at(name);
        if (now < worker.progress) throw std::invalid_argument("time moved backwards");
        worker.progress = now;
    }
    std::vector<std::string> stalled(Clock::time_point now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& item : workers_)
            if (now < item.second.progress || now - item.second.progress > item.second.timeout)
                result.push_back(item.first);
        return result;
    }
};
}

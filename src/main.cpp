#include "vehicle/core.hpp"
#include "vehicle/health.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }

struct Options {
    bool simulation = false;
    bool version = false;
    bool help = false;
    double seconds = 5;
    vehicle::Config config;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--simulate") options.simulation = true;
        else if (arg == "--version") options.version = true;
        else if (arg == "--help") options.help = true;
        else if (arg == "--duration" && i + 1 < argc) {
            std::string value = argv[++i];
            size_t consumed = 0;
            options.seconds = std::stod(value, &consumed);
            if (consumed != value.size()) throw std::invalid_argument("invalid duration");
        } else if (arg == "--config" && i + 1 < argc) {
            options.config = vehicle::Config::load(argv[++i]);
        } else throw std::invalid_argument("unknown option or missing value: " + arg);
    }
    if (!std::isfinite(options.seconds) || options.seconds <= 0 || options.seconds > 86400)
        throw std::invalid_argument("duration must be greater than 0 and at most 86400 seconds");
    options.config.validate();
    return options;
}
}

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        if (options.help) {
            std::cout << "vehicle-monitor --simulate [--duration seconds] [--config path]\n"
                         "vehicle-monitor --version\n";
            return 0;
        }
        if (options.version) { std::cout << "vehicle-monitor 0.1.0\n"; return 0; }
        if (!options.simulation)
            throw std::runtime_error("hardware backend not commissioned; use --simulate explicitly");
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        vehicle::StateStore store;
        vehicle::BoundedQueue<std::string> logs(128);
        vehicle::HealthMonitor health;
        health.add("acquisition", std::chrono::milliseconds(500), vehicle::Clock::now());
        std::atomic<bool> running{true};
        std::thread logger([&] {
            while (auto line = logs.pop()) std::cout << *line << std::endl;
        });
        std::thread acquisition;
        try {
            acquisition = std::thread([&] {
                try {
                    auto next = vehicle::Clock::now();
                    unsigned tick = 0;
                    while (running) {
                        vehicle::Snapshot s;
                        const auto now = vehicle::Clock::now();
                        s.speed = {tick++ % 30 < 15 ? 60.0 : 90.0, true, now,
                                   "km/h", "simulation", 0};
                        store.publish(s);
                        health.progress("acquisition", now);
                        next += std::chrono::milliseconds(100);
                        std::this_thread::sleep_until(next);
                    }
                } catch (...) { running = false; }
            });
            vehicle::ThresholdFault overspeed(options.config.overspeed_kmh,
                                             options.config.hysteresis_kmh);
            const auto end = vehicle::Clock::now() + std::chrono::duration<double>(options.seconds);
            bool failed = false;
            while (!stopping && vehicle::Clock::now() < end) {
                if (!running || !health.stalled(vehicle::Clock::now()).empty()) {
                    failed = true;
                    break;
                }
                auto s = store.snapshot();
                if (s.sequence && overspeed.update(s.speed.value,
                    s.speed.fresh(vehicle::Clock::now(),
                                  std::chrono::milliseconds(options.config.stale_ms)))) {
                    logs.push("SIMULATION overspeed state=" +
                              std::to_string(static_cast<int>(overspeed.state())) +
                              " sequence=" + std::to_string(s.sequence));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            running = false;
            acquisition.join();
            logs.close();
            logger.join();
            std::cout << "SIMULATION stopped; log_dropped=" << logs.dropped() << '\n';
            return failed || logs.dropped() ? 2 : 0;
        } catch (...) {
            running = false;
            if (acquisition.joinable()) acquisition.join();
            logs.close();
            logger.join();
            throw;
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#pragma once
#include "vehicle/core.hpp"
#include "vehicle/diagnosis.hpp"
#include "vehicle/communication.hpp"
#include "vehicle/storage.hpp"

namespace vehicle {
struct GuiState {
    Clock::time_point updated{};
    Snapshot sensors;
    std::vector<FaultStatus> faults;
    std::vector<NodeStatus> nodes;
    NetworkStatus network;
    bool network_enabled=false, can_enabled=false, settings_writable=false, persistent_history=false;
    Config config;
    std::vector<LogRecord> history;
};
class GuiStateStore {
    mutable std::mutex mutex_;
    GuiState state_;
public:
    void publish(GuiState state){std::lock_guard<std::mutex> lock(mutex_);state_=std::move(state);}
    GuiState snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return state_;}
};
struct GuiCallbacks {
    std::function<bool()> acknowledge;
    std::function<bool(const std::string&)> set_parameter;
};
class GuiWindow {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit GuiWindow(bool headless,GuiCallbacks callbacks={});
    ~GuiWindow();
    GuiWindow(const GuiWindow&)=delete;
    GuiWindow& operator=(const GuiWindow&)=delete;
    void update(const GuiState& state,Clock::time_point now,const std::string& feedback={});
    // All methods, LVGL calls and display/input callbacks belong to ONE thread.
    bool step(uint32_t elapsed_ms);
    void navigate(unsigned page);
    void click(int x,int y);
    void save_ppm(const std::string& path)const;
    unsigned page()const;
    bool layout_valid()const;
    std::vector<std::string> visible_text()const;
};
class GuiService {
    GuiStateStore& states_;
    ParameterStore& parameters_;
    std::function<bool()> acknowledge_;
    bool headless_;
    std::string capture_;
    std::atomic<bool> stop_{false},failed_{false},finished_{false};
    BoundedQueue<std::string> settings_{8};
    std::mutex mutex_;
    std::string feedback_;
    std::thread settings_worker_,gui_worker_;
    void run()noexcept;
public:
    GuiService(GuiStateStore& states,ParameterStore& parameters,std::function<bool()> acknowledge,
               bool headless,std::string capture={});
    ~GuiService();
    void close();
    bool failed()const{return failed_;}
    bool finished()const{return finished_;}
};
}

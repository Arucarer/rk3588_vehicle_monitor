#include "vehicle/gui.hpp"
#include <iostream>
namespace vehicle {
GuiService::GuiService(GuiStateStore& states,ParameterStore& parameters,std::function<bool()> acknowledge,
                       bool headless,std::string capture,std::shared_ptr<FaultInjection> injection):states_(states),injection_(std::move(injection)),parameters_(parameters),
    acknowledge_(std::move(acknowledge)),headless_(headless),capture_(std::move(capture)) {
    progress_.beat();
    settings_worker_=std::thread([this] {
        try {
            while(auto request=settings_.pop()) {
                settings_progress_.beat();
                auto error=parameters_.set(*request);
                settings_progress_.idle();
                std::lock_guard<std::mutex> lock(mutex_);
                feedback_=error==0?"保存成功":error==2?"参数错误":error==3?"配置只读":"保存失败";
            }
        }catch(...){failed_=true;}
    });
    try{gui_worker_=std::thread(&GuiService::run,this);}
    catch(...){settings_.close();settings_worker_.join();throw;}
}
GuiService::~GuiService() {close();}
void GuiService::close() {
    stop_=true;if(gui_worker_.joinable())gui_worker_.join();
    settings_.close();if(settings_worker_.joinable())settings_worker_.join();
}
void GuiService::run()noexcept {
    try {
        GuiWindow window(headless_,{acknowledge_,[this](const std::string& key_value){return settings_.push(key_value);}});
        auto previous=Clock::now(),next=previous;
        auto feedback_until=previous;std::string displayed;
        while(!stop_) {
            while(injection_&&injection_->active("gui-stall")&&!stop_)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto now=Clock::now();
            if(now>=next) {
                {std::lock_guard<std::mutex> lock(mutex_);if(!feedback_.empty()){
                    displayed=std::move(feedback_);feedback_.clear();feedback_until=now+std::chrono::seconds(3);}}
                window.update(states_.snapshot(),now,now<feedback_until?displayed:"");
                next=now+std::chrono::milliseconds(100);
            }
            auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now-previous).count();
            if(ms>0){if(!window.step(static_cast<uint32_t>(ms)))break;previous=now;}
            progress_.beat();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        window.step(40);
        if(!capture_.empty())window.save_ppm(capture_);
    }catch(const std::exception& e){std::cerr<<"GUI failed: "<<e.what()<<'\n';failed_=true;}
    catch(...){std::cerr<<"GUI failed: unknown exception\n";failed_=true;}
    finished_=true;
}
}

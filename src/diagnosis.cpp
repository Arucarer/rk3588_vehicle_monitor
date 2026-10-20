#include "vehicle/diagnosis.hpp"
namespace vehicle {
void FaultRule::validate() const {
    if (id.empty()||!std::isfinite(threshold)||!std::isfinite(hysteresis)||hysteresis<0||
        trigger.count()<0||recovery.count()<0||ttl.count()<=0||severity==Severity::normal||
        (absolute && (low || threshold<hysteresis))) throw std::invalid_argument("invalid fault rule");
}
void Diagnosis::add(FaultRule rule) {
    rule.validate();
    if(entries_.count(rule.id)) throw std::invalid_argument("duplicate fault rule");
    auto id=rule.id;
    entries_.emplace(id,Entry{rule,{id,FaultState::normal,Severity::normal,false},{},{}});
}
std::vector<FaultEvent> Diagnosis::evaluate(const std::map<std::string,Measurement>& inputs,Clock::time_point now) {
    if(last_ && now<*last_) throw std::invalid_argument("unordered diagnosis time");
    last_=now;
    std::vector<FaultEvent> events;
    for(auto& pair:entries_) {
        auto& e=pair.second;auto old=e.status;
        auto input=inputs.find(pair.first);
        bool valid=input!=inputs.end() && input->second.fresh(now,e.rule.ttl) && std::isfinite(input->second.value);
        if(!valid) {
            e.active_since.reset();e.clear_since.reset();
            e.status.state=FaultState::fault;e.status.severity=Severity::fault;
        } else {
            double x=input->second.value;
            if(e.rule.absolute)x=std::abs(x);
            bool abnormal=e.rule.low ? x<e.rule.threshold : x>e.rule.threshold;
            bool clear=e.rule.low ? x>=e.rule.threshold+e.rule.hysteresis : x<=e.rule.threshold-e.rule.hysteresis;
            bool active=e.status.state==FaultState::warning||e.status.state==FaultState::fault;
            if(abnormal) {
                e.clear_since.reset();
                if(!e.active_since)e.active_since=now;
                if(now-*e.active_since>=e.rule.trigger) {
                    e.status.state=e.rule.severity==Severity::fault?FaultState::fault:FaultState::warning;
                    e.status.severity=e.rule.severity;
                }
            } else {
                e.active_since.reset();
                if(clear) {
                    if(active) {
                        if(!e.clear_since)e.clear_since=now;
                        if(now-*e.clear_since>=e.rule.recovery) {
                            e.status.state=FaultState::recovered;e.status.severity=Severity::normal;
                            e.clear_since.reset();
                        }
                    } else if(e.status.state==FaultState::recovered) {
                        e.status.state=FaultState::normal;
                    }
                } else e.clear_since.reset();
            }
        }
        if(old.state!=e.status.state || old.severity!=e.status.severity) {
            e.status.acknowledged=false;
            events.push_back({++sequence_,now,e.status});
        }
    }
    return events;
}
void Diagnosis::acknowledge(const std::string& id) { entries_.at(id).status.acknowledged=true; }
std::vector<FaultStatus> Diagnosis::statuses() const {
    std::vector<FaultStatus> out;for(const auto& p:entries_)out.push_back(p.second.status);return out;
}
AlarmOutput alarm_output(const std::vector<FaultStatus>& faults,Clock::time_point now) {
    AlarmOutput out;
    Severity audible=Severity::normal;
    for(const auto& f:faults) {
        if(f.state!=FaultState::warning && f.state!=FaultState::fault)continue;
        if(f.severity>out.severity)out.severity=f.severity;
        if(!f.acknowledged && f.severity>audible)audible=f.severity;
    }
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    out.led=out.severity==Severity::fault ? ms%200<100 : out.severity==Severity::warning && ms%1000<500;
    out.buzzer=audible==Severity::fault || (audible==Severity::warning && ms%1000<100);
    return out;
}
}

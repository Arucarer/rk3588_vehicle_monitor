#pragma once
#include "vehicle/core.hpp"
#include <map>

namespace vehicle {
enum class Severity { normal, warning, fault };
struct FaultRule {
    std::string id;
    double threshold=80, hysteresis=5;
    bool absolute=false, low=false;
    Severity severity=Severity::warning;
    std::chrono::milliseconds trigger{100}, recovery{200}, ttl{500};
    void validate() const;
};
struct FaultStatus {
    std::string id;
    FaultState state=FaultState::normal;
    Severity severity=Severity::normal;
    bool acknowledged=false;
};
struct FaultEvent { uint64_t sequence; Clock::time_point time; FaultStatus status; };
class Diagnosis {
    struct Entry {
        FaultRule rule;
        FaultStatus status;
        std::optional<Clock::time_point> active_since, clear_since;
    };
    std::map<std::string,Entry> entries_;
    std::optional<Clock::time_point> last_;
    uint64_t sequence_=0;
public:
    void add(FaultRule rule);
    void configure(FaultRule rule);
    // Missing, invalid, future or stale measurements immediately become sensor faults.
    std::vector<FaultEvent> evaluate(const std::map<std::string,Measurement>& inputs, Clock::time_point now);
    void acknowledge(const std::string& id);
    std::vector<FaultStatus> statuses() const;
};
struct AlarmOutput { Severity severity=Severity::normal; bool led=false, buzzer=false; };
AlarmOutput alarm_output(const std::vector<FaultStatus>& faults, Clock::time_point now);
}

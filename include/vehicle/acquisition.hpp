#pragma once
#include "vehicle/core.hpp"
#include <array>

namespace vehicle {
struct SpeedConfig {
    double circumference_m = 2, pulses_per_rev = 1;
    std::chrono::microseconds debounce{0};
    std::chrono::milliseconds stationary_timeout{2000};
    void validate() const;
};
// Single producer owns each processor. Inputs are monotonic edge timestamps.
class SpeedEstimator {
    SpeedConfig config_;
    std::optional<Clock::time_point> previous_, last_, window_;
    uint64_t count_ = 0;
public:
    explicit SpeedEstimator(SpeedConfig config);
    bool pulse(Clock::time_point time);
    Measurement sample(Clock::time_point now, bool counter_healthy = true);
};
struct ImuSample {
    std::array<double,3> acceleration_g{};
    std::array<double,3> gyro_dps{};
    Clock::time_point time{};
    bool valid = false;
};
struct AttitudeResult { Measurement pitch, roll, vibration; };
class AttitudeEstimator {
    std::array<double,3> gyro_bias_{};
    std::optional<Clock::time_point> last_;
    double pitch_ = 0, roll_ = 0, tau_;
public:
    explicit AttitudeEstimator(double tau_seconds = 0.5);
    // Calibration requires stationary, level samples; subtract gyro zero bias only.
    void calibrate(const std::vector<ImuSample>& stationary);
    AttitudeResult update(const ImuSample& sample);
};
struct AnalogConfig {
    double raw_min = 0, raw_max = 4095, gain = 1, offset = 0, alpha = 1;
    std::string unit, source;
    void validate() const;
};
class AnalogChannel {
    AnalogConfig config_;
    std::optional<double> filtered_;
public:
    explicit AnalogChannel(AnalogConfig config);
    Measurement update(double raw, bool valid, Clock::time_point time);
};
class DigitalDebouncer {
    std::chrono::milliseconds delay_;
    bool active_high_, candidate_ = false, stable_ = false, initialized_ = false;
    std::optional<Clock::time_point> since_, last_;
public:
    DigitalDebouncer(bool active_high, std::chrono::milliseconds delay);
    Measurement update(bool level, bool valid, Clock::time_point time);
};
class EnvironmentProcessor {
    std::chrono::milliseconds period_;
    std::optional<Clock::time_point> attempt_;
public:
    explicit EnvironmentProcessor(std::chrono::milliseconds period = std::chrono::milliseconds(2000));
    bool due(Clock::time_point now) const;
    // Adapter marks attempt before I/O, so failed reads cannot trigger rapid retries.
    void attempted(Clock::time_point now);
    std::pair<Measurement,Measurement> decode(const std::array<uint8_t,5>& bytes,
                                            bool io_ok, Clock::time_point now) const;
};
}

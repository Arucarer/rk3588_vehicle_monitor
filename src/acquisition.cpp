#include "vehicle/acquisition.hpp"
#include <algorithm>
#include <numeric>
namespace vehicle {
namespace {
bool finite(const std::array<double,3>& v) {
    return std::all_of(v.begin(),v.end(),[](double x){return std::isfinite(x);});
}
Measurement value(double x, bool valid, Clock::time_point t,
                  const std::string& unit, const std::string& source, int error = 0) {
    return {valid ? x : 0, valid, t, unit, source, valid ? 0 : error};
}
}
void SpeedConfig::validate() const {
    if (!std::isfinite(circumference_m) || circumference_m <= 0 ||
        !std::isfinite(pulses_per_rev) || pulses_per_rev <= 0 || debounce.count() < 0 ||
        stationary_timeout.count() <= 0 || debounce >= stationary_timeout)
        throw std::invalid_argument("invalid speed configuration");
}
SpeedEstimator::SpeedEstimator(SpeedConfig c):config_(c) { config_.validate(); }
bool SpeedEstimator::pulse(Clock::time_point t) {
    if (last_ && t <= *last_) throw std::invalid_argument("unordered pulse");
    if (last_ && t-*last_ < config_.debounce) return false;
    previous_=(last_ && t-*last_ < config_.stationary_timeout) ? last_ : std::nullopt;
    last_=t; ++count_; return true;
}
Measurement SpeedEstimator::sample(Clock::time_point now, bool healthy) {
    if ((window_ && now <= *window_) || (last_ && now < *last_))
        throw std::invalid_argument("unordered speed sample");
    double speed=0;
    if (last_ && now-*last_ < config_.stationary_timeout) {
        if (count_ >= 2 && window_) {
            speed=count_*config_.circumference_m*3.6/config_.pulses_per_rev/
                std::chrono::duration<double>(now-*window_).count();
        } else if (previous_) {
            // At low speed use pulse period, decaying as the next pulse becomes overdue.
            double dt=std::chrono::duration<double>(std::max(*last_-*previous_,now-*last_)).count();
            speed=config_.circumference_m*3.6/config_.pulses_per_rev/dt;
        }
    }
    bool ready=window_.has_value() && (!last_ || now-*last_>=config_.stationary_timeout || previous_.has_value());
    window_=now; count_=0;
    return value(speed,healthy && ready,now,"km/h","hall",1);
}
AttitudeEstimator::AttitudeEstimator(double tau):tau_(tau) {
    if (!std::isfinite(tau) || tau <= 0) throw std::invalid_argument("invalid filter tau");
}
void AttitudeEstimator::calibrate(const std::vector<ImuSample>& samples) {
    if (samples.size()<10) throw std::invalid_argument("need 10 stationary samples");
    std::array<double,3> bias{};
    for (const auto& s:samples) {
        double norm=std::hypot(std::hypot(s.acceleration_g[0],s.acceleration_g[1]),s.acceleration_g[2]);
        if (!s.valid || !finite(s.acceleration_g) || !finite(s.gyro_dps) ||
            std::abs(norm-1)>0.05 || std::abs(s.acceleration_g[0])>0.05 ||
            std::abs(s.acceleration_g[1])>0.05 || s.acceleration_g[2]<0.95 ||
            std::any_of(s.gyro_dps.begin(),s.gyro_dps.end(),[](double v){return std::abs(v)>5;}))
            throw std::invalid_argument("calibration requires level stationary data");
        for (size_t i=0;i<3;++i) bias[i]+=s.gyro_dps[i]/samples.size();
    }
    gyro_bias_=bias; last_.reset();
}
AttitudeResult AttitudeEstimator::update(const ImuSample& s) {
    bool ok=s.valid && finite(s.acceleration_g) && finite(s.gyro_dps);
    double norm=std::hypot(std::hypot(s.acceleration_g[0],s.acceleration_g[1]),s.acceleration_g[2]);
    ok=ok && norm>0.05;
    if (last_ && s.time<=*last_) throw std::invalid_argument("unordered IMU sample");
    if (!ok) { last_.reset(); return {value(0,false,s.time,"deg","imu",1),value(0,false,s.time,"deg","imu",1),value(0,false,s.time,"g","imu",1)}; }
    constexpr double deg=180/3.14159265358979323846;
    double ap=std::atan2(-s.acceleration_g[0],std::hypot(s.acceleration_g[1],s.acceleration_g[2]))*deg;
    double ar=std::atan2(s.acceleration_g[1],s.acceleration_g[2])*deg;
    double dt=last_ ? std::chrono::duration<double>(s.time-*last_).count() : 0;
    if (!last_ || dt>0.2) { pitch_=ap; roll_=ar; }
    else {
        double a=tau_/(tau_+dt);
        // Ignore gravity correction during strong linear acceleration.
        if (std::abs(norm-1)>0.2) a=1;
        pitch_=a*(pitch_+(s.gyro_dps[1]-gyro_bias_[1])*dt)+(1-a)*ap;
        roll_=a*(roll_+(s.gyro_dps[0]-gyro_bias_[0])*dt)+(1-a)*ar;
    }
    last_=s.time;
    return {value(pitch_,true,s.time,"deg","imu"),value(roll_,true,s.time,"deg","imu"),
            value(std::abs(norm-1),true,s.time,"g","imu")};
}
void AnalogConfig::validate() const {
    if (!std::isfinite(raw_min)||!std::isfinite(raw_max)||raw_min>=raw_max||
        !std::isfinite(gain)||!std::isfinite(offset)||!std::isfinite(alpha)||alpha<=0||alpha>1||
        unit.empty()||source.empty()) throw std::invalid_argument("invalid analog configuration");
}
AnalogChannel::AnalogChannel(AnalogConfig c):config_(std::move(c)){config_.validate();}
Measurement AnalogChannel::update(double raw,bool valid,Clock::time_point t) {
    double x=raw*config_.gain+config_.offset;
    if (!valid||!std::isfinite(raw)||raw<config_.raw_min||raw>config_.raw_max||!std::isfinite(x)) {
        filtered_.reset();return value(0,false,t,config_.unit,config_.source,1);
    }
    filtered_=filtered_ ? config_.alpha*x+(1-config_.alpha)* *filtered_ : x;
    return value(*filtered_,true,t,config_.unit,config_.source);
}
DigitalDebouncer::DigitalDebouncer(bool high,std::chrono::milliseconds delay):delay_(delay),active_high_(high) {
    if (delay.count()<0) throw std::invalid_argument("negative debounce");
}
Measurement DigitalDebouncer::update(bool level,bool valid,Clock::time_point t) {
    if (last_ && t<*last_) throw std::invalid_argument("unordered digital sample");
    last_=t;
    if (!valid) { since_.reset();initialized_=false;return value(0,false,t,"bool","digital",1); }
    bool active=level==active_high_;
    if (!since_ || candidate_!=active) { candidate_=active;since_=t; }
    if (t-*since_>=delay_) { stable_=candidate_;initialized_=true; }
    return value(stable_?1:0,initialized_,t,"bool","digital",2);
}
EnvironmentProcessor::EnvironmentProcessor(std::chrono::milliseconds p):period_(p) {
    if (p<std::chrono::milliseconds(2000)) throw std::invalid_argument("DHT11 period below 2s");
}
bool EnvironmentProcessor::due(Clock::time_point t) const { return !attempt_ || (t>=*attempt_ && t-*attempt_>=period_); }
void EnvironmentProcessor::attempted(Clock::time_point t) {
    if (!due(t)) throw std::invalid_argument("DHT11 read attempted too soon");
    attempt_=t;
}
std::pair<Measurement,Measurement> EnvironmentProcessor::decode(const std::array<uint8_t,5>& b,bool io,Clock::time_point t) const {
    // Common positive-temperature DHT11 decimal-byte layout; verify actual module datasheet.
    double humidity=b[0]+b[1]*0.1,temperature=b[2]+b[3]*0.1;
    bool ok=io && uint8_t(b[0]+b[1]+b[2]+b[3])==b[4] && b[1]<=9 && b[3]<=9 && humidity<=100 && temperature<=50;
    return {value(temperature,ok,t,"degC","dht11",1),value(humidity,ok,t,"%RH","dht11",1)};
}
}

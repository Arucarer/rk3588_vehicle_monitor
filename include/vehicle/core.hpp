#pragma once
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <map>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>
namespace vehicle {
using Clock = std::chrono::steady_clock;
struct Measurement { double value = 0; bool valid = false; Clock::time_point sampled{}; std::string unit; std::string source; int error = 0;
 bool fresh(Clock::time_point now, std::chrono::milliseconds ttl) const { return valid && std::isfinite(value) && now >= sampled && now-sampled <= ttl; }
};
struct Snapshot { uint64_t sequence=0; Measurement speed, pitch, roll, temperature, humidity, vibration;
 std::map<std::string,Measurement> analog, digital; };
class StateStore { mutable std::mutex mutex_; Snapshot state_; public: void publish(Snapshot s); Snapshot snapshot() const; };
template<class T> class BoundedQueue {
 std::mutex mutex_; std::condition_variable cv_; std::deque<T> queue_; size_t capacity_; bool closed_=false; uint64_t dropped_=0;
 public:
 explicit BoundedQueue(size_t capacity):capacity_(capacity) { if(!capacity) throw std::invalid_argument("zero queue capacity"); }
 bool push(T value) { std::lock_guard<std::mutex> l(mutex_); if(closed_) return false; if(queue_.size()==capacity_) { ++dropped_; return false; } queue_.push_back(std::move(value)); cv_.notify_one(); return true; }
 std::optional<T> pop() { std::unique_lock<std::mutex> l(mutex_); cv_.wait(l,[&]{return closed_ || !queue_.empty();}); if(queue_.empty()) return {}; auto v=std::move(queue_.front()); queue_.pop_front(); return v; }
 void close() { std::lock_guard<std::mutex> l(mutex_); closed_=true; cv_.notify_all(); }
 uint64_t dropped() { std::lock_guard<std::mutex> l(mutex_); return dropped_; }
};
struct Config { double circumference_m=2.0, pulses_per_rev=1, overspeed_kmh=80, hysteresis_kmh=5; unsigned stale_ms=500; static Config load(const std::string& path); void validate() const; };
double speed_kmh(uint64_t pulses, double elapsed_s, const Config& config);
enum class FaultState { normal, warning, fault, recovered };
class ThresholdFault { double limit_, hysteresis_; bool acknowledged_=false; FaultState state_=FaultState::normal; public:
 ThresholdFault(double limit,double hysteresis):limit_(limit),hysteresis_(hysteresis) {}
 bool update(double value,bool valid); void acknowledge(){acknowledged_=true;} bool acknowledged()const{return acknowledged_;} FaultState state()const{return state_;}
};
uint32_t crc32(const uint8_t* data,size_t size);
struct Frame { uint8_t command=0; uint32_t sequence=0; std::vector<uint8_t> payload; };
std::vector<uint8_t> encode(const Frame& frame);
class StreamDecoder { std::vector<uint8_t> pending_; public: std::vector<Frame> feed(const uint8_t* data,size_t size); size_t buffered()const{return pending_.size();} };
}

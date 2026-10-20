#pragma once
#include "vehicle/core.hpp"
#include "vehicle/runtime.hpp"
#include "vehicle/version.hpp"
#include <memory>
#include <atomic>
#include <thread>

namespace vehicle {
struct StorageGeometry { size_t erase_size=4096, page_size=256, blocks=258; };
// One owner. No implicit erase of a Linux MTD partition is implemented here.
class NorDevice {
public:
    virtual ~NorDevice() = default;
    virtual StorageGeometry geometry() const = 0;
    virtual void read(size_t offset, uint8_t* data, size_t length) = 0;
    virtual void program(size_t offset, const uint8_t* data, size_t length) = 0;
    virtual void erase(size_t block) = 0;
    virtual void sync() = 0;
};
class FileNor final : public NorDevice {
    int fd_=-1;
    StorageGeometry geometry_;
public:
    // create=true creates a NEW regular file with O_EXCL; never truncates existing data.
    FileNor(const std::string& path, StorageGeometry geometry, bool create=false);
    ~FileNor() override;
    StorageGeometry geometry() const override { return geometry_; }
    void read(size_t offset,uint8_t* data,size_t length) override;
    void program(size_t offset,const uint8_t* data,size_t length) override;
    void erase(size_t block) override;
    void sync() override;
};
enum class RecordType : uint8_t { sample=1, fault=2, diagnostic=3 };
struct LogRecord {
    uint64_t sequence=0, monotonic_ns=0, utc_ms=0;
    bool clock_synchronized=false;
    RecordType type=RecordType::diagnostic;
    std::string boot_id, software_version=vehicle::software_version, config_version, payload;
};
struct RecoveryStats { size_t valid=0, incomplete=0, corrupt=0; };
class CircularLog {
    std::shared_ptr<NorDevice> device_;
    size_t capacity_;
    uint64_t next_sequence_=1;
    std::vector<std::pair<size_t,LogRecord>> records_;
    RecoveryStats recovery_;
    bool failed_=false;
    void recover();
public:
    // Block 0 is a CRC-protected immutable format header; capacity+1 data blocks.
    CircularLog(std::shared_ptr<NorDevice> device,size_t capacity,bool initialize=false);
    uint64_t append(LogRecord record);
    std::vector<LogRecord> records() const;
    RecoveryStats recovery_stats() const { return recovery_; }
    size_t capacity() const { return capacity_; }
};
void export_jsonl(const std::vector<LogRecord>& records,const std::string& path);
class LogService {
    CircularLog& log_;
    WorkerProgress progress_;
    std::shared_ptr<FaultInjection> injection_;
    std::atomic<bool> stop_wait_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<LogRecord> critical_, samples_, recent_;
    size_t critical_capacity_, sample_capacity_;
    bool closing_=false;
    std::atomic<bool> failed_{false};
    uint64_t dropped_critical_=0, dropped_samples_=0;
    std::thread worker_;
    void run() noexcept;
public:
    LogService(CircularLog& log,size_t critical_capacity=128,size_t sample_capacity=64,std::shared_ptr<FaultInjection> injection={});
    ~LogService();
    bool submit(LogRecord record,bool critical);
    void close();
    bool failed() const { return failed_; }
    bool healthy()const{return !failed_&&progress_.healthy(std::chrono::milliseconds(2000));}
    size_t pending();
    std::pair<uint64_t,uint64_t> dropped();
    std::vector<LogRecord> history();
};
}

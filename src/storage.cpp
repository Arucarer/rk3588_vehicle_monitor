#include "vehicle/storage.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vehicle {
namespace {
constexpr uint32_t header_magic=0x564c4f47, record_magic=0x56455654, committed=0x434f4d54;
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void put(std::vector<uint8_t>& b,size_t offset,uint64_t n,size_t bytes) {
    for(size_t i=0;i<bytes;++i)b.at(offset+i)=static_cast<uint8_t>(n>>(8*(bytes-i-1)));
}
uint64_t get(const std::vector<uint8_t>& b,size_t offset,size_t bytes) {
    uint64_t n=0;for(size_t i=0;i<bytes;++i)n=(n<<8)|b.at(offset+i);return n;
}
bool blank(const std::vector<uint8_t>& b) {return std::all_of(b.begin(),b.end(),[](uint8_t x){return x==255;});}
void geometry_valid(StorageGeometry g) {
    require(g.page_size>=4 && g.erase_size>=256 && g.erase_size%g.page_size==0 &&
            g.blocks>=3 && g.blocks<=65536 && g.erase_size<=65536 &&
            g.page_size<=g.erase_size,"invalid NOR geometry");
}
void write_pages(NorDevice& d,size_t offset,const std::vector<uint8_t>& bytes) {
    auto page=d.geometry().page_size;
    size_t position=0;
    while(position<bytes.size()) {
        size_t n=std::min(page-(offset+position)%page,bytes.size()-position);
        d.program(offset+position,bytes.data()+position,n);position+=n;
    }
}
std::vector<uint8_t> serialize(const LogRecord& r,size_t block_size) {
    size_t total=64+r.boot_id.size()+r.software_version.size()+r.config_version.size()+r.payload.size();
    require(total<=block_size-4 && r.boot_id.size()<=128 && r.software_version.size()<=128 &&
            r.config_version.size()<=128 && !r.boot_id.empty() && !r.config_version.empty() &&
            !r.software_version.empty(),"invalid record metadata or payload size");
    require(r.type>=RecordType::sample && r.type<=RecordType::diagnostic,"invalid record type");
    std::vector<uint8_t> b(total,0);
    put(b,0,record_magic,4);put(b,4,1,2);put(b,6,static_cast<uint8_t>(r.type),1);
    put(b,7,r.clock_synchronized?1:0,1);put(b,8,total,4);put(b,12,r.sequence,8);
    put(b,20,r.monotonic_ns,8);put(b,28,r.utc_ms,8);
    put(b,36,r.boot_id.size(),2);put(b,38,r.software_version.size(),2);
    put(b,40,r.config_version.size(),2);put(b,42,r.payload.size(),4);
    size_t pos=64;
    for(const auto* s:{&r.boot_id,&r.software_version,&r.config_version,&r.payload}) {
        std::copy(s->begin(),s->end(),b.begin()+pos);pos+=s->size();
    }
    // CRC field itself is zero during calculation; includes every metadata byte.
    put(b,48,crc32(b.data(),b.size()),4);return b;
}
LogRecord deserialize(std::vector<uint8_t> b) {
    require(get(b,0,4)==record_magic && get(b,4,2)==1,"unknown record format");
    size_t total=get(b,8,4);
    require(total>=64 && total<=b.size()-4,"invalid record length");
    b.resize(total);uint32_t crc=get(b,48,4);put(b,48,0,4);
    require(crc32(b.data(),b.size())==crc,"record CRC mismatch");
    size_t boot=get(b,36,2),software=get(b,38,2),config=get(b,40,2),payload=get(b,42,4);
    require(boot>0 && boot<=128 && software>0 && software<=128 && config>0 && config<=128 &&
            boot+software+config+payload==total-64,"invalid metadata lengths");
    require(b[6]>=1 && b[6]<=3 && b[7]<=1,"invalid record flags");
    LogRecord r;r.type=static_cast<RecordType>(b[6]);r.clock_synchronized=b[7];
    r.sequence=get(b,12,8);require(r.sequence>0,"zero sequence");
    r.monotonic_ns=get(b,20,8);r.utc_ms=get(b,28,8);
    size_t pos=64;
    auto str=[&](size_t n){std::string s(b.begin()+pos,b.begin()+pos+n);pos+=n;return s;};
    r.boot_id=str(boot);r.software_version=str(software);r.config_version=str(config);r.payload=str(payload);
    return r;
}
std::string quoted(const std::string& text) {
    std::string out="\"";const char* hex="0123456789abcdef";
    // Escape bytes >=128 too: exports remain valid JSON even for damaged/non-UTF8 input.
    for(unsigned char c:text) {
        if(c=='"'||c=='\\'){out+='\\';out+=c;}
        else if(c<32||c>=128){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=c;
    }
    return out+'"';
}
}
FileNor::FileNor(const std::string& path,StorageGeometry g,bool create):geometry_(g) {
    geometry_valid(g);
    fd_=::open(path.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|(create?(O_CREAT|O_EXCL):0),0600);
    require(fd_>=0,"cannot open file NOR (existing files are never overwritten on create)");
    try {
        struct stat st{};require(fstat(fd_,&st)==0 && S_ISREG(st.st_mode),"NOR simulator requires regular file");
        require(flock(fd_,LOCK_EX|LOCK_NB)==0,"NOR file already in use");
        if(create) {
            std::vector<uint8_t> erased(g.erase_size,255);
            for(size_t i=0;i<g.blocks;++i) {
                require(pwrite(fd_,erased.data(),erased.size(),i*g.erase_size)==static_cast<ssize_t>(erased.size()),"NOR initialization write failed");
            }
            sync();
        } else require(static_cast<uint64_t>(st.st_size)==g.blocks*g.erase_size,"NOR geometry/file size mismatch");
    } catch(...) {::close(fd_);fd_=-1;throw;}
}
FileNor::~FileNor(){if(fd_>=0)::close(fd_);}
void FileNor::read(size_t offset,uint8_t* data,size_t length) {
    size_t size=geometry_.blocks*geometry_.erase_size;
    require(offset<=size && length<=size-offset,"NOR read out of bounds");
    require(pread(fd_,data,length,offset)==static_cast<ssize_t>(length),"NOR read failed");
}
void FileNor::program(size_t offset,const uint8_t* data,size_t length) {
    require(length>0 && length<=geometry_.page_size &&
            offset/geometry_.page_size==(offset+length-1)/geometry_.page_size,"NOR page crossing");
    std::vector<uint8_t> old(length);read(offset,old.data(),length);
    for(size_t i=0;i<length;++i)require((old[i]&data[i])==data[i],"NOR attempted 0-to-1 programming");
    require(pwrite(fd_,data,length,offset)==static_cast<ssize_t>(length),"NOR program failed");
}
void FileNor::erase(size_t block) {
    require(block>0 && block<geometry_.blocks,"cannot erase format header or invalid block");
    std::vector<uint8_t> b(geometry_.erase_size,255);
    require(pwrite(fd_,b.data(),b.size(),block*geometry_.erase_size)==static_cast<ssize_t>(b.size()),"NOR erase failed");
}
void FileNor::sync(){require(fdatasync(fd_)==0,"NOR sync failed");}
CircularLog::CircularLog(std::shared_ptr<NorDevice> d,size_t capacity,bool initialize):device_(std::move(d)),capacity_(capacity) {
    require(bool(device_),"missing NOR device");auto g=device_->geometry();geometry_valid(g);
    require(capacity>0 && capacity==g.blocks-2,"capacity/geometry mismatch");
    std::vector<uint8_t> header(g.erase_size);device_->read(0,header.data(),header.size());
    if(initialize) {
        require(blank(header),"format header not erased");
        // Refuse formatting a medium containing old data behind an erased header.
        for(size_t i=1;i<g.blocks;++i){std::vector<uint8_t>b(g.erase_size);device_->read(i*g.erase_size,b.data(),b.size());require(blank(b),"nonempty NOR cannot be initialized");}
        std::vector<uint8_t> h(32,0);put(h,0,header_magic,4);put(h,4,1,4);
        put(h,8,g.erase_size,4);put(h,12,g.page_size,4);put(h,16,g.blocks,4);put(h,20,capacity,4);
        put(h,28,crc32(h.data(),28),4);write_pages(*device_,0,h);device_->sync();
        device_->read(0,header.data(),header.size());
    }
    require(get(header,0,4)==header_magic && get(header,4,4)==1 &&
            get(header,8,4)==g.erase_size && get(header,12,4)==g.page_size &&
            get(header,16,4)==g.blocks && get(header,20,4)==capacity &&
            get(header,28,4)==crc32(header.data(),28),"invalid NOR format header");
    recover();
}
void CircularLog::recover() {
    auto g=device_->geometry();std::set<uint64_t> sequences;
    for(size_t block=1;block<g.blocks;++block) {
        std::vector<uint8_t> b(g.erase_size);device_->read(block*g.erase_size,b.data(),b.size());
        if(blank(b))continue;
        if(get(b,b.size()-4,4)!=committed){++recovery_.incomplete;continue;}
        try {
            auto r=deserialize(b);
            require(sequences.insert(r.sequence).second,"duplicate sequence");
            records_.push_back({block,std::move(r)});++recovery_.valid;
        }catch(const std::runtime_error&){++recovery_.corrupt;}
    }
    std::sort(records_.begin(),records_.end(),[](const auto& a,const auto& b){return a.second.sequence<b.second.sequence;});
    if(!records_.empty()) {
        require(records_.back().second.sequence<std::numeric_limits<uint64_t>::max(),"sequence exhausted");
        next_sequence_=records_.back().second.sequence+1;
    }
    if(records_.size()>capacity_)records_.erase(records_.begin(),records_.end()-capacity_);
}
uint64_t CircularLog::append(LogRecord r) {
    require(!failed_,"storage previously failed; reopen and recover before retry");
    require(next_sequence_<std::numeric_limits<uint64_t>::max(),"sequence exhausted");
    r.sequence=next_sequence_;auto g=device_->geometry();auto bytes=serialize(r,g.erase_size);
    // Exactly one spare block: never erase a retained record before the new commit.
    size_t block=1;
    for(;block<g.blocks;++block)if(std::none_of(records_.begin(),records_.end(),[&](const auto& entry){return entry.first==block;}))break;
    require(block<g.blocks,"missing spare block");
    try {
        device_->erase(block);device_->sync();
        write_pages(*device_,block*g.erase_size,bytes);device_->sync();
        std::vector<uint8_t> marker(4);put(marker,0,committed,4);
        device_->program((block+1)*g.erase_size-4,marker.data(),marker.size());device_->sync();
        records_.push_back({block,std::move(r)});
        if(records_.size()>capacity_)records_.erase(records_.begin());
        return next_sequence_++;
    } catch(...) {failed_=true;throw;}
}
std::vector<LogRecord> CircularLog::records() const {
    std::vector<LogRecord> out;for(const auto& p:records_)out.push_back(p.second);return out;
}
void export_jsonl(const std::vector<LogRecord>& records,const std::string& path) {
    // O_EXCL prevents exports from overwriting their own source or an existing file.
    int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    require(fd>=0,"export destination must be new");
    try {
        for(const auto& r:records) {
            std::string line="{\"format_version\":1,\"sequence\":"+std::to_string(r.sequence)+
                ",\"monotonic_ns\":"+std::to_string(r.monotonic_ns)+",\"utc_ms\":"+std::to_string(r.utc_ms)+
                ",\"clock_synchronized\":"+(r.clock_synchronized?"true":"false")+
                ",\"type\":"+std::to_string(static_cast<int>(r.type))+",\"boot_id\":"+quoted(r.boot_id)+
                ",\"software_version\":"+quoted(r.software_version)+",\"config_version\":"+quoted(r.config_version)+
                ",\"payload\":"+quoted(r.payload)+"}\n";
            size_t done=0;
            while(done<line.size()) {auto n=write(fd,line.data()+done,line.size()-done);if(n<0&&errno==EINTR)continue;require(n>0,"export write failed");done+=static_cast<size_t>(n);}
        }
        require(fdatasync(fd)==0,"export sync failed");::close(fd);
    } catch(...) {::close(fd);throw;}
}
LogService::LogService(CircularLog& log,size_t critical,size_t samples,std::shared_ptr<FaultInjection> injection):log_(log),injection_(std::move(injection)),critical_capacity_(critical),sample_capacity_(samples) {
    require(critical>0 && samples>0,"zero log queue capacity");
    auto initial=log.records();
    auto begin=initial.size()>256?initial.end()-256:initial.begin();
    recent_.assign(begin,initial.end());
    worker_=std::thread(&LogService::run,this);
}
LogService::~LogService(){close();}
bool LogService::submit(LogRecord r,bool critical) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(closing_||failed_)return false;
    auto& queue=critical?critical_:samples_;auto capacity=critical?critical_capacity_:sample_capacity_;
    if(queue.size()>=capacity){if(critical)++dropped_critical_;else ++dropped_samples_;return false;}
    queue.push_back(std::move(r));cv_.notify_one();return true;
}
void LogService::run() noexcept {
    try {
        while(true) {
            LogRecord record;
            {
                std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[&]{return closing_||!critical_.empty()||!samples_.empty();});
                if(critical_.empty()&&samples_.empty())break;
                auto& queue=critical_.empty()?samples_:critical_;
                record=std::move(queue.front());queue.pop_front();
            }
            progress_.beat();
            while(injection_&&injection_->active("storage-stall")&&!stop_wait_)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if((injection_&&injection_->active("storage-fail"))||(stop_wait_&&injection_&&injection_->active("storage-stall")))throw std::runtime_error("injected storage failure");
            record.sequence=log_.append(record);
            {std::lock_guard<std::mutex> lock(mutex_);recent_.push_back(std::move(record));
             const auto limit=std::min(size_t(256),log_.capacity());
             while(recent_.size()>limit)recent_.pop_front();}
            progress_.idle();
        }
    }catch(...) {failed_=true;}
}
void LogService::close() {
    stop_wait_=true;
    {std::lock_guard<std::mutex> lock(mutex_);closing_=true;cv_.notify_all();}
    if(worker_.joinable())worker_.join();
}
std::pair<uint64_t,uint64_t> LogService::dropped(){std::lock_guard<std::mutex> lock(mutex_);return {dropped_critical_,dropped_samples_};}
}

namespace vehicle {
std::vector<LogRecord> LogService::history(){std::lock_guard<std::mutex> lock(mutex_);return {recent_.begin(),recent_.end()};}
}

namespace vehicle {size_t LogService::pending(){std::lock_guard<std::mutex> lock(mutex_);return critical_.size()+samples_.size();}}

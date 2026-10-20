#include "vehicle/storage.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <unistd.h>
using namespace vehicle;
namespace {
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
class MemoryNor : public NorDevice {
public:
    StorageGeometry g;
    std::vector<uint8_t> bytes;
    int cut=-1, operations=0;
    explicit MemoryNor(size_t capacity):g{4096,256,capacity+2},bytes(g.erase_size*g.blocks,255){}
    StorageGeometry geometry()const override{return g;}
    void read(size_t p,uint8_t* out,size_t n)override {
        check(p<=bytes.size()&&n<=bytes.size()-p,"read bounds");std::copy_n(bytes.begin()+p,n,out);
    }
    void step(){if(cut>=0&&operations++==cut)throw std::runtime_error("injected power interruption");}
    void program(size_t p,const uint8_t* data,size_t n)override {
        check(n>0 && p/g.page_size==(p+n-1)/g.page_size,"page crossing");
        for(size_t i=0;i<n;++i){check((bytes.at(p+i)&data[i])==data[i],"NOR 0->1");bytes[p+i]=data[i];if(i==n/2)step();}
    }
    void erase(size_t block)override {
        check(block>0&&block<g.blocks,"erase bounds");
        for(size_t i=0;i<g.erase_size;++i){bytes[block*g.erase_size+i]=255;if(i==g.erase_size/2)step();}
    }
    void sync()override{step();}
};
class GatedNor : public MemoryNor {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered=false, released=false;
public:
    bool armed=false;
    GatedNor():MemoryNor(8){}
    void sync()override {
        if(armed) {
            std::unique_lock<std::mutex> lock(mutex);entered=true;cv.notify_all();
            cv.wait(lock,[&]{return released;});
        }
        MemoryNor::sync();
    }
    void wait_started(){std::unique_lock<std::mutex> lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),"writer did not start");}
    void release(){std::lock_guard<std::mutex> lock(mutex);released=true;cv.notify_all();}
};
LogRecord record(std::string payload="sample") {
    LogRecord r;r.boot_id="test-boot";r.config_version="config-crc:1234";
    r.payload=std::move(payload);r.monotonic_ns=100;r.utc_ms=123456;r.type=RecordType::fault;return r;
}
}
int main(){try {
    auto medium=std::make_shared<MemoryNor>(1024);
    {
        CircularLog log(medium,1024,true);
        for(int i=0;i<1100;++i)check(log.append(record(std::to_string(i)))==static_cast<uint64_t>(i+1),"sequence");
        check(log.records().size()==1024,"capacity exceeded");
    }
    CircularLog recovered(medium,1024);
    auto records=recovered.records();check(records.size()==1024,"1000+ query failed");
    check(records.front().sequence==77&&records.back().sequence==1100,"wrap ordering");
    check(records.back().payload=="1099"&&records.back().boot_id=="test-boot","metadata recovery");
    check(recovered.append(record())==1101,"sequence continuity");

    auto base=std::make_shared<MemoryNor>(4);
    {CircularLog log(base,4,true);for(int i=0;i<9;++i)log.append(record(std::to_string(i)));}
    // Interrupt every erase/page-write/sync/commit boundary while wrapping a full ring.
    for(int cut=0;cut<12;++cut) {
        auto interrupted=std::make_shared<MemoryNor>(*base);
        CircularLog writer(interrupted,4);interrupted->cut=cut;
        bool failed=false;
        try{writer.append(record(std::string(900,'x')));}catch(...){failed=true;}
        if(failed){bool rejected=false;try{writer.append(record());}catch(...){rejected=true;}check(rejected,"retry without recovery");}
        interrupted->cut=-1;
        CircularLog restart(interrupted,4);auto saved=restart.records();
        check(saved.size()==4,"interruption lost retained record");
        check(saved.back().sequence==9||saved.back().sequence==10,"unexpected recovered boundary");
        if(saved.back().sequence==9)check(saved.front().sequence==6,"old records changed before commit");
        check(restart.append(record())==saved.back().sequence+1,"recovery append");
    }
    // Independent corruption must not prevent reading the other committed blocks.
    auto corrupt=std::make_shared<MemoryNor>(*base);
    size_t victim=0;
    for(size_t block=1;block<corrupt->g.blocks;++block)if(corrupt->bytes[block*4096]==0x56){victim=block;break;}
    corrupt->bytes[victim*4096+70]^=1;
    CircularLog damaged(corrupt,4);check(damaged.recovery_stats().corrupt==1,"CRC corruption unreported");
    check(!damaged.records().empty(),"corruption destroyed ring");
    auto header=std::make_shared<MemoryNor>(*base);header->bytes[8]^=1;
    bool rejected=false;try{CircularLog invalid(header,4);}catch(...){rejected=true;}check(rejected,"bad header accepted");

    auto async_medium=std::make_shared<MemoryNor>(32);CircularLog async_log(async_medium,32,true);
    {LogService service(async_log,32,32);for(int i=0;i<20;++i)check(service.submit(record(),i%2==0),"async rejected");service.close();check(!service.failed(),"async failure");check(service.history().size()==20&&service.history().back().sequence==20,"committed GUI history");check(!service.submit(record(),true),"accepted after close");}
    check(async_log.records().size()==20,"close did not drain");
    auto gated=std::make_shared<GatedNor>();CircularLog gated_log(gated,8,true);
    gated->armed=true;
    {
        LogService service(gated_log,1,1);
        check(service.submit(record("first"),false),"first queue submit");gated->wait_started();
        bool normal=service.submit(record("sample"),false);
        bool normal_overflow=service.submit(record("rejected sample"),false);
        bool critical=service.submit(record("critical"),true);
        bool critical_overflow=service.submit(record("rejected critical"),true);
        gated->release();service.close();
        check(normal&&!normal_overflow&&critical&&!critical_overflow,"separate queue reservation");
        check(service.dropped()==std::make_pair(uint64_t(1),uint64_t(1)),"overflow metrics");
        auto saved=gated_log.records();check(saved.size()==3&&saved[1].payload=="critical"&&saved[2].payload=="sample","critical priority");
    }
    auto fail_medium=std::make_shared<MemoryNor>(8);CircularLog fail_log(fail_medium,8,true);
    fail_medium->cut=0;fail_medium->operations=0;
    {LogService service(fail_log);check(service.submit(record(),true),"initial submit");service.close();check(service.failed(),"storage failure hidden");check(!service.submit(record(),true),"failed service accepted");}

    std::string path="/tmp/vehicle-storage-test-"+std::to_string(getpid());
    std::remove(path.c_str());std::remove((path+".jsonl").c_str());
    {
        auto file=std::make_shared<FileNor>(path,StorageGeometry{4096,256,10},true);
        CircularLog log(file,8,true);log.append(record("quote\" newline\n"));
        rejected=false;try{FileNor competing(path,{4096,256,10});}catch(...){rejected=true;}check(rejected,"exclusive lock missing");
        uint8_t byte=255;rejected=false;try{file->program(4096,&byte,1);}catch(...){rejected=true;}check(rejected,"file NOR 0->1 allowed");
        export_jsonl(log.records(),path+".jsonl");
        rejected=false;try{export_jsonl(log.records(),path);}catch(...){rejected=true;}check(rejected,"export overwrote source");
    }
    {
        auto file=std::make_shared<FileNor>(path,StorageGeometry{4096,256,10});CircularLog log(file,8);
        check(log.records().size()==1&&log.records()[0].payload=="quote\" newline\n","file reopen");
    }
    std::remove(path.c_str());std::remove((path+".jsonl").c_str());
    std::cout<<"storage ring, 1024-record query, interruption recovery and async checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

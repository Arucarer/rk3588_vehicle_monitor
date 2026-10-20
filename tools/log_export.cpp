#include "vehicle/storage.hpp"
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("usage: vehicle-log-export NOR_FILE CAPACITY NEW_JSONL_FILE");
        std::string value=argv[2];size_t used=0;auto capacity=std::stoul(value,&used);
        if(used!=value.size()||capacity<1||capacity>65534)throw std::invalid_argument("invalid capacity");
        auto device=std::make_shared<vehicle::FileNor>(argv[1],vehicle::StorageGeometry{4096,256,capacity+2});
        vehicle::CircularLog log(device,capacity);
        vehicle::export_jsonl(log.records(),argv[3]);
        auto stats=log.recovery_stats();
        std::cout<<"exported="<<log.records().size()<<" corrupt="<<stats.corrupt<<" incomplete="<<stats.incomplete<<'\n';
        return stats.corrupt?2:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

#include "vehicle/runtime.hpp"
#include <iostream>
int main(){try {
    using namespace vehicle;using namespace std::chrono_literals;
    WorkerProgress worker;auto t=Clock::now();
    if(!worker.healthy(1ms,t))throw std::runtime_error("idle not healthy");
    worker.beat(t);if(!worker.healthy(10ms,t+10ms)||worker.healthy(10ms,t+11ms))throw std::runtime_error("deadline boundary");
    worker.idle();if(!worker.healthy(1ms,t+10s))throw std::runtime_error("idle timed out");
    std::atomic<bool> stop{false};
    std::thread producer([&]{while(!stop)worker.beat();});
    for(int i=0;i<100000;++i)if(!worker.healthy(1s)){stop=true;producer.join();throw std::runtime_error("concurrent progress false alarm");}
    stop=true;producer.join();
    bool rejected=false;try{FaultInjection::parse("unknown:100");}catch(...){rejected=true;}
    if(!rejected)throw std::runtime_error("unknown injection accepted");
    std::cout<<"runtime progress checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

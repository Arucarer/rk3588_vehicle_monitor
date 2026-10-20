#include "vehicle/core.hpp"
#include "vehicle/health.hpp"
#include "vehicle/acquisition.hpp"
#include "vehicle/diagnosis.hpp"
#include "vehicle/storage.hpp"
#include "vehicle/communication.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }

struct Options {
    bool simulation = false;
    bool version = false;
    bool help = false;
    double seconds = 5;
    std::string log_file;
    size_t log_capacity = 256;
    unsigned sample_period_ms = 1000;
    vehicle::Config config;
    std::string config_path, can_interface;
    bool can_simulate=false, allow_remote_set=false;
    std::optional<vehicle::NetworkConfig> network;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--simulate") options.simulation = true;
        else if (arg == "--version") options.version = true;
        else if (arg == "--help") options.help = true;
        else if (arg == "--can-simulate") options.can_simulate=true;
        else if (arg == "--allow-remote-set") options.allow_remote_set=true;
        else if (arg == "--can-interface" && i+1<argc) options.can_interface=argv[++i];
        else if (arg == "--tcp" && i+1<argc) {
            std::string endpoint=argv[++i];auto colon=endpoint.find(':');
            if(colon==std::string::npos)throw std::invalid_argument("TCP endpoint must be IPv4:port");
            std::string port=endpoint.substr(colon+1);size_t used=0;auto number=std::stoul(port,&used);
            if(used!=port.size()||number<1||number>65535)throw std::invalid_argument("invalid TCP port");
            vehicle::NetworkConfig net;net.address=endpoint.substr(0,colon);net.port=static_cast<uint16_t>(number);
            net.validate();options.network=net;
        }
        else if (arg == "--duration" && i + 1 < argc) {
            std::string value = argv[++i];
            size_t consumed = 0;
            options.seconds = std::stod(value, &consumed);
            if (consumed != value.size()) throw std::invalid_argument("invalid duration");
        } else if (arg == "--log-file" && i + 1 < argc) {
            options.log_file = argv[++i];
        } else if ((arg == "--log-capacity" || arg == "--sample-period-ms") && i + 1 < argc) {
            std::string value=argv[++i];size_t used=0;auto n=std::stoul(value,&used);
            if(used!=value.size() || n<1 || n>(arg=="--log-capacity"?65534ul:60000ul))
                throw std::invalid_argument("invalid log option");
            if(arg=="--log-capacity")options.log_capacity=n;
            else options.sample_period_ms=static_cast<unsigned>(n);
        } else if (arg == "--config" && i + 1 < argc) {
            options.config_path=argv[++i];
            options.config = vehicle::Config::load(options.config_path);
        } else throw std::invalid_argument("unknown option or missing value: " + arg);
    }
    if (!std::isfinite(options.seconds) || options.seconds <= 0 || options.seconds > 86400)
        throw std::invalid_argument("duration must be greater than 0 and at most 86400 seconds");
    if(options.can_simulate&&!options.can_interface.empty())throw std::invalid_argument("choose one CAN source");
    if(options.allow_remote_set && options.config_path.empty())throw std::invalid_argument("remote writes require --config path");
    options.config.validate();
    return options;
}
std::string config_id(const vehicle::Config& c, const Options& o) {
    std::ostringstream text;
    text << std::setprecision(17) << c.circumference_m << ',' << c.pulses_per_rev << ','
         << c.overspeed_kmh << ',' << c.hysteresis_kmh << ',' << c.stale_ms << ','
         << o.sample_period_ms << ",stage2-demo-policy-v1," << o.can_simulate << "," << o.can_interface;
    if(o.network)text << ",tcp=" << o.network->address << ":" << o.network->port;
    auto bytes=text.str();
    return "v1-crc32:"+std::to_string(vehicle::crc32(
        reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()));
}
vehicle::LogRecord record(vehicle::RecordType type, const std::string& payload,
                          const std::string& boot, const std::string& config) {
    vehicle::LogRecord r;
    r.type=type; r.payload=payload;r.boot_id=boot;r.config_version=config;
    r.monotonic_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
        vehicle::Clock::now().time_since_epoch()).count();
    r.utc_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // No reliable time-sync service has been integrated: explicitly report unsynchronized.
    r.clock_synchronized=false;
    return r;
}
std::string sample_payload(const vehicle::Snapshot& s) {
    std::ostringstream text;text<<std::setprecision(17)<<"SIMULATION snapshot="<<s.sequence;
    auto add=[&](const std::string& key,const vehicle::Measurement& m) {
        text<<';'<<key<<"={value=";
        if(m.valid && std::isfinite(m.value))text<<m.value;else text<<"invalid";
        text<<",valid="<<m.valid<<",unit="<<m.unit<<",source="<<m.source<<",error="<<m.error
            <<",sample_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(m.sampled.time_since_epoch()).count()<<'}';
    };
    add("speed",s.speed);add("pitch",s.pitch);add("roll",s.roll);add("vibration",s.vibration);
    add("temperature",s.temperature);add("humidity",s.humidity);
    for(const auto& p:s.analog)add("analog."+p.first,p.second);
    for(const auto& p:s.digital)add("digital."+p.first,p.second);
    return text.str();
}
}

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        if (options.help) {
            std::cout << "vehicle-monitor --simulate [--duration seconds] [--config path]\n"
                         "  [--log-file NEW_OR_EXISTING_NOR --log-capacity 256 --sample-period-ms 1000]\n"
                         "  [--tcp IPv4:port] [--allow-remote-set --config path]\n"
                         "  [--can-simulate | --can-interface can0]\n"
                         "vehicle-monitor --version\n";
            return 0;
        }
        if (options.version) { std::cout << "vehicle-monitor 0.1.0\n"; return 0; }
        if (!options.simulation)
            throw std::runtime_error("hardware backend not commissioned; use --simulate explicitly");
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::shared_ptr<vehicle::FileNor> medium;
        std::unique_ptr<vehicle::CircularLog> persistent;
        std::unique_ptr<vehicle::LogService> log_service;
        std::string boot_id;
        std::ifstream boot_file("/proc/sys/kernel/random/boot_id");
        std::getline(boot_file,boot_id);
        if(boot_id.empty())throw std::runtime_error("cannot read Linux boot identifier");
        auto configuration=config_id(options.config,options);
        if(!options.log_file.empty()) {
            struct stat st{};
            bool create=lstat(options.log_file.c_str(),&st)!=0;
            medium=std::make_shared<vehicle::FileNor>(options.log_file,
                vehicle::StorageGeometry{4096,256,options.log_capacity+2},create);
            persistent=std::make_unique<vehicle::CircularLog>(medium,options.log_capacity,create);
            auto recovery=persistent->recovery_stats();
            std::cout<<"SIMULATION log recovered="<<recovery.valid<<" corrupt="<<recovery.corrupt
                     <<" incomplete="<<recovery.incomplete<<'\n';
            log_service=std::make_unique<vehicle::LogService>(*persistent);
        }
        vehicle::StateStore store, runtime_store;
        vehicle::ParameterStore parameters(options.config,options.allow_remote_set?options.config_path:"");
        vehicle::NodeManager simulated_nodes;
        std::unique_ptr<vehicle::CanService> can;
        if(!options.can_interface.empty())can=std::make_unique<vehicle::CanService>(options.can_interface);
        std::unique_ptr<vehicle::TcpClient> network;
        if(options.network)network=std::make_unique<vehicle::TcpClient>(*options.network,parameters,[&] {
            const auto current=parameters.snapshot().config;
            auto text="simulation=1\n"+vehicle::snapshot_text(store.snapshot(),vehicle::Clock::now(),
                std::chrono::milliseconds(current.stale_ms));
            text+="runtime\n"+vehicle::snapshot_text(runtime_store.snapshot(),vehicle::Clock::now(),std::chrono::milliseconds(500));
            if(can || options.can_simulate) {
                const auto nodes=can?can->nodes():simulated_nodes.snapshot();
                text+=can?"can_source=socketcan\n":"can_source=simulation\n";
                for(const auto& node:nodes)text+="can.node."+std::to_string(node.id)+
                    "=online:"+std::to_string(node.online)+",seen:"+std::to_string(node.seen)+
                    ",state:"+std::to_string(node.state)+",sequence:"+std::to_string(node.sequence)+"\n";
            }
            return text+"parameters\n"+vehicle::config_text(current);
        });
        vehicle::BoundedQueue<std::string> logs(128);
        vehicle::HealthMonitor health;
        health.add("acquisition", std::chrono::milliseconds(500), vehicle::Clock::now());
        std::atomic<bool> running{true};
        std::thread logger([&] {
            while (auto line = logs.pop()) std::cout << *line << std::endl;
        });
        std::thread acquisition;
        try {
            acquisition = std::thread([&] {
                try {
                    auto next = vehicle::Clock::now();
                    unsigned tick = 0;
                    vehicle::AttitudeEstimator attitude;
                    vehicle::AnalogChannel bus({0,100,1,0,0.25,"V","simulation.bus"});
                    vehicle::DigitalDebouncer liquid(true,std::chrono::milliseconds(40));
                    while (running) {
                        vehicle::Snapshot s;
                        const auto now = vehicle::Clock::now();
                        s.speed = {tick % 150 < 75 ? 60.0 : 90.0, true, now,
                                   "km/h", "simulation", 0};
                        constexpr double pi=3.14159265358979323846;
                        double tilt=(tick%150>=75 ? 20.0 : 0.0)*pi/180;
                        auto pose=attitude.update({{-std::sin(tilt),0,std::cos(tilt)}, {0,0,0},now,true});
                        s.pitch=pose.pitch; s.roll=pose.roll; s.vibration=pose.vibration;
                        s.pitch.source=s.roll.source=s.vibration.source="simulation.imu";
                        s.analog["bus"]=bus.update(tick%150<75?24:32,true,now);
                        s.digital["liquid"]=liquid.update(tick%150>=75,true,now);
                        s.digital["liquid"].source="simulation.digital";
                        ++tick;
                        store.publish(s);
                        health.progress("acquisition", now);
                        next += std::chrono::milliseconds(20);
                        std::this_thread::sleep_until(next);
                    }
                } catch (...) { running = false; }
            });
            vehicle::Diagnosis diagnosis;
            using namespace std::chrono_literals;
            diagnosis.add({"speed",options.config.overspeed_kmh,options.config.hysteresis_kmh,
                           false,false,vehicle::Severity::warning,100ms,200ms,
                           std::chrono::milliseconds(options.config.stale_ms)});
            diagnosis.add({"tilt",15,2,true,false,vehicle::Severity::fault,100ms,200ms,500ms});
            diagnosis.add({"bus",30,2,false,false,vehicle::Severity::warning,100ms,200ms,500ms});
            diagnosis.add({"liquid",0.5,0.1,false,false,vehicle::Severity::fault,100ms,200ms,500ms});
            if(can || options.can_simulate)for(const auto id:{1,2})
                diagnosis.add({"node."+std::to_string(id),0.5,0,false,true,vehicle::Severity::fault,0ms,0ms,500ms});
            if(network)diagnosis.add({"tcp",0.5,0,false,true,vehicle::Severity::warning,100ms,0ms,500ms});
            uint64_t applied_revision=0;
            uint16_t can_sequence=0;
            auto simulation_start=vehicle::Clock::now(),next_can=simulation_start;
            const auto end = vehicle::Clock::now() + std::chrono::duration<double>(options.seconds);
            bool failed = false;
            auto next_sample=vehicle::Clock::now();
            while (!stopping && vehicle::Clock::now() < end) {
                if (!running || (log_service && log_service->failed()) || !health.stalled(vehicle::Clock::now()).empty()) {
                    failed = true;
                    break;
                }
                const auto parameter_state=parameters.snapshot();
                if(parameter_state.revision!=applied_revision) {
                    const auto& c=parameter_state.config;
                    diagnosis.configure({"speed",c.overspeed_kmh,c.hysteresis_kmh,false,false,
                        vehicle::Severity::warning,100ms,200ms,std::chrono::milliseconds(c.stale_ms)});
                    configuration=config_id(c,options);applied_revision=parameter_state.revision;
                    if(log_service && !log_service->submit(record(vehicle::RecordType::diagnostic,
                        "SIMULATION config_changed revision="+std::to_string(applied_revision)+" "+vehicle::config_text(c),
                        boot_id,configuration),true))failed=true;
                }
                auto s = store.snapshot();
                if (s.sequence) {
                    std::map<std::string,vehicle::Measurement> inputs{
                        {"speed",s.speed},{"tilt",s.pitch},
                        {"bus",s.analog.at("bus")},{"liquid",s.digital.at("liquid")}};
                    const auto now=vehicle::Clock::now();
                    if(options.can_simulate) {
                        if(now>=next_can) {
                            simulated_nodes.receive(vehicle::heartbeat(1,can_sequence,0),now);
                            auto elapsed=now-simulation_start;
                            if(elapsed<3s || elapsed>=7s)simulated_nodes.receive(vehicle::heartbeat(2,can_sequence,0),now);
                            ++can_sequence;next_can=now+1s;
                        }
                        simulated_nodes.expire(now);
                    }
                    if(can || options.can_simulate) {
                        auto nodes=can?can->nodes():simulated_nodes.snapshot();
                        for(const auto& node:nodes)inputs["node."+std::to_string(node.id)]={
                            node.online?1.0:0.0,true,now,"bool",can?"socketcan":"simulation.can",0};
                    }
                    if(network)inputs["tcp"]={network->status().connected?1.0:0.0,true,now,"bool","tcp",0};
                    for(const auto& event:diagnosis.evaluate(inputs,now)) {
                        const std::string message="SIMULATION fault="+event.status.id+
                            " state="+std::to_string(static_cast<int>(event.status.state))+
                            " severity="+std::to_string(static_cast<int>(event.status.severity))+
                            " acknowledged="+std::to_string(event.status.acknowledged)+
                            " event="+std::to_string(event.sequence);
                        logs.push(message);
                        if(log_service && !log_service->submit(record(vehicle::RecordType::fault,
                            message,boot_id,configuration),true))failed=true;
                    }
                    if(log_service && now>=next_sample) {
                        log_service->submit(record(vehicle::RecordType::sample,
                            sample_payload(s),boot_id,configuration),false);
                        next_sample=now+std::chrono::milliseconds(options.sample_period_ms);
                    }
                    vehicle::Snapshot runtime;
                    for(const auto& p:inputs)if(p.first=="tcp"||p.first.find("node.")==0)runtime.digital[p.first]=p.second;
                    for(const auto& fault:diagnosis.statuses())runtime.digital["fault."+fault.id]={
                        static_cast<double>(fault.state),true,now,"state","diagnosis",0};
                    runtime_store.publish(std::move(runtime));
                    // Pure policy output: hardware GPIO is not accessed in simulation.
                    const auto alarm=vehicle::alarm_output(diagnosis.statuses(),now);
                    (void)alarm;
                }
                if(failed)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if(network) {
                auto status=network->status();
                std::cout<<"SIMULATION tcp_connections="<<status.connections<<" failures="<<status.failures
                         <<" sent="<<status.sent<<" received="<<status.received<<'\n';
            }
            network.reset();
            running = false;
            acquisition.join();
            logs.close();
            logger.join();
            std::cout << "SIMULATION stopped; log_dropped=" << logs.dropped() << '\n';
            if(log_service) {
                log_service->close();
                auto dropped=log_service->dropped();
                std::cout<<"SIMULATION storage_failed="<<log_service->failed()
                         <<" critical_dropped="<<dropped.first<<" samples_dropped="<<dropped.second<<'\n';
                failed=failed || log_service->failed() || dropped.first>0;
            }
            return failed || logs.dropped() ? 2 : 0;
        } catch (...) {
            running = false;
            if (acquisition.joinable()) acquisition.join();
            logs.close();
            logger.join();
            throw;
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

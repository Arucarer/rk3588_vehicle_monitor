#include "vehicle/gui.hpp"
#include "lvgl.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef VEHICLE_GUI_SDL
#include <SDL.h>
#endif
static_assert(LVGL_VERSION_MAJOR==8 && LVGL_VERSION_MINOR==2 && LVGL_VERSION_PATCH==0,"LVGL must be pinned to 8.2.0");
namespace vehicle {
namespace {
constexpr int width=240,height=320;
constexpr uint32_t background=0x0d1522,panel=0x182435,muted=0x97a9bf,cyan=0x53dbe8,white=0xf0f6ff,red=0xff7474,amber=0xffc56b;
const char* titles[]={"仪表","健康","节点","历史","设置"};
const char* parameter_keys[]={"overspeed_kmh","hysteresis_kmh","circumference_m","pulses_per_rev","stale_ms"};
const char* parameter_names[]={"超速 km/h","滞回 km/h","周长 m","脉冲/圈","有效期 ms"};
std::string number(double value){std::ostringstream s;s<<std::fixed<<std::setprecision(1)<<value;return s.str();}
std::string measurement(const Measurement& m,Clock::time_point now,std::chrono::milliseconds ttl) {
    if(!m.fresh(now,ttl))return "无效";
    if(m.unit=="g"){std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<m.value;return out.str()+" g";}
    return number(m.value)+" "+m.unit;
}
double parameter(const Config& c,int index) {
    switch(index){case 0:return c.overspeed_kmh;case 1:return c.hysteresis_kmh;case 2:return c.circumference_m;case 3:return c.pulses_per_rev;default:return c.stale_ms;}
}
std::string event_summary(const LogRecord& r) {
    auto start=r.payload.find("fault=");
    if(start==std::string::npos)return r.payload;
    start+=6;auto end=r.payload.find(' ',start);auto id=r.payload.substr(start,end-start);
    std::string name=id=="speed"?"超速":id=="tilt"?"倾斜":id=="bus"?"母线越限":id=="liquid"?"液位告警":id=="tcp"?"网络":id;
    if(id.find("node.")==0)name="节点 "+id.substr(5);
    auto pos=r.payload.find("state=");std::string status;
    if(pos!=std::string::npos && pos+6<r.payload.size()) {
        const char* states[]={"正常","预警","故障","恢复"};
        char code=r.payload[pos+6];if(code>='0'&&code<='3')status=states[code-'0'];
    }
    return name+" "+status;
}
std::string record_time(const LogRecord& r) {
    std::time_t time=r.utc_ms/1000;std::tm t{};gmtime_r(&time,&t);char buffer[32];
    std::strftime(buffer,sizeof(buffer),"%m-%d %H:%M:%S UTC",&t);return buffer;
}
}
struct GuiWindow::Impl {
    struct Binding{Impl* self;int action;};
    GuiCallbacks callbacks;
    GuiState state;
    Clock::time_point now{};
    unsigned current=0;
    size_t history_offset=0;
    int selected=0;
    double edited=80;
    bool dirty=false,pressed=false,headless;
    int mouse_x=0,mouse_y=0;
    lv_disp_draw_buf_t draw_buffer{};
    lv_disp_drv_t driver{};
    lv_indev_drv_t input_driver{};
    lv_disp_t* display=nullptr;
    lv_indev_t* input=nullptr;
    lv_obj_t *screen=nullptr,*status=nullptr,*content=nullptr,*detail=nullptr;
    lv_obj_t *speed_value=nullptr,*temperature=nullptr,*humidity=nullptr,*pitch=nullptr,*roll=nullptr;
    lv_obj_t *history_labels[2]{},*history_buttons[2]{},*history_info=nullptr;
    lv_obj_t *node_labels[2]{},*parameter_labels[5]{},*parameter_rows[5]{};
    lv_obj_t *navigation[5]{},*actions[3]{};
    std::vector<lv_obj_t*> health_labels;
    std::vector<std::string> health_keys;
    std::map<int,Binding> bindings;
    std::vector<LogRecord> history;
    std::vector<lv_color_t> drawing=std::vector<lv_color_t>(width*32);
    std::vector<uint32_t> pixels=std::vector<uint32_t>(width*height,0xff000000);
#ifdef VEHICLE_GUI_SDL
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    SDL_Texture* texture=nullptr;
#endif
    Impl(bool hidden,GuiCallbacks cb):callbacks(std::move(cb)),headless(hidden) {
        if(!headless) {
#ifdef VEHICLE_GUI_SDL
            if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
            window=SDL_CreateWindow("RK3588 vehicle monitor / SIMULATION",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width*2,height*2,0);
            if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
            if(renderer)texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);
            if(!texture){cleanup_sdl();throw std::runtime_error("SDL GUI initialization failed");}
#else
            throw std::runtime_error("SDL not compiled: use --gui-headless");
#endif
        }
        lv_init();
        lv_disp_draw_buf_init(&draw_buffer,drawing.data(),nullptr,drawing.size());
        lv_disp_drv_init(&driver);driver.hor_res=width;driver.ver_res=height;
        driver.draw_buf=&draw_buffer;driver.flush_cb=flush;driver.user_data=this;
        display=lv_disp_drv_register(&driver);
        lv_indev_drv_init(&input_driver);input_driver.type=LV_INDEV_TYPE_POINTER;
        input_driver.read_cb=read_input;input_driver.user_data=this;input=lv_indev_drv_register(&input_driver);
        screen=lv_scr_act();lv_obj_remove_style_all(screen);
        lv_obj_set_style_bg_color(screen,lv_color_hex(background),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
        lv_obj_set_style_text_font(screen,&vehicle_ui_16,0);lv_obj_set_style_text_color(screen,lv_color_hex(white),0);
        label(screen,8,4,154,26,"车载监测",cyan);
        auto* tag=label(screen,164,7,68,22,"SIM",amber);lv_obj_set_style_text_align(tag,LV_TEXT_ALIGN_RIGHT,0);
        status=label(screen,8,31,224,23,"模拟 | 网络未启用",muted);
        content=lv_obj_create(screen);base(content,8,58,224,188,background);
        for(int i=0;i<5;++i)navigation[i]=button(screen,4+i*47,286,45,32,titles[i],100+i);
        actions[0]=button(screen,8,250,64,28,"上一页",201);
        actions[1]=button(screen,78,250,84,28,"确认告警",202);
        actions[2]=button(screen,168,250,64,28,"下一页",203);
        build();
    }
    ~Impl(){lv_indev_delete(input);lv_disp_remove(display);lv_deinit();cleanup_sdl();}
    void cleanup_sdl(){
#ifdef VEHICLE_GUI_SDL
        if(texture)SDL_DestroyTexture(texture);
        if(renderer)SDL_DestroyRenderer(renderer);
        if(window)SDL_DestroyWindow(window);
        if(!headless)SDL_Quit();
#endif
    }
    static void flush(lv_disp_drv_t* d,const lv_area_t* area,lv_color_t* source) {
        auto& self=*static_cast<Impl*>(d->user_data);
        for(int y=area->y1;y<=area->y2;++y)for(int x=area->x1;x<=area->x2;++x) {
            if(x>=0&&x<width&&y>=0&&y<height)self.pixels[y*width+x]=0xff000000u|lv_color_to32(*source);
            ++source;
        }
        lv_disp_flush_ready(d);
    }
    static void read_input(lv_indev_drv_t* d,lv_indev_data_t* data) {
        auto& self=*static_cast<Impl*>(d->user_data);
        data->point={static_cast<lv_coord_t>(self.mouse_x),static_cast<lv_coord_t>(self.mouse_y)};
        data->state=self.pressed?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
    }
    void base(lv_obj_t* object,int x,int y,int w,int h,uint32_t color) {
        lv_obj_remove_style_all(object);lv_obj_set_pos(object,x,y);lv_obj_set_size(object,w,h);
        lv_obj_set_style_bg_color(object,lv_color_hex(color),0);lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
        lv_obj_set_style_radius(object,8,0);lv_obj_set_style_pad_all(object,0,0);
        lv_obj_clear_flag(object,LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t* label(lv_obj_t* parent,int x,int y,int w,int h,const std::string& text,uint32_t color=white) {
        auto* object=lv_label_create(parent);lv_obj_set_pos(object,x,y);lv_obj_set_size(object,w,h);
        lv_label_set_long_mode(object,LV_LABEL_LONG_WRAP);lv_label_set_text(object,text.c_str());
        lv_obj_set_style_text_color(object,lv_color_hex(color),0);return object;
    }
    lv_obj_t* button(lv_obj_t* parent,int x,int y,int w,int h,const std::string& text,int action) {
        auto* object=lv_btn_create(parent);base(object,x,y,w,h,panel);
        lv_obj_set_style_bg_color(object,lv_color_hex(0x294359),LV_STATE_PRESSED);
        auto binding=bindings.emplace(action,Binding{this,action});
        lv_obj_add_event_cb(object,event,LV_EVENT_CLICKED,&binding.first->second);
        if(!text.empty()){auto* caption=label(object,0,0,w,h,text);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(caption);}
        return object;
    }
    static void event(lv_event_t* e) {
        auto& binding=*static_cast<Binding*>(lv_event_get_user_data(e));
        try{binding.self->action(binding.action);}catch(...){lv_label_set_text(binding.self->status,"操作失败");}
    }
    lv_obj_t* card(int x,int y,int w,int h,const std::string& title) {
        auto* box=lv_obj_create(content);base(box,x,y,w,h,panel);label(box,8,3,w-16,22,title,muted);return box;
    }
    void build() {
        if(detail){lv_obj_del(detail);detail=nullptr;}
        lv_obj_clean(content);
        lv_obj_clear_flag(content,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_scroll_to_y(content,0,LV_ANIM_OFF);
        // Screen/nav bindings survive; content bindings are removed only with their objects.

        health_labels.clear();health_keys.clear();
        for(int i=0;i<5;++i)lv_obj_set_style_bg_color(navigation[i],lv_color_hex(current==unsigned(i)?0x235767:panel),0);
        if(current==0) {
            auto* speed=card(0,0,224,72,"车速");speed_value=label(speed,10,29,148,38,"--",cyan);
            lv_obj_set_style_text_font(speed_value,&lv_font_montserrat_28,0);label(speed,165,36,52,26,"km/h",muted);
            temperature=label(card(0,78,108,52,"温度"),8,26,96,24,"无效");
            humidity=label(card(116,78,108,52,"湿度"),8,26,96,24,"无效");
            pitch=label(card(0,136,108,52,"俯仰"),8,26,96,24,"无效");
            roll=label(card(116,136,108,52,"横滚"),8,26,96,24,"无效");
        } else if(current==1) {
            lv_obj_add_flag(content,LV_OBJ_FLAG_SCROLLABLE);
        } else if(current==2) {
            node_labels[0]=label(card(0,0,224,88,"CAN 节点 1"),8,27,208,56,"");
            node_labels[1]=label(card(0,96,224,88,"CAN 节点 2"),8,27,208,56,"");
        } else if(current==3) {
            for(int i=0;i<2;++i) {
                history_buttons[i]=button(content,0,i*80,224,74,"",300+i);
                history_labels[i]=label(history_buttons[i],8,2,208,70,"暂无记录");
            }
            history_info=label(content,0,164,224,23,"",muted);
        } else {
            for(int i=0;i<5;++i) {
                parameter_rows[i]=button(content,0,i*36,224,32,"",400+i);
                label(parameter_rows[i],6,4,112,24,parameter_names[i],muted);
                parameter_labels[i]=label(parameter_rows[i],120,4,98,24,"");
            }
        }
        const char* names[3]={current==4?"-":"上一页",current==4?"保存":"确认告警",current==4?"+":"下一页"};
        for(int i=0;i<3;++i)lv_label_set_text(lv_obj_get_child(actions[i],0),names[i]);
    }
    void action(int code) {
        if(code>=100&&code<105){current=code-100;dirty=false;edited=parameter(state.config,selected);build();refresh();return;}
        if(code>=400&&code<405){selected=code-400;dirty=false;edited=parameter(state.config,selected);refresh();return;}
        if(code==500){if(detail)lv_obj_del(detail);detail=nullptr;return;}
        if(code>=300&&code<302) {
            size_t index=history_offset+code-300;if(index>=history.size())return;
            if(detail)lv_obj_del(detail);
            detail=lv_obj_create(screen);base(detail,4,56,232,224,panel);
            button(detail,168,0,60,28,"返回",500);
            auto* scroll=lv_obj_create(detail);base(scroll,4,32,224,188,panel);lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);
            const auto& r=history[index];
            std::string text="#"+std::to_string(r.sequence)+" "+record_time(r)+"\n"+
                (r.clock_synchronized?"时间同步":"时间未同步")+"\n"+r.payload+"\n"+
                r.software_version+"\n"+r.config_version;
            auto* body=label(scroll,4,0,212,LV_SIZE_CONTENT,text);lv_obj_set_height(body,LV_SIZE_CONTENT);return;
        }
        if(current==4) {
            if(code==202) {
                if(!state.settings_writable){lv_label_set_text(status,"配置只读");return;}
                std::ostringstream value;value<<std::setprecision(12)<<edited;
                bool ok=callbacks.set_parameter && callbacks.set_parameter(std::string(parameter_keys[selected])+"="+value.str());
                lv_label_set_text(status,ok?"保存处理中":"队列忙");
            } else {
                double step=selected==2?0.1:selected==4?100:1;
                const double minima[]={1,0,0.1,1,100},maxima[]={300,50,10,10000,60000};
                edited=std::clamp(edited+(code==201?-step:step),minima[selected],maxima[selected]);dirty=true;refresh();
            }
        } else if(code==202) {
            bool ok=callbacks.acknowledge&&callbacks.acknowledge();lv_label_set_text(status,ok?"确认处理中":"队列忙");
        } else if(current==3) {
            if(code==201)history_offset=history_offset>=2?history_offset-2:0;
            else if(history_offset+2<history.size())history_offset+=2;
            refresh();
        } else {current=(current+(code==201?4:1))%5;build();refresh();}
    }
    void refresh() {
        auto ttl=std::chrono::milliseconds(state.config.stale_ms);
        if(current==0) {
            bool valid=state.sensors.speed.fresh(now,ttl);
            lv_obj_set_style_text_font(speed_value,valid?&lv_font_montserrat_28:&vehicle_ui_16,0);
            lv_label_set_text(speed_value,valid?number(state.sensors.speed.value).c_str():"无效");
            lv_label_set_text(temperature,measurement(state.sensors.temperature,now,std::chrono::milliseconds(5000)).c_str());
            lv_label_set_text(humidity,measurement(state.sensors.humidity,now,std::chrono::milliseconds(5000)).c_str());
            lv_label_set_text(pitch,measurement(state.sensors.pitch,now,ttl).c_str());
            lv_label_set_text(roll,measurement(state.sensors.roll,now,ttl).c_str());
        } else if(current==1) {
            std::vector<std::pair<std::string,Measurement>> values{{"震动",state.sensors.vibration}};
            for(const auto& p:state.sensors.analog)values.push_back({p.first=="bus"?"母线电压 bus":p.first,p.second});
            for(const auto& p:state.sensors.digital)values.push_back({p.first=="liquid"?"液位开关 liquid":p.first,p.second});
            std::vector<std::string> keys;for(const auto& p:values)keys.push_back(p.first);
            if(keys!=health_keys) {
                lv_obj_clean(content);health_labels.clear();health_keys=keys;
                for(size_t i=0;i<values.size();++i) {
                    auto* row=lv_obj_create(content);base(row,0,i*55,224,49,panel);
                    auto* text=label(row,8,1,208,48,"");health_labels.push_back(text);
                }
            }
            for(size_t i=0;i<values.size();++i) {
                auto text=values[i].first+"\n"+measurement(values[i].second,now,ttl);
                lv_label_set_text(health_labels[i],text.c_str());
            }
        } else if(current==2) {
            for(int i=0;i<2;++i) {
                auto found=std::find_if(state.nodes.begin(),state.nodes.end(),[&](const auto& n){return n.id==i+1;});
                std::string text="总线未启用";
                bool online=found!=state.nodes.end()&&found->online&&now>=found->last_seen&&now-found->last_seen<std::chrono::seconds(3);
                if(state.can_enabled)text=found==state.nodes.end()?"离线":std::string(online?"在线":"离线")+
                    "  状态 "+std::to_string(found->state)+"\n"+"序列 "+std::to_string(found->sequence);
                lv_label_set_text(node_labels[i],text.c_str());
                lv_obj_set_style_text_color(node_labels[i],lv_color_hex(online?cyan:amber),0);
            }
        } else if(current==3) {
            history.clear();for(auto it=state.history.rbegin();it!=state.history.rend();++it)if(it->type!=RecordType::sample)history.push_back(*it);
            if(history_offset>=history.size())history_offset=0;
            for(int i=0;i<2;++i) {
                size_t index=history_offset+i;std::string text="暂无记录";
                if(index<history.size()) {
                    const auto& r=history[index];text="#"+std::to_string(r.sequence)+" "+record_time(r)+"\n"+event_summary(r);
                }
                lv_label_set_long_mode(history_labels[i],LV_LABEL_LONG_DOT);lv_label_set_text(history_labels[i],text.c_str());
            }
            std::string text=(state.persistent_history?"持久化":"内存")+std::string(" ")+
                std::to_string(history.empty()?0:history_offset/2+1)+"/"+std::to_string((history.size()+1)/2);
            lv_label_set_text(history_info,text.c_str());
        } else {
            if(dirty&&std::abs(edited-parameter(state.config,selected))<1e-9)dirty=false;
            if(!dirty)edited=parameter(state.config,selected);
            for(int i=0;i<5;++i) {
                auto value=number(i==selected?edited:parameter(state.config,i));
                lv_label_set_text(parameter_labels[i],value.c_str());
                lv_obj_set_style_bg_color(parameter_rows[i],lv_color_hex(i==selected?0x235767:panel),0);
            }
        }
    }
    bool tick(uint32_t elapsed) {
#ifdef VEHICLE_GUI_SDL
        if(!headless) {
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                if(event.type==SDL_QUIT)return false;
                if(event.type==SDL_MOUSEMOTION){mouse_x=event.motion.x/2;mouse_y=event.motion.y/2;}
                if(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP){mouse_x=event.button.x/2;mouse_y=event.button.y/2;pressed=event.type==SDL_MOUSEBUTTONDOWN;}
                if(event.type==SDL_MOUSEWHEEL)lv_obj_scroll_by(content,0,event.wheel.y*40,LV_ANIM_OFF);
                if(event.type==SDL_KEYDOWN) {
                    if(event.key.keysym.sym==SDLK_ESCAPE)return false;
                    if(event.key.keysym.sym==SDLK_LEFT)action(100+(current+4)%5);
                    if(event.key.keysym.sym==SDLK_RIGHT)action(100+(current+1)%5);
                }
            }
        }
#endif
        lv_tick_inc(elapsed);lv_timer_handler();
#ifdef VEHICLE_GUI_SDL
        if(!headless){SDL_UpdateTexture(texture,nullptr,pixels.data(),width*4);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);SDL_RenderPresent(renderer);}
#endif
        return true;
    }
};
GuiWindow::GuiWindow(bool headless,GuiCallbacks cb):impl_(std::make_unique<Impl>(headless,std::move(cb))){}
GuiWindow::~GuiWindow()=default;
void GuiWindow::update(const GuiState& state,Clock::time_point now,const std::string& feedback) {
    impl_->state=state;impl_->now=now;
    size_t active=0,unacknowledged=0;for(const auto& f:state.faults)if(f.state==FaultState::warning||f.state==FaultState::fault){++active;if(!f.acknowledged)++unacknowledged;}
    std::string text=feedback.empty()?std::string("模拟 | ")+(state.network_enabled?(state.network.connected?"网络在线":"网络离线"):"网络未启用")+" "+(active?(unacknowledged?"告警 ":"已确认 "):"正常 ")+std::to_string(active):feedback;
    if(feedback.empty()) {
        if(state.updated==Clock::time_point{})text="模拟 | 等待数据";
        else if(now<state.updated||now-state.updated>std::chrono::milliseconds(500))text="模拟 | 数据过期";
        else if(impl_->current==4&&!state.settings_writable)text="模拟 | 配置只读";
    }
    lv_label_set_text(impl_->status,text.c_str());
    lv_obj_set_style_text_color(impl_->status,lv_color_hex(active?amber:muted),0);impl_->refresh();
}
bool GuiWindow::step(uint32_t elapsed){return impl_->tick(elapsed);}
void GuiWindow::navigate(unsigned page){if(page>=5)throw std::invalid_argument("invalid GUI page");impl_->action(100+page);}
void GuiWindow::click(int x,int y) {
    impl_->mouse_x=x;impl_->mouse_y=y;impl_->pressed=true;impl_->tick(40);impl_->pressed=false;impl_->tick(40);
}
void GuiWindow::save_ppm(const std::string& path)const {
    std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot create GUI capture");
    f<<"P6\n240 320\n255\n";
    for(auto pixel:impl_->pixels){char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};f.write(rgb,3);}
    if(!f)throw std::runtime_error("GUI capture write failed");
}
unsigned GuiWindow::page()const{return impl_->current;}
std::vector<std::string> GuiWindow::visible_text()const {
    std::vector<std::string> text;
    std::function<void(lv_obj_t*)> visit=[&](lv_obj_t* obj){
        if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;
        if(lv_obj_check_type(obj,&lv_label_class))text.push_back(lv_label_get_text(obj));
        for(uint32_t i=0;i<lv_obj_get_child_cnt(obj);++i)visit(lv_obj_get_child(obj,i));
    };visit(impl_->screen);return text;
}
bool GuiWindow::layout_valid()const {
    lv_obj_update_layout(impl_->screen);
    // Content can scroll; verify its viewport and all navigation/action controls.
    for(auto* object:{impl_->content,impl_->actions[0],impl_->actions[1],impl_->actions[2],impl_->navigation[0],impl_->navigation[4]}) {
        lv_area_t a;lv_obj_get_coords(object,&a);
        if(a.x1<0||a.y1<0||a.x2>=width||a.y2>=height)return false;
    }
    return true;
}
}

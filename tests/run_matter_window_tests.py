"""Test the production commissioning control against a queued CHIP server stub."""
from pathlib import Path
import argparse, subprocess
p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
s=(Path(__file__).resolve().parents[1]/'components/matter/matter_component.cpp').read_text(encoding='utf-8')
config=(Path(__file__).resolve().parents[1]/'components/matter/__init__.py').read_text(encoding='utf-8')
assert 'add_idf_sdkconfig_option("CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART", False)' in config

def extract(name):
 start=s.index('void MatterComponent::'+name+'(');opening=s.index('{',start);end=opening+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
pre=r'''
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <iostream>
#define USE_TEXT_SENSOR
#define ESP_LOGI(...)
#define ESP_LOGE(...)
using CHIP_ERROR=int;constexpr int CHIP_NO_ERROR=0;
bool open=false,fail=false,fail_open=false,fail_clear=false,pending_add=false;int clears=0,fabric_sensor_updates=0;int fabrics=0,opens=0,seconds=0;
std::vector<std::function<void()>>queue,main_queue;
namespace chip {
constexpr uint8_t kUndefinedFabricIndex=0;
namespace System::Clock {struct Seconds32 {int value;explicit Seconds32(int v):value(v){}};}
enum class CommissioningWindowAdvertisement{kDnssdOnly};
struct Window {void CloseCommissioningWindow(){open=false;}bool IsCommissioningWindowOpen(){return open;}
int OpenBasicCommissioningWindow(System::Clock::Seconds32 t,CommissioningWindowAdvertisement){opens++;seconds=t.value;if(fail_open)return 1;open=true;return 0;}};
struct Fabrics {void DeleteAllFabrics(){clears++;if(!fail_clear)fabrics=0;}int FabricCount()const{return fabrics;}uint8_t GetPendingNewFabricIndex()const{return pending_add?1:kUndefinedFabricIndex;}};
struct Server {static Server&GetInstance(){static Server s;return s;}Window w;Fabrics f;
Window&GetCommissioningWindowManager(){return w;}Fabrics&GetFabricTable(){return f;}};
namespace DeviceLayer {struct Platform {void LockChipStack(){}void UnlockChipStack(){}
int ScheduleWork(void(*f)(intptr_t),intptr_t a){if(fail)return 1;queue.push_back([=](){f(a);});return 0;}};
Platform&PlatformMgr(){static Platform p;return p;}}
}
struct Sensor {std::string state;int updates=0;void publish_state(std::string s){state=s;updates++;}};
struct MatterComponent {Sensor *commissioning_status_sensor_;bool commissioning_window_open_=false;bool manual_window_active_=false;uint8_t manual_window_fabric_count_=0;bool failed=false;bool is_failed(){return failed;}
void defer_to_main_loop(std::function<void()>f){main_queue.push_back(f);}
void schedule_fabric_sensor_update(){fabric_sensor_updates++;}
void clear_fabrics();void refresh_commissioning_status();void open_commissioning_window();void maintain_commissioning_window();};
void drain(){auto q=std::move(queue);queue.clear();for(auto f:q)f();auto m=std::move(main_queue);main_queue.clear();for(auto f:m)f();}
'''
test=r'''
int main(){Sensor sensor;MatterComponent c{&sensor};c.refresh_commissioning_status();assert(sensor.state.find("準備中")!=std::string::npos);
c.maintain_commissioning_window();assert(!open);drain();assert(open&&seconds==900&&opens==1);
c.maintain_commissioning_window();drain();assert(opens==1); // Keep an active session intact.
pending_add=true;fabrics=1;c.refresh_commissioning_status();assert(sensor.state.find("配對紀錄 0 組")!=std::string::npos&&sensor.state.find("尚未完成 Matter 配對")!=std::string::npos);
// A failed first attempt rolls back; QR-visible state remains uncommissioned.
pending_add=false;fabrics=0;c.refresh_commissioning_status();assert(sensor.state.find("配對紀錄 0 組")!=std::string::npos);
open=false;c.maintain_commissioning_window();drain();assert(open&&opens==2); // Renew expired window.
fabrics=1;open=false;c.maintain_commissioning_window();drain();assert(!open&&opens==2&&sensor.state.find("配對紀錄 1 組")!=std::string::npos&&sensor.state.find("已完成 Matter 配對")!=std::string::npos);
fabrics=0;c.maintain_commissioning_window();fabrics=1;drain();assert(!open&&opens==2); // Join raced with queue.
c.open_commissioning_window();drain();assert(open&&opens==3&&c.manual_window_active_&&sensor.state.find("手動15分鐘")!=std::string::npos); // Explicit new-family window.
c.maintain_commissioning_window();drain();assert(open&&opens==3);
// AddNOC creates a pending fabric before CommissioningComplete. If the
// attempt then fails, the SDK rolls it back and resumes the SAME window.
// Neither transition may hide the manual QR or require a 15-minute wait.
pending_add=true;fabrics=2;c.refresh_commissioning_status();assert(open&&c.manual_window_active_&&sensor.state.find("手動15分鐘")!=std::string::npos&&sensor.state.find("配對紀錄 1 組")!=std::string::npos);
pending_add=false;fabrics=1;c.refresh_commissioning_status();assert(open&&c.manual_window_active_&&sensor.state.find("手動15分鐘")!=std::string::npos);
const int retry_opens=opens;c.open_commissioning_window();drain();assert(opens==retry_opens&&c.manual_window_active_);
fabrics=2;open=false;c.refresh_commissioning_status();assert(!c.manual_window_active_&&sensor.state.find("配對紀錄 2 組")!=std::string::npos&&sensor.state.find("個家庭")==std::string::npos);
c.maintain_commissioning_window();drain();assert(!open);
c.open_commissioning_window();drain();assert(open&&c.manual_window_active_);
open=false;c.refresh_commissioning_status();assert(!c.manual_window_active_&&sensor.state.find("手動15分鐘")==std::string::npos);
const int expired_opens=opens;c.maintain_commissioning_window();drain();assert(!open&&opens==expired_opens); // Manual expiry must not renew for joined devices.
open=false;fail=true;c.maintain_commissioning_window();assert(queue.empty());
fail=false;fabrics=0;fail_open=true;c.maintain_commissioning_window();drain();assert(!open&&queue.empty());
fail_open=false;c.failed=true;c.maintain_commissioning_window();assert(queue.empty());
c.failed=false;fabrics=3;open=true;c.clear_fabrics();assert(fabrics==3);drain();assert(fabrics==0&&!open&&clears==1);drain();assert(open&&sensor.state.find("配對紀錄 0 組")!=std::string::npos&&sensor.state.find("尚未完成 Matter 配對")!=std::string::npos);
fabrics=2;fail_clear=true;c.clear_fabrics();drain();drain();assert(fabrics==2&&!open&&sensor.state.find("配對紀錄 2 組")!=std::string::npos);
fail_clear=false;fail=true;c.clear_fabrics();assert(queue.empty()&&clears==2);
fail=false;c.failed=true;c.clear_fabrics();assert(queue.empty()&&clears==2);
std::cout<<"PASS Matter: uncommissioned auto-open/renew, joined/race no reopening, active session retained, clear-all reopens, error retry bounded\n";}

'''
(b/'main.cpp').write_text(pre+extract('refresh_commissioning_status')+extract('open_commissioning_window')+extract('clear_fabrics')+extract('maintain_commissioning_window')+test,encoding='utf-8')
exe=b/'window.exe';subprocess.run([a.cxx,'c++','-std=c++17',str(b/'main.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

"""Exercise production FanControl bridge with a queued SDK boundary stub."""
from pathlib import Path
from types import SimpleNamespace
import argparse, ast, subprocess
p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]
s=(root/'components/matter/matter_attributes.cpp').read_text(encoding='utf-8')
# Exercise the actual endpoint generator without loading the complete ESPHome
# code-generation environment. The removed optional feature must be absent from
# creation as well as reporting, while three speeds and Auto remain available.
endpoint_ast=ast.parse((root/'components/matter/endpoints.py').read_text(encoding='utf-8'))
generator=next(node for node in ast.walk(endpoint_ast) if isinstance(node,ast.FunctionDef) and node.name=='_make_ac_fan_lines')
namespace={'Cluster':object,'cv':SimpleNamespace(Invalid=ValueError)}
exec(compile(ast.Module(body=[generator],type_ignores=[]),'endpoints.py','exec'),namespace)
config=SimpleNamespace(enabled_features={'MultiSpeed':True,'Auto':True})
owner=SimpleNamespace(_cluster_configs={'FanControl':config})
lines=namespace['_make_ac_fan_lines'](owner,SimpleNamespace(name='FanControl'))
emitted='\n'.join(lines)
assert 'speed_config.speed_max = 3;' in emitted
assert 'feature::fan_auto::add(fan_cluster)' in emitted
assert 'rock' not in emitted.lower()
config.enabled_features['Rocking']=True
try:
 namespace['_make_ac_fan_lines'](owner,SimpleNamespace(name='FanControl'))
except ValueError:
 pass
else:
 raise AssertionError('removed optional fan feature was accepted')
print('PASS AC endpoint generation: three speeds and Auto; removed optional fan feature rejected')
def function(name):
 pos=s.index(name+'(');start=s.rfind('\n',0,pos)+1
 if name=='endpoint_attribute_update_cb':start=s.rfind('\n',0,start-1)+1
 opening=s.index('{',pos);end=opening+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
pre=r"""
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>
#define ESP_LOGE(...)
#define ESP_LOGW(...)
#define ESP_LOGI(...)
#define ESP_LOGV(...)
using esp_err_t=int;constexpr int ESP_OK=0;
struct CHIP_ERROR{int value=0;bool operator!=(CHIP_ERROR o)const{return value!=o.value;}const char*AsString()const{return "stub";}};
constexpr CHIP_ERROR CHIP_NO_ERROR{};
struct esp_matter_attr_val_t{uint8_t value;};
esp_matter_attr_val_t esp_matter_enum8(uint8_t v){return {v};}
std::string format_attribute_value(const esp_matter_attr_val_t&){return "stub";}
namespace esp_matter::attribute {enum callback_type_t{PRE_UPDATE,POST_UPDATE};}
esp_err_t endpoint_attribute_update_cb(esp_matter::attribute::callback_type_t,uint16_t,uint32_t,uint32_t,esp_matter_attr_val_t*,void*);
int callbacks=0;uint32_t last_attr=999;uint8_t last_value=255;bool missing=false,fail_schedule=false;
void echo(uint32_t attr,uint8_t v){auto val=esp_matter_enum8(v);endpoint_attribute_update_cb(esp_matter::attribute::POST_UPDATE,1,0x202,attr,&val,nullptr);}
namespace chip {
namespace Protocols::InteractionModel {enum class Status{Success,Failure};}
namespace app::Clusters {
namespace FanControl {enum class FanModeEnum:uint8_t{Off,Low,Medium,High,On,Auto,Smart};}
struct FanControlCluster {
 uint8_t mode=0,speed=0,percent=0;int reports=0;
 FanControl::FanModeEnum GetFanMode(){return static_cast<FanControl::FanModeEnum>(mode);}
 auto SetFanMode(FanControl::FanModeEnum value){mode=uint8_t(value);reports++;echo(0,mode);echo(5,mode);return Protocols::InteractionModel::Status::Success;}
 void SetSpeedCurrent(uint8_t v){speed=v;echo(6,v);}
 void SetPercentCurrent(uint8_t v){percent=v;echo(3,v);}
};
}
namespace DeviceLayer {
std::vector<std::pair<void(*)(intptr_t),intptr_t>>queue;
struct Platform{CHIP_ERROR ScheduleWork(void(*f)(intptr_t),intptr_t a){if(fail_schedule)return {1};queue.push_back({f,a});return {};}};
Platform&PlatformMgr(){static Platform p;return p;}
}
}
chip::app::Clusters::FanControlCluster fan;
namespace esp_matter::data_model::provider {
struct Path{uint16_t endpoint;uint32_t cluster;};
struct Registry{void*Get(Path p){return !missing&&p.endpoint==1&&p.cluster==0x202?&fan:nullptr;}};
struct Provider{Registry&registry(){static Registry r;return r;}};
Provider&get_instance(){static Provider p;return p;}
}
struct MatterComponent {
 void report_ac_fan(uint16_t,uint8_t);
 void dispatch_attribute_update(uint16_t,uint32_t,uint32_t attr,const esp_matter_attr_val_t&v){callbacks++;last_attr=attr;last_value=v.value;}
};
MatterComponent component;MatterComponent*global_matter_component=&component;
void drain(){auto q=std::move(chip::DeviceLayer::queue);chip::DeviceLayer::queue.clear();for(auto [f,a]:q)f(a);}
"""
body=s[s.index('unsigned local_report_depth'):s.index('std::string format_attribute_value')]
test=r"""
int main(){
 component.report_ac_fan(1,3);assert(fan.reports==0);drain();assert(fan.mode==3&&fan.speed==3&&fan.percent==100&&callbacks==0);
 for(int i=0;i<240;i++){component.report_ac_fan(1,5);drain();}
 assert(fan.mode==5&&fan.speed==0&&fan.percent==0&&callbacks==0&&local_report_depth==0);
 for(uint8_t mode=0;mode<=3;mode++){component.report_ac_fan(1,mode);drain();assert(fan.mode==mode&&fan.speed==mode&&callbacks==0);}
 for(auto attr:{0,2,5}){fan.mode=2;echo(attr,99);assert(last_attr==0&&last_value==2);}
 assert(callbacks==3);
 fan.mode=5;echo(2,0);assert(callbacks==4&&last_value==5); // null Auto is normalized via server mode
 missing=true;int n=fan.reports;component.report_ac_fan(1,1);drain();assert(fan.reports==n&&callbacks==4);
 missing=false;fail_schedule=true;component.report_ac_fan(1,1);assert(chip::DeviceLayer::queue.empty());
 std::cout<<"PASS FanControl: queued reports and derived echoes suppressed; off/low/medium/high/auto state; mode/speed/percent normalization; missing endpoint and scheduling failure\n";
}
"""
(b/'fan.cpp').write_text(pre+body+function('MatterComponent::report_ac_fan')+function('endpoint_attribute_update_cb')+test,encoding='utf-8')
exe=b/'fan.exe';subprocess.run([a.cxx,'c++','-std=c++17',str(b/'fan.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

"""Execute production Matter attribute writers/callback against a queued Matter API stub."""
from pathlib import Path
import argparse, re, subprocess
p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);p.add_argument('--build-dir',type=Path,required=True)
p.add_argument('--source',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parents[1];b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
s=(a.source or root/'components/matter/matter_attributes.cpp').read_text(encoding='utf-8')
guard=s[s.index('unsigned local_report_depth'):s.index('chip::app::Clusters::FanControlCluster *get_ac_fan_server') if 'get_ac_fan_server' in s else s.index('std::string format_attribute_value')] if 'unsigned local_report_depth' in s else 'unsigned local_report_depth = 0;\n'
def function(name):
 pos=s.index(name+'(');start=s.rfind('\n',0,pos)+1
 if name=='endpoint_attribute_update_cb':start=s.rfind('\n',0,start-1)+1
 opening=s.index('{',pos);end=opening+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
types='INVALID BOOLEAN FLOAT INT8 INT16 INT32 INT64 UINT8 ENUM8 BITMAP8 UINT16 ENUM16 BITMAP16 UINT32 BITMAP32 UINT64 CHAR_STRING LONG_CHAR_STRING NULLABLE_INT16'.split()
pre=r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <utility>
#include <iostream>
#include <cmath>
#include <limits>
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
using esp_err_t=int;constexpr int ESP_OK=0;
struct CHIP_ERROR {int value=0;bool operator!=(CHIP_ERROR b)const{return value!=b.value;}const char*AsString()const{return "stub";}};
constexpr CHIP_ERROR CHIP_NO_ERROR{};
using MatterAttributeValue=std::variant<bool,float,int64_t,uint64_t,std::string>;
ENUMS
struct esp_matter_attr_val_t {
 esp_matter_val_type_t type;
 struct Value {bool b;float f;int8_t i8;int16_t i16;int32_t i32;int64_t i64;
 uint8_t u8;uint16_t u16;uint32_t u32;uint64_t u64;
 struct {uint8_t*b;size_t s,t,max;}a;}val{};
 auto get_base_type()const{return type==ESP_MATTER_VAL_TYPE_NULLABLE_INT16?ESP_MATTER_VAL_TYPE_INT16:type;}
 bool is_null()const{return type==ESP_MATTER_VAL_TYPE_NULLABLE_INT16&&val.i16==std::numeric_limits<int16_t>::min();}
};
bool dispatch_ac_fan_update(uint16_t,uint32_t,uint32_t){return false;}
int callbacks=0,reports=0;bool fail_schedule=false,fail_report=false,echo_report=true;
esp_matter_val_type_t expected_type=ESP_MATTER_VAL_TYPE_BOOLEAN;
esp_matter_attr_val_t last{};
namespace esp_matter::attribute {
 enum callback_type_t {PRE_UPDATE,POST_UPDATE};
 auto get_val_type(uint16_t,uint32_t,uint32_t){return expected_type;}
 esp_err_t report(uint16_t,uint32_t,uint32_t,esp_matter_attr_val_t*);
}
struct MatterComponent {void dispatch_attribute_update(uint16_t,uint32_t,uint32_t,const esp_matter_attr_val_t&){callbacks++;}};
MatterComponent component;MatterComponent*global_matter_component=&component;
std::string format_attribute_value(const esp_matter_attr_val_t&){return "test";}
struct AttributeActionUpdate{uint16_t endpoint_id;uint32_t cluster_id,attribute_id;MatterAttributeValue value;};
std::vector<std::pair<void(*)(intptr_t),intptr_t>>queue;
namespace chip::DeviceLayer {
 struct Platform{CHIP_ERROR ScheduleWork(void(*f)(intptr_t),intptr_t c){if(fail_schedule)return {1};queue.push_back({f,c});return {};}};
 Platform&PlatformMgr(){static Platform p;return p;}
}
void drain(){auto q=std::move(queue);queue.clear();for(auto [f,c]:q)f(c);}
'''.replace('ENUMS','enum esp_matter_val_type_t{'+','.join('ESP_MATTER_VAL_TYPE_'+t for t in types)+'};')
yaml=(root/'bc7215a-c3.yaml').read_text(encoding='utf-8')
sensor_lambda=re.search(r'path: matter_ac\.thermostat\.local_temperature[ \t]*\n(?:[ \t]*#[^\n]*\n)*([ \t]*)value: !lambda ([^\n]+)',yaml)
assert sensor_lambda, 'Expected the actual SHT40 to Matter conversion'
sensor_body=sensor_lambda.group(2).strip()
if sensor_body in ('|-','|'):
 sensor_lines=[]
 indent=len(sensor_lambda.group(1))
 for line in yaml[sensor_lambda.end():].splitlines():
  if not line.strip():continue
  if len(line)-len(line.lstrip())<=indent:break
  sensor_lines.append(line.strip())
 sensor_body='\n'.join(sensor_lines)
pre+='\nint16_t sensor_temperature(float x){'+sensor_body+'}\n'
test=r'''
// Model the verified SDK WRITABLE path: report(false) -> set_val ->
// WriteAttribute -> unconditional POST_UPDATE, including derived attributes.
namespace esp_matter::attribute {
esp_err_t report(uint16_t ep,uint32_t cluster,uint32_t attr,esp_matter_attr_val_t*v){
 reports++;last=*v;
 if(echo_report){
  endpoint_attribute_update_cb(POST_UPDATE,ep,cluster,attr,v,nullptr);
  endpoint_attribute_update_cb(POST_UPDATE,ep,cluster,attr+1,v,nullptr);
 }
 return fail_report ? 1 : ESP_OK;
}
}
int main(){
 set_attribute_value(2,6,0,true);assert(reports==0);drain();assert(reports==1&&callbacks==0&&last.val.b);
 expected_type=ESP_MATTER_VAL_TYPE_INT16;set_attribute_value(1,0x201,0x11,int64_t(2500));drain();assert(last.val.i16==2500&&callbacks==0);
 expected_type=ESP_MATTER_VAL_TYPE_ENUM8;set_attribute_value(1,0x201,0x1c,uint64_t(3));drain();assert(last.val.u8==3&&callbacks==0);
 endpoint_attribute_update_cb(esp_matter::attribute::POST_UPDATE,1,0x201,0x1c,&last,nullptr);assert(callbacks==1);
 // Corrective boolean reports must not become remote commands.
 expected_type=ESP_MATTER_VAL_TYPE_BOOLEAN;set_attribute_value(2,6,0,false);drain();assert(!last.val.b&&callbacks==1);
 // Periodic SHT40 -> climate on_state -> Matter reports must never become commands.
 for(int i=0;i<240;i++){
  expected_type=ESP_MATTER_VAL_TYPE_ENUM8;set_attribute_value(1,0x201,0x1c,uint64_t(3));
  expected_type=ESP_MATTER_VAL_TYPE_ENUM8;drain();
  expected_type=ESP_MATTER_VAL_TYPE_INT16;set_attribute_value(1,0x201,0x11,int64_t(2500));drain();
  set_attribute_value(1,0x201,0x12,int64_t(2500));drain();
 }
 assert(callbacks==1&&queue.empty()&&local_report_depth==0);
 fail_report=true;set_attribute_value(1,0x201,0x11,int64_t(2600));drain();
 assert(callbacks==1&&local_report_depth==0);fail_report=false;
 endpoint_attribute_update_cb(esp_matter::attribute::POST_UPDATE,1,0x201,0x11,&last,nullptr);
 assert(callbacks==2); // A real external write immediately after a report is preserved.
 echo_report=false;set_attribute_value(1,0x201,0x11,int64_t(2400));drain();assert(callbacks==2);
 expected_type=ESP_MATTER_VAL_TYPE_BOOLEAN;
 int n=reports;set_attribute_value(2,6,0,int64_t(1));drain();assert(reports==n);
 // The real YAML conversion must publish unknown after sensor failures, then
 // recover; an old valid value must not survive as Matter's current reading.
 expected_type=ESP_MATTER_VAL_TYPE_NULLABLE_INT16;
 set_attribute_value(1,0x201,0,int64_t(sensor_temperature(25.5f)));drain();assert(last.val.i16==2550&&!last.is_null());
 for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),-40.1f,125.1f}){
  set_attribute_value(1,0x201,0,int64_t(sensor_temperature(bad)));drain();assert(last.is_null());
 }
 for(float good:{-40.0f,0.0f,26.0f,125.0f}){
  set_attribute_value(1,0x201,0,int64_t(sensor_temperature(good)));drain();assert(!last.is_null()&&last.val.i16==std::lround(good*100));
 }
 n=reports;
 expected_type=ESP_MATTER_VAL_TYPE_INVALID;set_attribute_value(9,9,9,true);drain();assert(reports==n);
 fail_schedule=true;set_attribute_value(2,6,0,true);assert(queue.empty());
 std::cout<<"PASS Matter bridge: queued local reports, no command echo, remote callback, corrective reports, type/missing-attribute/scheduling guards\n";
}
'''
(b/'matter_test.cpp').write_text(pre+guard+'\n'.join(function(n) for n in ['set_attribute_value_on_matter_thread','update_attribute_action_on_matter_thread','set_attribute_value','endpoint_attribute_update_cb'])+test,encoding='utf-8')
exe=b/'matter_test.exe';subprocess.run([a.cxx,'c++','-std=c++17',str(b/'matter_test.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
setup=(root/'components/matter/matter_component.cpp').read_text(encoding='utf-8')
assert '  replay_attribute_triggers(this);' not in setup
print('PASS startup: Matter defaults are not replayed as remote IR commands')

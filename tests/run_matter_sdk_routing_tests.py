"""Prove the pinned SDK report() writable routing using its actual function bodies.
Only CHIP storage/transport is stubbed; no network or device access.
"""
from pathlib import Path
import argparse,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--cxx',required=True);p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();a.build_dir.mkdir(parents=True,exist_ok=True)
def extract(path,signature):
 s=path.read_text(encoding='utf-8');start=s.index(signature);op=s.index('{',start);end=op+1;depth=1
 while depth: depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
model=a.sdk/'data_model/esp_matter_data_model.cpp';utils=a.sdk/'data_model/esp_matter_attribute_utils.cpp'
provider=(a.sdk/'data_model_provider/esp_matter_data_model_provider.cpp').read_text(encoding='utf-8')
assert 'attribute::execute_callback(attribute::POST_UPDATE' in provider
assert 'execute_post_update();' in provider
pre=r'''
#include <cstdint>
#include <cassert>
#include <iostream>
#define ESP_LOGE(...) ((void)0)
#define VerifyOrReturnError(c,e,...) do {if(!(c))return e;}while(0)
using esp_err_t=int;constexpr int ESP_OK=0,ESP_ERR_INVALID_ARG=1,ESP_ERR_NOT_FOUND=2,ESP_ERR_NOT_SUPPORTED=3,ESP_ERR_NOT_FINISHED=4;
constexpr int portMAX_DELAY=0,ESP_MATTER_VAL_TYPE_INVALID=0,ESP_MATTER_VAL_TYPE_ARRAY=1;
constexpr uint16_t ATTRIBUTE_FLAG_WRITABLE=1,ATTRIBUTE_FLAG_MANAGED_INTERNALLY=2;
struct esp_matter_attr_val_t{int type=2;};
struct attribute_t{};
int callbacks=0,writes=0,reports=0;
namespace chip::app {struct AttributePathParams{AttributePathParams(uint16_t,uint32_t,uint32_t){}};}
namespace lock {struct ScopedChipStackLock{ScopedChipStackLock(int){}};}
namespace data_model::provider {struct Provider{void ReportAttributeChanged(chip::app::AttributePathParams){reports++;}};Provider&get_instance(){static Provider p;return p;}}
namespace esp_matter::attribute {
attribute_t attr;uint16_t flags=ATTRIBUTE_FLAG_WRITABLE;
attribute_t*get(uint16_t,uint32_t,uint32_t){return &attr;}
int get_val_type(attribute_t*){return 2;}
uint16_t get_flags(attribute_t*){return flags;}
void val_print(uint16_t,uint32_t,uint32_t,esp_matter_attr_val_t*,bool){}
// Verified WriteAttribute runs the application's POST_UPDATE synchronously on
// success. No hardware/CHIP dependencies are needed to test the routing decision.
esp_err_t set_val_via_write_attribute(uint16_t,uint32_t,uint32_t,esp_matter_attr_val_t*){writes++;callbacks++;return ESP_OK;}
esp_err_t set_val_internal(attribute_t*,esp_matter_attr_val_t*,bool call_callbacks){if(call_callbacks)callbacks++;return ESP_OK;}
'''
code=pre+extract(model,'esp_err_t set_val(uint16_t endpoint_id')+r'''
esp_err_t set_val(attribute_t*,esp_matter_attr_val_t*v,bool callbacks){return set_val(1,0x201,0x11,v,callbacks);}
'''+extract(utils,'static esp_err_t update_or_report(')+extract(utils,'esp_err_t report(')+r'''
}
int main(){
 esp_matter_attr_val_t val;
 using namespace esp_matter::attribute;
 assert(report(1,0x201,0x11,&val)==ESP_OK);
 assert(callbacks==1&&writes==1&&reports==1);
 flags=0;callbacks=writes=reports=0;
 assert(report(1,0x201,0,&val)==ESP_OK);
 assert(callbacks==0&&writes==0&&reports==1);
 std::cout<<"PASS actual SDK routing: report(false) invokes writable WriteAttribute callback; readonly report suppresses it\n";
}
'''
f=a.build_dir/'sdk_routing.cpp';f.write_text(code,encoding='utf-8');exe=a.build_dir/'sdk_routing.exe'
subprocess.run([a.cxx,'c++','-std=c++17',str(f),'-o',str(exe)],check=True);subprocess.run([str(exe.resolve())],check=True)

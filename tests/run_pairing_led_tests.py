"""Exercise production pairing transitions and the YAML light automation."""
from pathlib import Path
import argparse
import subprocess
import textwrap

p=argparse.ArgumentParser()
p.add_argument('--cxx',default='g++')
p.add_argument('--cxx-arg',action='append',default=[])
p.add_argument('--build-dir',type=Path,required=True)
args=p.parse_args()
root=Path(__file__).resolve().parents[1]
cpp=(root/'components/bc7215a/bc7215a_climate.cpp').read_text(encoding='utf-8')
header=(root/'components/bc7215a/bc7215a_climate.h').read_text(encoding='utf-8')
y=(root/'bc7215a-c3.yaml').read_text(encoding='utf-8')
def extract(name):
    pos=cpp.index('BC7215AClimate::'+name+'(')
    start=cpp.rfind('\n',0,pos)+1
    opening=cpp.index('{',pos); end=opening+1; level=1
    while level:
        level+=(cpp[end]=='{')-(cpp[end]=='}'); end+=1
    return cpp[start:end]
phase=header[header.index('enum Phase'):header.index('phase_{PROBE};')+len('phase_{PROBE};')]
automation=textwrap.dedent(y[y.index('          using namespace esphome::bc7215a;'):y.index('\nbutton:')])
pre=r'''
#include "status_led.h"
#include <cassert>
uint32_t now=0;uint32_t millis(){return now;}
#include <string>
#include <iostream>
struct Sensor {std::string value; void publish_state(const char *v){value=v;}
 void publish_state(bool v){value=v?"on":"off";}};
struct AC {void stopCapture(){} void startCapture(){}};
struct Driver {void setRx(){}};
struct PairingData {int marker=0;};
struct BC7215AClimate {
'''
mock=r'''
 AC ac; AC *ac_=&ac; Driver driver; Driver *driver_=&driver;
 Sensor match,identity,paired,known;
 Sensor *match_info_=&match,*pairing_id_=&identity,*paired_=&paired,*known_sensor_=&known;
 bool pending_=false, paired_ok_=false,known_=false,restore=false,count_ok=true;
 unsigned rx_recovery_stage_=0;bool rx_watch_=false;
 PairingData candidate_,saved_{42}; std::string status;
 bool apply_pairing_(const PairingData &){return restore;}
 bool count_candidates_(){if(!count_ok)phase_=FAULT;return count_ok;}
 void publish_match_(){} void listen_(){phase_=LISTEN;}
 void status_message_(const char *s){status=s;}
 bool is_pairing()const; void start_pairing(); void cancel_pairing();
};
struct Light {
 bool on=false;int calls=0,color=0;float brightness=0;
 struct Call {Light *l;bool state=false;
   void set_brightness(float b){l->brightness=b;}
   void set_rgb(float r,float g,float b){l->color=(r?4:0)|(g?2:0)|(b?1:0);}
   void set_state(bool v){state=v;}
   void set_transition_length(int){}
   void perform(){l->on=state;l->calls++;}
 };
 Call make_call(){return {this};}
};
struct WiFi {bool ap=false,connected=false;bool is_ap_active(){return ap;}bool is_connected(){return connected;}};
struct Matter {bool open=false;bool is_commissioning_window_open(){return open;}};
BC7215AClimate ac_controller; Light pairing_rgb;WiFi network_wifi;Matter matter_controller;
#define id(x) x
'''
tests=r'''
int main(){
 auto &c=ac_controller;auto &led=pairing_rgb;
 tick();assert(led.on&&led.color==4&&led.calls==1);
 network_wifi.ap=true;tick();assert(led.on&&led.color==4);now+=500;tick();assert(!led.on);
 c.phase_=c.UNPAIRED;now+=500;tick();assert(led.on&&led.color==4);
 c.start_pairing();tick();assert(led.on&&led.color==1);c.restore=false;c.cancel_pairing();tick();assert(led.on&&led.color==4);
 now+=500;tick();assert(!led.on); // AP red blinking resumes after cancellation.
 network_wifi.ap=false;network_wifi.connected=true;c.phase_=c.LISTEN;tick();assert(led.on&&led.color==2);
 c.start_pairing();tick();assert(led.on&&led.color==1&&led.brightness==0.20f);
 const int first=led.calls;for(int i=0;i<100;i++)tick();assert(led.calls==first);
 now+=500;tick();assert(!led.on);c.phase_=c.EXTRA;tick();assert(!led.on);
 c.restore=true;c.cancel_pairing();tick();assert(c.phase_==c.LISTEN&&led.color==2&&c.candidate_.marker==42);
 c.start_pairing();c.restore=false;c.cancel_pairing();tick();assert(c.phase_==c.UNPAIRED&&!c.paired_ok_&&c.saved_.marker==42);
 c.phase_=c.LISTEN;matter_controller.open=true;tick();assert(led.color==2&&led.on);
 now+=500;tick();assert(led.on&&led.color==2);matter_controller.open=false;tick();assert(led.on&&led.color==2);
 matter_controller.open=true;tick();assert(led.on&&led.color==2);
 c.phase_=c.LISTEN;tick();assert(led.color==2);matter_controller.open=false;tick();assert(led.color==2);
 network_wifi.connected=false;tick();assert(led.color==4&&led.on);
 std::cout<<"PASS production YAML RGB: four modes, Matter leaves steady green, blink continuity, pairing cancellation, no repeated effect/transition calls\\n";
}

'''
args.build_dir.mkdir(parents=True,exist_ok=True)
source=args.build_dir/'pairing_led_test.cpp';exe=args.build_dir/'pairing_led_test.exe'
source.write_text(pre+phase+mock+'\n'.join(extract(n) for n in ['is_pairing','start_pairing','cancel_pairing'])+'\nvoid tick(){\n'+automation+'\n}\n'+tests,encoding='utf-8')
subprocess.run([args.cxx,*args.cxx_arg,'-std=c++17','-Wall','-Wextra','-I'+str(root/'components/bc7215a'),str(source),'-o',str(exe)],check=True)
subprocess.run([str(exe.resolve())],check=True)

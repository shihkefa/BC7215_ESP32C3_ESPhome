"""Run the production IDF compatibility header and driver against IDF GPIO semantics."""
from pathlib import Path
import argparse, subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', required=True)
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--component', type=Path)
p.add_argument('--expect-broken', action='store_true')
a = p.parse_args()
c = (a.component or Path(__file__).resolve().parents[1] / 'components/bc7215a').resolve()
b = a.build_dir.resolve()
for d in ('esphome/core', 'driver'):
    (b / d).mkdir(parents=True, exist_ok=True)
(b/'esphome/core/defines.h').write_text('#pragma once\n')
(b/'esphome/core/hal.h').write_text('''#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t now_ms=0;
inline uint32_t millis(){return now_ms;}
inline uint32_t micros(){return now_ms*1000;}
inline void delay(unsigned ms){now_ms+=ms;}
}
''')
(b/'driver/gpio.h').write_text('''#pragma once
using gpio_num_t=int;
enum gpio_mode_t { GPIO_MODE_INPUT=1, GPIO_MODE_OUTPUT=2, GPIO_MODE_INPUT_OUTPUT=3 };
inline int gpio_modes[22]{}, gpio_levels[22]{};
inline int gpio_set_direction(int p,gpio_mode_t m){gpio_modes[p]=m;return 0;}
inline int gpio_set_level(int p,int v){gpio_levels[p]=v;return 0;}
// ESP-IDF documented behavior: input-disabled pads always read zero.
inline int gpio_get_level(int p){return (gpio_modes[p]&GPIO_MODE_INPUT)?gpio_levels[p]:0;}
''')
(b/'main.cpp').write_text('''#include "bc7215.h"
#include <deque>
#include <vector>
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<#x<<" at "<<__LINE__<<"\\n";return 1;}}while(0)
struct UART:Stream {
 std::deque<uint8_t> rx;std::vector<uint8_t> tx;
 int available()override{return rx.size();}
 int read()override{if(rx.empty())return -1;int n=rx.front();rx.pop_front();return n;}
 int peek()override{return rx.empty()?-1:rx.front();}
 size_t write(uint8_t n)override{tx.push_back(n);return 1;}
 void flush()override{}
};
int main(){
 UART u;BC7215 d(u,0,1);
 CHECK(digitalRead(0)==HIGH);CHECK(gpio_modes[1]==GPIO_MODE_INPUT);
 d.abortCommand();CHECK(u.tx.empty());
 d.setTx();d.setShutDown();CHECK((u.tx==std::vector<uint8_t>{0xf7,0}));
 u.rx={0x7a};CHECK(d.cmdCompleted());
 d.setRx();delay(50);d.setRxMode(1);CHECK(u.tx.back()==1);
 u.rx={0x55,0x12,8,0,0x7a};CHECK(d.dataReady());
 bc7215DataMaxPkt_t data{};CHECK(d.getData(data)==0x12);CHECK(data.bitLen==8&&data.data[0]==0x55);
 auto n=u.tx.size();d.irTx(data);CHECK(u.tx.size()==n); // RX must not transmit IR.
 d.setTx();d.irTx(data);CHECK((std::vector<uint8_t>(u.tx.begin()+n,u.tx.end())==std::vector<uint8_t>{0xf5,2,8,0,0x55}));
 u.rx={0x7a};CHECK(d.cmdCompleted());
 d.setRx();u.rx={0x56,0x77};CHECK(d.isBusy());
 d.setTx();d.resetReceiveState();d.abortCommand();delay(40);d.setShutDown();
 u.rx={0x7a};CHECK(d.cmdCompleted());d.setRx();delay(50);d.setRxMode(1);
 CHECK(!d.isBusy());CHECK(digitalRead(0)==HIGH);
 d.setTx();gpio_levels[1]=1;n=u.tx.size();d.abortCommand();CHECK(d.io_error&&u.tx.size()==n);
 std::cout<<"PASS IDF real driver: MOD readback, RX mode, RX bytes, TX guard, IR bytes, ACK, recovery, BUSY timeout\\n";
}
''')
exe=b/'idf_driver.exe'
subprocess.run([a.cxx,'c++','-std=c++17','-I'+str(b),'-I'+str(c),str(c/'bc7215.cpp'),str(b/'main.cpp'),'-o',str(exe)],check=True)
r = subprocess.run([str(exe)])
if a.expect_broken:
    if r.returncode == 0: raise SystemExit('Expected v1.0.22 regression was not reproduced')
    print('PASS: v1.0.22 regression reproduced')
else:
    r.check_returncode()

"""Exercise production UART adapter + BC7215 driver with bounded FIFO/CTS model."""
from pathlib import Path
import argparse, subprocess
p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
c=Path(__file__).resolve().parents[1]/'components/bc7215a'
for d in ('esphome/core','driver'): (b/d).mkdir(exist_ok=True,parents=True)
(b/'esphome/core/defines.h').write_text('#pragma once\n#define USE_ESP32\n#define USE_ESP_IDF\n')
(b/'esphome/core/hal.h').write_text('''#pragma once
#include <cstdint>
namespace esphome { inline uint32_t now=0;inline uint32_t millis(){return now;}
inline uint32_t micros(){return now*1000;}inline void delay(unsigned ms){now+=ms;} }
''')
(b/'driver/gpio.h').write_text('''#pragma once
using gpio_num_t=int;enum gpio_mode_t{GPIO_MODE_INPUT=1,GPIO_MODE_OUTPUT=2,GPIO_MODE_INPUT_OUTPUT=3};
inline int levels[22]{};inline int gpio_set_direction(int,gpio_mode_t){return 0;}
inline int gpio_set_level(int p,int v){levels[p]=v;return 0;}inline int gpio_get_level(int p){return levels[p];}
''')
src=(c/'bc7215a_climate.cpp').read_text(encoding='utf-8')
adapter=src[src.index('int UARTStream::read()'):src.index('static constexpr uint32_t MAGIC')]
header=(c/'bc7215a_climate.h').read_text(encoding='utf-8')
cls=header[header.index('class UARTStream'):header.index('struct Capture')]
pre=r'''
#include "bc7215.h"
#include <algorithm>
#include <cassert>
#include <deque>
#include <vector>
#include <cstdio>
#include <iostream>
using uart_port_t=int;
constexpr int ESP_OK=0,UART_PIN_NO_CHANGE=-1,UART_HW_FLOWCTRL_CTS=2;
#define ESP_LOGI(...)
#define ESP_LOGD(...)
#define ESP_LOGW(...)
#define pdMS_TO_TICKS(x) (x)
#define UART_LL_GET_HW(x) (x)
std::deque<uint8_t> fifo;std::vector<uint8_t> wire;
uint32_t hold_until=0, serviced=0;bool stuck=false;int resets=0,cts_pin=-1,flow=-1;
void tick(){while(serviced<esphome::now){++serviced;if(!stuck&&serviced>=hold_until&&!fifo.empty()){wire.push_back(fifo.front());fifo.pop_front();}}}
int uart_set_pin(int,int,int,int,int cts){cts_pin=cts;return ESP_OK;}
int uart_set_hw_flow_ctrl(int,int f,int){flow=f;return ESP_OK;}
int uart_tx_chars(int,const char *p,uint32_t n){tick();unsigned count=std::min(n,unsigned(128-fifo.size()));for(unsigned i=0;i<count;i++)fifo.push_back(uint8_t(p[i]));assert(fifo.size()<=128);return count;}
int uart_wait_tx_done(int,uint32_t timeout){uint32_t start=esphome::now;tick();while(!fifo.empty()&&esphome::now-start<timeout){esphome::delay(1);tick();}return fifo.empty()?0:-1;}
void uart_ll_txfifo_rst(int){fifo.clear();resets++;}
namespace esphome::uart {
enum class UARTFlushResult {UART_FLUSH_RESULT_SUCCESS,UART_FLUSH_RESULT_TIMEOUT};
struct UARTComponent {int ordinary_flushes=0;int available(){return 0;}bool read_byte(uint8_t*){return false;}bool peek_byte(uint8_t*){return false;}
void write_byte(uint8_t c){assert(uart_tx_chars(0,reinterpret_cast<char*>(&c),1)==1);}
UARTFlushResult flush(){ordinary_flushes++;return uart_wait_tx_done(0,100)==0?UARTFlushResult::UART_FLUSH_RESULT_SUCCESS:UARTFlushResult::UART_FLUSH_RESULT_TIMEOUT;}};
struct IDFUARTComponent:UARTComponent {int get_hw_serial_number(){return 1;}};
}
namespace esphome::bc7215a {
'''
tests=r'''
}
using namespace esphome::bc7215a;
int main(){
 esphome::uart::IDFUARTComponent bus;UARTStream u(&bus);BC7215 d(u,0,1);
 assert(u.configure_cts(1)&&cts_pin==1&&flow==UART_HW_FLOWCTRL_CTS);d.setTx();
 d.setShutDown();assert((wire==std::vector<uint8_t>{0xf7,0}));assert(bus.ordinary_flushes==0);
 wire.clear();bc7215FormatPkt_t fmt{};fmt.signature.inByte=0x30;fmt.format[0]=0x7a;fmt.format[1]=0x7b;
 d.loadFormat(fmt);assert(wire.size()==37&&wire[0]==0xf6&&wire[1]==1&&wire[2]==0x30);
 assert(wire[3]==0x7b&&wire[4]==0xfa&&wire[5]==0x7b&&wire[6]==0xfb);
 wire.clear();std::vector<uint8_t> raw(300,0x55);hold_until=esphome::now+100;
 d.sendRaw(raw.data(),raw.size());assert(wire.size()==304&&wire[0]==0xf5&&wire[1]==2&&!u.io_error);
 assert(bus.ordinary_flushes==0); // no per-byte task wake/flush
 wire.clear();stuck=true;uint32_t start=esphome::now;d.sendRaw(raw.data(),raw.size());
 assert(u.io_error&&wire.empty()&&fifo.empty()&&resets==1&&esphome::now-start<=2000);
 d.setShutDown();assert(wire.empty()); // failed stream cannot leak remaining bytes
 stuck=false;u.io_error=false;d.abortCommand();assert(wire.size()==1&&wire[0]==0x7a);
 wire.clear();esphome::delay(40);d.setShutDown();assert((wire==std::vector<uint8_t>{0xf7,0}));
 u.beginPacket();for(unsigned i=0;i<1101;i++)u.write(1);u.endPacket();assert(u.io_error&&fifo.empty());
 std::cout<<"PASS production IDF packet transport: CTS mapping, stuffing, long FIFO refill, no per-byte flush, bounded stuck CTS, FIFO discard, recovery, overflow\n";
}
'''
(b/'main.cpp').write_text(pre+cls+adapter+tests,encoding='utf-8')
exe=b/'packet.exe'
subprocess.run([a.cxx,'c++','-std=c++17','-I'+str(b),'-I'+str(c),str(c/'bc7215.cpp'),str(b/'main.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

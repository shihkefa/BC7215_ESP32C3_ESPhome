"""Compile the real BC7215 driver with an in-memory UART and fake GPIO/time."""
from pathlib import Path
import argparse
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--cxx',required=True)
p.add_argument('--cxx-arg',action='append',default=[])
p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args()
c=Path(__file__).resolve().parents[1]/'components/bc7215a'
b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
(b/'esphome/core').mkdir(parents=True,exist_ok=True)
(b/'esphome/core/defines.h').write_text('#pragma once\n#define USE_ARDUINO\n')
(b/'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
inline int pins[10]{};
inline uint32_t clock_ms=0;
inline uint32_t millis(){return clock_ms;}
inline uint32_t micros(){return clock_ms*1000;}
inline void delay(unsigned ms){clock_ms+=ms;}
inline void pinMode(int,int){}
inline void digitalWrite(int p,int v){pins[p]=v;}
inline int digitalRead(int p){return pins[p];}
''',encoding='utf-8')
(b/'Stream.h').write_text('''#pragma once
#include <Arduino.h>
class Stream { public:
virtual ~Stream()=default;
virtual int available()=0;
virtual int read()=0;
virtual size_t write(uint8_t)=0;
virtual void flush()=0;
};
''',encoding='utf-8')
(b/'test.cpp').write_text('''#include "bc7215.h"
#include <deque>
#include <vector>
#include <cassert>
#include <iostream>
struct UART : Stream {
std::deque<uint8_t> rx;std::vector<uint8_t> tx;
int available()override{return rx.size();}
int read()override{if(rx.empty())return -1;auto v=rx.front();rx.pop_front();return v;}
size_t write(uint8_t v)override{tx.push_back(v);return 1;}
void flush()override{}
};
int main(){
UART uart;BC7215 d(uart,0,1);
d.abortCommand();assert(uart.tx.empty()); // Never inject reset into receive mode.
d.setTx();d.resetReceiveState();d.abortCommand();delay(40);d.setShutDown();
assert((uart.tx==std::vector<uint8_t>{0x7a,0xf7,0x00}));
assert(!d.cmdCompleted());uart.rx={0x7a};assert(d.cmdCompleted());
d.resetReceiveState();d.setRx();delay(50);d.setRxMode(1);
assert((uart.tx==std::vector<uint8_t>{0x7a,0xf7,0x00,0x01}));
// Unterminated receive data may mark the parser busy. Recovery clears it.
uart.rx={0x56,0x77};assert(d.isBusy());d.setTx();d.resetReceiveState();d.setRx();
assert(!d.isBusy()&&!d.dataReady()&&!d.formatReady());
d.setTx();pins[1]=HIGH;auto n=uart.tx.size();d.abortCommand();
assert(d.io_error&&uart.tx.size()==n); // BUSY timeout must not emit a command.
std::cout<<"PASS real driver: abort/standby/wake bytes, ACK, partial RX reset and BUSY timeout; no F5 IR command\\n";
}
''',encoding='utf-8')
exe=b/'driver_test.exe'
subprocess.run([a.cxx,*a.cxx_arg,'-std=c++17','-I'+str(b),'-I'+str(c),str(c/'bc7215.cpp'),str(b/'test.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

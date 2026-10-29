"""Exercise queued data/format segments through both production UART/capture drivers."""
from pathlib import Path
import argparse
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', required=True)
p.add_argument('--cxx-arg', action='append', default=[])
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--component', type=Path)
p.add_argument('--expect-broken', action='store_true')
a = p.parse_args()
c = (a.component or Path(__file__).resolve().parents[1] / 'components/bc7215a').resolve()
b = a.build_dir.resolve()
(b / 'esphome/core').mkdir(parents=True, exist_ok=True)
(b / 'esphome/core/defines.h').write_text('#pragma once\n#define USE_ARDUINO\n')
(b / 'Arduino.h').write_text('''#pragma once
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
''')
(b / 'Stream.h').write_text('''#pragma once
#include <Arduino.h>
class Stream {public:virtual ~Stream()=default;virtual int available()=0;
virtual int read()=0;virtual size_t write(uint8_t)=0;virtual void flush()=0;};
''')
(b / 'test.cpp').write_text(r'''#include "bc7215ac.h"
#include <deque>
#include <vector>
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<#x<<" at "<<__LINE__<<"\n";return 1;}}while(0)
struct UART:Stream {
 std::deque<uint8_t> rx;std::vector<uint8_t> tx;
 int available()override{return int(rx.size());}
 int read()override{if(rx.empty())return -1;int c=rx.front();rx.pop_front();return c;}
 size_t write(uint8_t c)override{tx.push_back(c);return 1;}void flush()override{}
 void stuff(uint8_t c){if(c==0x7a||c==0x7b){rx.push_back(0x7b);rx.push_back(c|0x80);}else rx.push_back(c);}
 void data(uint8_t value,uint16_t bits,uint8_t status){
  for(unsigned i=0;i<(bits+7)/8;i++)stuff(value);
  stuff(status);stuff(bits&255);stuff(bits>>8);rx.push_back(0x7a);
 }
 void format(uint8_t status){stuff(status&0x3f);for(int i=0;i<32;i++)stuff(0x7a+i%2);rx.push_back(0x7a);rx.push_back(0x7a);}
 void segment(uint8_t value,uint16_t bits,uint8_t status){data(value,bits,status);format(status);}
};
int main(){
 // The whole burst is already in the UART ring buffer before the first loop.
 // Remain below the production 4096-byte receive buffer, including stuffing.
 for(unsigned segments=1;segments<=4;segments++){
  UART uart;BC7215 driver(uart,0,1);BC7215AC ac(driver);ac.startCapture();
  bc7215DataMaxPkt_t profile[4]{};uint8_t status[4]{};
  for(unsigned i=0;i<segments;i++){profile[i].bitLen=40+8*i;status[i]=0x12+i;}
  ac.setCaptureProfile(segments,profile,status);
  for(unsigned press=0;press<8;press++)for(unsigned i=0;i<segments;i++)
   uart.segment(press%2?0x7a:0x7b,40+8*i,status[i]|(press%2?0x40:0));
  CHECK(uart.rx.size()<4096);unsigned complete=0;
  for(unsigned loops=0;loops<100&&!uart.rx.empty();loops++){
   if(ac.signalCaptured()){
    CHECK(ac.sampleCount==segments&&!ac.capture_overflow);
    for(unsigned i=0;i<segments;i++){
     CHECK(ac.sampleData[i].bitLen==40+8*i);
     CHECK(ac.sampleData[i].data[0]==(complete%2?0x7a:0x7b));
     CHECK(ac.sampleStatus[i]==(status[i]|(complete%2?0x40:0)));
     CHECK(ac.sampleFormat[i].format[0]==0x7a&&ac.sampleFormat[i].format[1]==0x7b);
    }
    complete++;ac.resumeCapture();
   }
   clock_ms++;
  }
  // isBusy() can prefetch one complete final segment before UART empties.
  if(ac.signalCaptured()){
   CHECK(ac.sampleCount==segments&&!ac.capture_overflow);
   for(unsigned i=0;i<segments;i++)CHECK(ac.sampleData[i].data[0]==(complete%2?0x7a:0x7b));
   complete++;ac.resumeCapture();
  }
  CHECK(complete==8&&uart.rx.empty());
 }
 // Pairing captures every queued segment and waits for its normal idle boundary.
 {UART uart;BC7215 driver(uart,0,1);BC7215AC ac(driver);ac.startCapture();
  for(int i=0;i<4;i++)uart.segment(0x31+i,40+8*i,0x12);
  for(int i=0;i<4;i++)CHECK(!ac.signalCaptured());
  delay(201);CHECK(ac.signalCaptured()&&ac.sampleCount==4&&!ac.capture_overflow);
  for(int i=0;i<4;i++)CHECK(ac.sampleData[i].data[0]==0x31+i);
 }
 // Read methods must preserve the associated data after getFormat clears its flag.
 {UART uart;BC7215 driver(uart,0,1);driver.setRx();
  uart.segment(0x11,40,0x12);uart.segment(0x22,48,0x12);
  CHECK(driver.formatReady());bc7215FormatPkt_t fmt{};driver.getFormat(fmt);
  CHECK(driver.getLen()==40&&driver.dpketSize()==7);
  bc7215DataMaxPkt_t first{};CHECK(driver.getData(first)==0x12&&first.bitLen==40&&first.data[0]==0x11);
  CHECK(driver.formatReady());driver.getFormat(fmt);uint8_t raw[8]{};
  CHECK(driver.getRaw(raw,8)==6&&raw[0]==0x22&&uart.rx.empty());
 }
 // Data and format can arrive separately, and standalone packets remain readable.
 {UART uart;BC7215 driver(uart,0,1);BC7215AC ac(driver);ac.startCapture();
  uart.data(0x35,40,0x12);CHECK(!ac.signalCaptured());
  uart.format(0x12);CHECK(!ac.signalCaptured());delay(201);
  CHECK(ac.signalCaptured()&&ac.sampleCount==1&&ac.sampleData[0].data[0]==0x35);
  ac.resumeCapture();driver.setTx();driver.setShutDown();uart.rx={0x7a};CHECK(driver.cmdCompleted());
  driver.setRx();uart.data(0x41,8,0x12);CHECK(driver.dataReady());
  bc7215DataMaxPkt_t pkt{};CHECK(driver.getData(pkt)==0x12&&pkt.bitLen==8&&pkt.data[0]==0x41);
 }
 std::cout<<"PASS real UART backlog: 1-4 segments, 32 rapid commands, REV/stuffing, pairing idle boundary, data getters, split arrival and TX ACK\n";
}
''')
obj = b / 'vendor.o'
if Path(a.cxx).name.lower() in ('zig', 'zig.exe'):
    cc = [a.cxx, 'cc']
else:
    cc = [a.cxx, '-x', 'c']
subprocess.run([*cc, '-std=c99', '-c', str(c / 'bc7215_ac_lib.c'), '-o', str(obj)], check=True)
exe = b / 'uart_backlog.exe'
subprocess.run([a.cxx, *a.cxx_arg, '-std=c++17', '-I' + str(b), '-I' + str(c),
                str(c / 'bc7215.cpp'), str(c / 'bc7215ac.cpp'), str(obj),
                str(b / 'test.cpp'), '-o', str(exe)], check=True)
r = subprocess.run([str(exe)])
if a.expect_broken:
    if r.returncode == 0:
        raise SystemExit('Expected UART backlog regression was not reproduced')
    print('PASS: pre-fix UART backlog loss reproduced')
else:
    r.check_returncode()

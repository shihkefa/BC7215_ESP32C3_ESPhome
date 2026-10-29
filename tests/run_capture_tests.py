"""Exercise production capture methods with a queued UART packet driver and clock."""
from pathlib import Path
import argparse, subprocess
p=argparse.ArgumentParser()
p.add_argument('--cxx',required=True);p.add_argument('--cxx-arg',action='append',default=[])
p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
source=(root/'components/bc7215a/bc7215ac.cpp').read_text(encoding='utf-8')
def extract(name):
    start=source.rfind('\n',0,source.index('BC7215AC::'+name+'('))+1
    opening=source.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
pre=r'''
#include <cassert>
#include <cstdint>
#include <deque>
#include <iostream>
using uint8_t=std::uint8_t;using uint16_t=std::uint16_t;
uint32_t clock_ms=0;unsigned long millis(){return clock_ms;}void delay(unsigned n){clock_ms+=n;}
struct bc7215DataMaxPkt_t{uint16_t bitLen=0;uint8_t data[512]{};};
struct bc7215FormatPkt_t{uint8_t tag=0;};
struct Frame{unsigned bits;uint8_t status,value;};
struct Driver{
 std::deque<Frame> q;unsigned rx=0,tx=0,clears=0;bool busy=false;
 void setRx(){rx++;}void setTx(){tx++;}void setRxMode(int){}
 void clrData(){clears++;if(!q.empty())q.pop_front();}void clrFormat(){}
 bool formatReady(){return !q.empty();}bool dataReady(){return false;}bool isBusy(){return busy;}
 void getFormat(bc7215FormatPkt_t &f){f.tag=q.front().value;}
 uint8_t getData(bc7215DataMaxPkt_t &d){auto f=q.front();q.pop_front();d.bitLen=f.bits;d.data[0]=f.value;return f.status;}
};
struct BC7215AC{
 Driver &bc7215;uint8_t sampleCount=0,sampleStatus[4]{};bc7215DataMaxPkt_t sampleData[4]{};
 bc7215FormatPkt_t sampleFormat[4]{};bool capture_overflow=false,isCapturing=false,receiving=false;
 unsigned long timerStartTime=0;uint8_t expectedCount=0,expectedStatus[4]{};uint16_t expectedBits[4]{};
 void startCapture();void stopCapture();void resumeCapture();bool signalCaptured();
 void setCaptureProfile(uint8_t,const bc7215DataMaxPkt_t*,const uint8_t*);
};
'''
test=r'''
int main(){
 // Repeated 1/2/3/4-segment commands at 10ms segment spacing, no 200ms idle.
 for(unsigned segments=1;segments<=4;segments++){
  Driver d;BC7215AC ac{d};ac.startCapture();auto clears=d.clears;
  bc7215DataMaxPkt_t profile[4]{};uint8_t status[4]{};
  for(unsigned i=0;i<segments;i++){profile[i].bitLen=40+8*i;status[i]=0x12+i;}
  ac.setCaptureProfile(segments,profile,status);
  for(unsigned press=0;press<30;press++)for(unsigned i=0;i<segments;i++)
    d.q.push_back({40+8*i,uint8_t(status[i]|(press%2?0x40:0)),uint8_t(press)});
  unsigned complete=0;
  while(!d.q.empty()){
   clock_ms+=10;
   if(ac.signalCaptured()){
    assert(ac.sampleCount==segments&&!ac.capture_overflow);
    for(unsigned i=0;i<segments;i++)assert(ac.sampleData[i].data[0]==complete);
    complete++;ac.resumeCapture();
   }
  }
  assert(complete==30&&d.rx==1&&d.tx==0&&d.clears==clears);
  // Noise and an incomplete prefix do not prevent the following complete command.
  for(unsigned i=0;i<12;i++)d.q.push_back({24,0x03,99});
  for(unsigned i=0;i<segments-1;i++)d.q.push_back({40+8*i,status[i],90});
  for(unsigned i=0;i<segments;i++)d.q.push_back({40+8*i,status[i],91});
  complete=0;
  while(!d.q.empty()){clock_ms+=10;if(ac.signalCaptured()){
   complete++;for(unsigned i=0;i<segments;i++)assert(ac.sampleData[i].data[0]==91);
   ac.resumeCapture();}}
  assert(complete==1);
  // After explicit TX/next-match transition, resume must re-enable hardware RX.
  ac.stopCapture();ac.resumeCapture();assert(d.tx==1&&d.rx==2&&ac.expectedCount==0);
 }
 // Learning stays idle-delimited and rejects over-capacity captures.
 {Driver d;BC7215AC ac{d};ac.startCapture();
  for(int i=0;i<6;i++){d.q.push_back({40,0x12,0});clock_ms+=10;assert(!ac.signalCaptured());}
  clock_ms+=201;assert(ac.signalCaptured()&&ac.capture_overflow&&ac.sampleCount==4);
 }
 // Non-profile short frame retains the idle fallback for off/special commands.
 {Driver d;BC7215AC ac{d};ac.startCapture();bc7215DataMaxPkt_t profile[1]{};profile[0].bitLen=40;
  uint8_t status[1]={0x12};ac.setCaptureProfile(1,profile,status);
  d.q.push_back({16,0x12,0});assert(!ac.signalCaptured());clock_ms+=201;
  assert(ac.signalCaptured()&&ac.sampleCount==1&&ac.sampleData[0].bitLen==16);
 }
 std::cout<<"PASS capture: 120 rapid commands, 1-4 segments, REV, noise resync, RX continuity, TX return, learning and idle fallback\n";
}
'''
cpp=b/'capture_test.cpp';cpp.write_text(pre+'\n'.join(extract(n) for n in ['startCapture','stopCapture','resumeCapture','setCaptureProfile','signalCaptured'])+test,encoding='utf-8')
exe=b/'capture_test.exe'
subprocess.run([a.cxx,*a.cxx_arg,'-std=c++17',str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

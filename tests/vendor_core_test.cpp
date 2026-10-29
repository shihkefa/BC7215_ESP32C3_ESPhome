#include "bc7215a_climate.h"
#include "pairing_fingerprint.h"
#include <iostream>
using namespace esphome;
using namespace esphome::bc7215a;
int main() {
  uart::UARTComponent bus;UARTStream stream(&bus);BC7215 driver(stream,0,1);BC7215AC ac(driver);
  BC7215AClimate c;c.ac_=&ac;c.driver_=&driver;c.stream_=&stream;
  InternalGPIOPin mod,busy;c.mod_=&mod;c.busy_=&busy;
  text_sensor::TextSensor status;c.status_=&status;
  // Real 2026-10-29 capture: unknown fan must not strand normal reception.
  bc7215FormatPkt_t fmt{};fmt.signature.inByte=0x30;
  const uint8_t fbytes[]={0x14,0xfd,0x0c,0x5d,0x14,0xfd,0x3d,0xfd,0x1e,0x98,0x62,0xa2,0x05,0x92,0x05,0xff,0,0,0x23,0x90,0x43,0xa0,0x43,0,0x66,0x90,0x2f,0x11,0x59,0x11,0x1f,0xe9};memcpy(fmt.format,fbytes,32);
  bc7215DataMaxPkt_t base{134,{0x79,0x09,0x20,0x50,0x0a,0,1,0,0xcf,0x4b,0,0x81,0x13,0,0,0x14,0x04}};
  c.candidate_.magic=0xBC720103;strncpy(c.candidate_.version,ac.getLibVer(),23);
  c.candidate_.base.count=1;c.candidate_.base.status[0]=0x30;
  c.candidate_.base.data[0]=base;c.candidate_.base.format[0]=fmt;
  assert(c.apply_pairing_(c.candidate_));c.paired_ok_=true;c.phase_=c.LISTEN;
  const auto fingerprint=pairing_fingerprint(c.candidate_);
  auto receive=[&](bc7215DataMaxPkt_t data) {
    ac.sampleCount=1;ac.sampleStatus[0]=0x30;ac.sampleData[0]=data;ac.sampleFormat[0]=fmt;
    c.received_();
  };
  auto unknown=base;unknown.data[15]=0x10;unknown.data[16]=0;
  c.current_.fan=3;receive(unknown);
  assert(c.phase_==c.LISTEN&&c.paired_ok_&&!c.known_&&c.current_.fan==3);
  assert(status.state.find("部分設定無法辨識")!=std::string::npos);
  bc7215DataMaxPkt_t low{134,{0x59,0x09,0x20,0x50,0x0a,0,1,0,0xcf,0x4a,0,0x81,0x13,0,0,0x04,0x34}};
  receive(low);
  assert(c.phase_==c.LISTEN&&c.paired_ok_&&c.known_&&c.current_.fan==1);
  const auto *generated=bc7215_ac_set(9,1,1,KEY_FAN);
  if(generated&&!generated->bitLen)generated=reinterpret_cast<const bc7215CombinedMsg_t*>(generated)->body.msg.datPkt;
  assert(generated&&generated->bitLen==low.bitLen&&memcmp(generated->data,low.data,(low.bitLen+7)/8)==0);
  for(int n=0;n<100;n++){receive(unknown);receive(low);assert(c.phase_==c.LISTEN&&c.known_);}
  assert(pairing_fingerprint(c.candidate_)==fingerprint);
  assert(c.traits().swings.empty());
  // Native library coverage: retain its documented match/temperature/fan pipeline.
  unsigned tested=0,skipped=0;
  for(unsigned i=0;i<ac.cntPredef();i++) {
    if(!ac.initPredef(i)){skipped++;continue;}
    int8_t t,m,f,p;
    if(!bc7215_ac_parse(&t,&m,&f,&p)||f<0||f>3){skipped++;continue;}
    const auto *changed=bc7215_ac_set(12,m,f,KEY_PLUS);
    assert(changed&&bc7215_ac_parse(&t,&m,&f,&p)&&t==12);
    tested++;
  }
  assert(tested>0);
  // Compatibility with existing main pairing preference is deliberate.
  static_assert(sizeof(PairingData)==4420,"PairingData storage layout changed");
  std::cout<<"PASS actual V8.1 library: unknown fan -> LOW recovery x100, real LOW command match, "
           <<tested<<" predefined temperature cases ("<<skipped<<" unsuitable samples skipped); pairing bytes="<<sizeof(PairingData)<<"\n";
}

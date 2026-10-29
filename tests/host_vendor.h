#pragma once
#include "host_framework.h"
#include "bc7215_types.h"
struct bc7215CombinedMsg_t {
  uint16_t bitLen;
  union { struct { const bc7215FormatPkt_t *fmt; const bc7215DataVarPkt_t *datPkt; } msg; } body;
};
#define KEY_PLUS 0
#define KEY_MINUS 1
#define KEY_MODE 2
#define KEY_FAN 3
// Synthetic five-byte protocol. This mock exercises application behavior only;
// the separate vendor test links the unmodified real code library.
inline bc7215DataMaxPkt_t test_base{};
inline bc7215FormatPkt_t test_format{};
inline uint8_t test_status=0;
inline bc7215DataMaxPkt_t test_generated{};
inline bool generate_ok=true, erase_feature=false;
inline bool replace_ok=true, parse_ok=true, init_ok=true;
inline int ir_count=0, sent_t=-1, sent_m=-1, sent_f=-1, sent_feature=-1;
inline bool bc7215_ac_replace_base(uint8_t status,const bc7215DataVarPkt_t *data) {
  test_base={}; test_base.bitLen=data->bitLen;
  test_status=status;
  memcpy(test_base.data,data->data,std::min(size_t(512),size_t((data->bitLen+7)/8)));
  return replace_ok;
}
inline bool bc7215_ac_parse(int8_t *t,int8_t *m,int8_t *f,int8_t *p) {
  const uint8_t invert=(test_status&0x40)?0xFF:0;
  *t=test_base.data[0]^invert; *m=test_base.data[1]^invert;
  *f=test_base.data[2]^invert; *p=test_base.data[3]^invert;
  return parse_ok;
}
inline const bc7215DataVarPkt_t *bc7215_ac_get_base_data(){return reinterpret_cast<bc7215DataVarPkt_t*>(&test_base);}
inline const bc7215FormatPkt_t *bc7215_ac_get_base_fmt(){return &test_format;}
inline const bc7215DataVarPkt_t *bc7215_ac_set(int t,int m,int f,int) {
  if(!generate_ok)return nullptr;
  test_generated=test_base;
  if(test_status&0x40)for(unsigned i=0;i<(test_generated.bitLen+7)/8;i++)test_generated.data[i]=~test_generated.data[i];
  test_generated.data[0]=t;test_generated.data[1]=m;test_generated.data[2]=f;
  if(erase_feature)test_generated.data[4]=0;
  return reinterpret_cast<bc7215DataVarPkt_t*>(&test_generated);
}
inline int bc7215_ac_need_extra_sample(){return 0;}
inline bool bc7215_ac_save_2nd_base(uint8_t,const bc7215CombinedMsg_t*){return true;}
struct BC7215 {
  unsigned receive_resets=0, aborts=0, shutdowns=0;
  bool ack=true;
  bool partial=false;
  bool receiveIncomplete()const{return partial;}
  void abortCommand(){aborts++;}
  void resetReceiveState(){receive_resets++;}
  bool io_error=false;
  BC7215(Stream&,int,int){}
  void setRx(){} void setTx(){} void setShutDown(){shutdowns++;} bool cmdCompleted(){return ack;}
};
struct BC7215AC {
  uint8_t sampleCount=0,sampleStatus[4]{};
  bc7215DataMaxPkt_t sampleData[4]{};
  bc7215FormatPkt_t sampleFormat[4]{};
  bool capture_overflow=false, tx_busy=false;
  BC7215AC(BC7215&){}
  void setCelsius(){} const char *getLibVer(){return "test-lib";}
  void startCapture(){sampleCount=0;tx_busy=false;} void stopCapture(){tx_busy=false;}
  void resumeCapture(){sampleCount=0;capture_overflow=false;}
  void setCaptureProfile(uint8_t,const bc7215DataMaxPkt_t*,const uint8_t*){}
  bool signalCaptured(){return false;} bool isBusy(){return tx_busy;}
  bool init(){test_base=sampleData[0];test_format=sampleFormat[0];test_status=sampleStatus[0];return init_ok;}
  bool matchNext(){return false;}
  bool parse(int &t,int &m,int &f,int &p) {
    if(!bc7215_ac_replace_base(sampleStatus[0],reinterpret_cast<bc7215DataVarPkt_t*>(&sampleData[0])))return false;
    int8_t a,b,c,d;bool ok=bc7215_ac_parse(&a,&b,&c,&d);
    t=a+16;m=b;f=c;p=d;return ok;
  }
  const bc7215DataVarPkt_t *on(){tx_busy=true;ir_count++;return bc7215_ac_get_base_data();}
  const bc7215DataVarPkt_t *off(){tx_busy=true;ir_count++;return bc7215_ac_get_base_data();}
  const bc7215DataVarPkt_t *setTo(int t,int m,int f,int) {
    tx_busy=true;ir_count++;sent_t=t;sent_m=m;sent_f=f;sent_feature=test_base.data[4];
    return bc7215_ac_get_base_data();
  }
};

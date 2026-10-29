"""Test production fingerprint, candidate enumeration/restoration and display."""
from pathlib import Path
import argparse
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default='g++')
p.add_argument('--cxx-arg', action='append', default=[])
p.add_argument('--build-dir', type=Path, required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
component = root / 'components/bc7215a'
header = (component / 'bc7215a_climate.h').read_text(encoding='utf-8')
cpp = (component / 'bc7215a_climate.cpp').read_text(encoding='utf-8')
records = header[header.index('struct Capture'):header.index('struct Settings')]
def extract(name):
    pos = cpp.index('BC7215AClimate::' + name + '(')
    start = cpp.rfind('\n', 0, pos) + 1
    opening = cpp.index('{', pos)
    end, level = opening + 1, 1
    while level:
        level += (cpp[end] == '{') - (cpp[end] == '}')
        end += 1
    return cpp[start:end]

pre = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include "bc7215_types.h"
#include "pairing_fingerprint.h"
using esphome::bc7215a::pairing_fingerprint;
uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
'''
mock = r'''
struct Sensor {std::string value; void publish_state(const char *s){value=s;}};
struct FakeAC {
  int total=3, selected=0, step_ms=0;
  bool init_ok=true, extra_loaded=true;
  bool init(){selected=0; extra_loaded=false; return init_ok;}
  bool matchNext(){now_ms+=step_ms; return ++selected<total;}
};
struct BC7215AClimate {
  PairingData candidate_{};
  FakeAC ac; FakeAC *ac_=&ac;
  uint16_t candidate_count_=0;
  bool candidate_count_limited_=false, restore_ok=true, fault=false;
  int restore_calls=0;
  Sensor match,id; Sensor *match_info_=&match,*pairing_id_=&id;
  void apply_capture_(const Capture &) {}
  bool apply_pairing_(const PairingData &p) {
    restore_calls++; ac.selected=p.match; ac.extra_loaded=p.extra; return restore_ok;
  }
  void fault_(const char *){fault=true;}
  bool count_candidates_();
  void publish_match_();
};
'''
tests = r'''
int main() {
  PairingData p{};
  strcpy(p.version,"V7.2 build2624"); p.magic=0xBC720103;
  p.base.count=1; p.base.data[0].bitLen=16;
  p.base.data[0].data[0]=0x12; p.base.data[0].data[1]=0x34;
  const auto original=pairing_fingerprint(p);
  {auto q=p; assert(pairing_fingerprint(q)==original);}
  {auto q=p; q.magic=0; assert(pairing_fingerprint(q)==original);}
  {auto q=p; q.base.data[0].data[99]=0x89; q.base.data[3].bitLen=123;
   q.second.data[0].data[0]=42; q.version[20]='x'; assert(pairing_fingerprint(q)==original);}
  {auto q=p; q.base.data[0].data[0]^=1; assert(pairing_fingerprint(q)!=original);}
  {auto q=p; q.base.format[0].format[31]=1; assert(pairing_fingerprint(q)!=original);}
  {auto q=p; q.match=1; assert(pairing_fingerprint(q)!=original);}
  {auto q=p; q.version[0]='W'; assert(pairing_fingerprint(q)!=original);}
  {auto q=p; q.extra=true; q.second=p.base; const auto a=pairing_fingerprint(q);
   q.second.data[0].data[0]^=1; assert(pairing_fingerprint(q)!=a);}
  {BC7215AClimate c; c.candidate_=p; c.candidate_.match=1; c.candidate_.extra=true;
   auto before=pairing_fingerprint(c.candidate_);
   assert(c.count_candidates_()); assert(c.candidate_count_==3&&!c.candidate_count_limited_);
   assert(c.ac.selected==1&&c.ac.extra_loaded&&c.restore_calls==1);
   assert(pairing_fingerprint(c.candidate_)==before);
   c.publish_match_(); assert(c.match.value=="候選 02／共 03 個");
   assert(c.id.value.size()==10&&c.id.value.substr(0,2)=="P-");}
  {BC7215AClimate c; c.ac.total=1; assert(c.count_candidates_());
   c.publish_match_(); assert(c.match.value=="候選 01／共 01 個");}
  {BC7215AClimate c; c.ac.total=1000; assert(c.count_candidates_());
   assert(c.candidate_count_==256&&c.candidate_count_limited_&&c.ac.selected==0);
   c.publish_match_(); assert(c.match.value=="候選 01／至少 256 個");}
  {BC7215AClimate c; c.ac.total=100; c.ac.step_ms=500; c.candidate_.match=5;
   assert(c.count_candidates_()); assert(c.candidate_count_limited_&&c.candidate_count_==6&&c.ac.selected==5);}
  {BC7215AClimate c; c.restore_ok=false; assert(!c.count_candidates_()&&c.fault);}
  {BC7215AClimate c; c.ac.init_ok=false; assert(c.count_candidates_()&&c.restore_calls==1);
   c.publish_match_(); assert(c.match.value=="候選 01／總數未知");}
  {BC7215AClimate c; c.candidate_=p; c.count_candidates_(); c.publish_match_();
   const auto id=c.id.value; BC7215AClimate reboot; reboot.candidate_=c.candidate_;
   reboot.count_candidates_(); reboot.publish_match_(); assert(reboot.id.value==id);}
  std::cout<<"PASS: 15 identity/count scenarios; production functions with mocked matching library\n";
}
'''
args.build_dir.mkdir(parents=True, exist_ok=True)
source=args.build_dir/'identity_test.cpp'
exe=args.build_dir/'identity_test.exe'
source.write_text(pre+records+mock+extract('count_candidates_')+extract('publish_match_')+tests, encoding='utf-8')
subprocess.run([args.cxx,*args.cxx_arg,'-std=c++17','-Wall','-Wextra','-I',str(component),str(source),'-o',str(exe)],check=True)
subprocess.run([str(exe.resolve())],check=True)

"""Execute production UART adapter and recovery functions with mocked pins/UART/time."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='g++')
parser.add_argument('--cxx-arg', action='append', default=[])
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = (root/'components/bc7215a/bc7215a_climate.cpp').read_text(encoding='utf-8')
header = (root/'components/bc7215a/bc7215a_climate.h').read_text(encoding='utf-8')

def extract(name):
    start = cpp.index(name)
    start = cpp.rfind('\n', 0, start) + 1
    opening = cpp.index('{', start)
    level, end = 1, opening + 1
    while level:
        level += (cpp[end] == '{') - (cpp[end] == '}')
        end += 1
    return cpp[start:end]

pre = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
uint32_t clock_us=0;
uint32_t millis(){return clock_us/1000;}
uint32_t micros(){return clock_us;}
void delay(uint32_t ms){clock_us+=ms*1000;}
bool pins[10]{};
int digitalRead(int pin){return pins[pin];}
#define LOW 0
#define LOG_STR(x) x
std::vector<std::string> logs;
void log_message(const char*,const char* fmt,...){
 char b[600];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);logs.emplace_back(b);
}
#define ESP_LOGI(...) log_message(__VA_ARGS__)
#define ESP_LOGW(...) log_message(__VA_ARGS__)
#define ESP_LOGD(...) log_message(__VA_ARGS__)
const char* TAG="test";
bool has_log(const std::string &part){for(auto &s:logs)if(s.find(part)!=std::string::npos)return true;return false;}
struct Stream{virtual ~Stream()=default;virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;virtual size_t write(uint8_t)=0;};
namespace uart {
enum class UARTFlushResult {UART_FLUSH_RESULT_SUCCESS,UART_FLUSH_RESULT_TIMEOUT};
struct UARTComponent {
 std::deque<uint8_t> rx;std::vector<uint8_t> tx;
 bool flush_failure=false;
 int available(){return int(rx.size());}
 bool read_byte(uint8_t*c){if(rx.empty())return false;*c=rx.front();rx.pop_front();return true;}
 bool peek_byte(uint8_t*c){if(rx.empty())return false;*c=rx.front();return true;}
 void write_byte(uint8_t c){tx.push_back(c);}
 UARTFlushResult flush(){clock_us+=600;return flush_failure?UARTFlushResult::UART_FLUSH_RESULT_TIMEOUT:UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;}
};
}
'''
adapter = header[header.index('class UARTStream'):header.index('struct Capture')]
adapter_methods = '\n'.join(extract('UARTStream::'+name) for name in
                            ('read()', 'write(', 'flush()', 'drain_raw()', 'log_trace('))
mock = r'''
struct Pin {int n;int get_pin(){return n;}};
struct Sensor{bool value=false;void publish_state(bool v){value=v;}};
struct Driver {
 UARTStream *s;bool io_error=false,complete=false;
 void setRx(){pins[0]=true;}
 void setTx(){pins[0]=false;complete=true;}
 void setShutDown(){s->write(0xf7);s->flush();s->write(0);s->flush();complete=false;}
 bool cmdCompleted(){while(s->available()){int c=s->read();if(!pins[0]&&c==0x7a)complete=true;}return complete;}
};
struct AC {bool capture_overflow=false;int sampleCount=0;
 bool isBusy(){return false;} bool signalCaptured(){return false;}
 void stopCapture(){} void startCapture(){}
 void resumeCapture(){} void setCaptureProfile(int,const int*,const int*){}
};
struct BC7215AClimate {
 enum Phase {PROBE,PAIR,EXTRA,LISTEN,TX_ON,TX_SETTINGS,TX_OFF,FAULT,PROBE_WAKE,PROBE_TX_WAIT,UNPAIRED} phase_=PROBE;
 uart::UARTComponent bus;UARTStream stream{&bus};UARTStream*stream_=&stream;
 Driver driver{&stream};Driver*driver_=&driver;AC ac;AC*ac_=&ac;
 Pin mod{0},busy{1};Pin*mod_=&mod;Pin*busy_=&busy;
 Sensor paired,known;Sensor*paired_=&paired;Sensor*known_sensor_=&known;
 bool pending_=false,known_=false,paired_ok_=false,error=false,restore=false;
 bool matter_command_debounce_=false;uint32_t matter_command_since_=0;
 void recover_failed_tx_(const char*){assert(false && "TX recovery is tested in the full component suite");}
 struct Pair {int marker;struct {int count=0,data[4]{},status[4]{};} base;
   Pair(int n=0):marker(n){} operator int()const{return marker;}};
 using PairingData=Pair;
 Pair saved_=42,candidate_=0;
 struct TextSensor{void publish_state(const char*){}};
 TextSensor *match_info_=nullptr,*pairing_id_=nullptr;
 unsigned rx_recovery_stage_=0;bool rx_watch_=false;
 bool service_rx_recovery_(){return false;} void service_rx_watch_(){}
 uint32_t since_=0,probe_rx_start_=0,probe_tx_start_=0,diagnostic_since_=0;
 std::string status;
 BC7215AClimate(){clock_us=0;pins[0]=pins[1]=false;logs.clear();}
 void status_set_error(const char*){error=true;}
 void status_clear_error(){error=false;}
 void status_message_(const char*m){status=m;}
 bool apply_pairing_(int){return restore;}
 void publish_match_(){} void listen_(){phase_=LISTEN;driver.setRx();}
 bool count_candidates_(){return true;}
 void start_pairing(){phase_=PAIR;driver.setRx();}
 void send_settings_(){} void finish_command_(){} void received_(){} void begin_command_(){}
 void loop();void begin_probe_();void retry_communication();void fault_(const char*);void log_diagnostics_();
 void advance(uint32_t ms){delay(ms);loop();}
 void send_probe(){begin_probe_();advance(50);advance(50);assert(phase_==PROBE);}
};
'''
methods = '\n'.join(extract('BC7215AClimate::'+name) for name in
                    ('fault_(', 'log_diagnostics_(', 'begin_probe_(', 'retry_communication(', 'loop('))
tests = r'''
int main(){
 {BC7215AClimate c;c.begin_probe_();assert(c.bus.tx.empty()&&pins[0]);c.advance(49);assert(c.bus.tx.empty());
  c.advance(1);assert(!pins[0]&&c.bus.tx.empty());c.advance(49);assert(c.bus.tx.empty());c.advance(1);
  assert((c.bus.tx==std::vector<uint8_t>{0xf7,0}));assert(c.stream.write_delta_us==600);}
 {BC7215AClimate c;c.restore=true;c.send_probe();c.bus.rx.push_back(0x7a);c.loop();
  assert(c.phase_==c.LISTEN&&c.paired_ok_&&c.paired.value&&!c.known_&&c.candidate_==42);}
 {BC7215AClimate c;c.send_probe();c.advance(1501);assert(c.phase_==c.FAULT&&pins[0]&&c.error);
  assert(c.status.find("未收到UART")!=std::string::npos);c.bus.rx={0x12,0x34,0x7a};c.loop();
  assert(c.stream.rx_count==3&&c.phase_==c.FAULT);
  c.retry_communication();assert(c.phase_==c.PROBE_WAKE&&!c.error&&c.saved_==42);
  c.advance(50);c.advance(50);c.bus.rx={0x7a};c.loop();assert(c.phase_==c.UNPAIRED);}
 {BC7215AClimate c;c.send_probe();c.bus.rx={0x55};c.loop();c.advance(1501);
  assert(c.phase_==c.FAULT&&c.status.find("未確認7A")!=std::string::npos);}
 {BC7215AClimate c;c.begin_probe_();c.bus.rx={0x7a};c.advance(50);c.advance(50);c.advance(1501);
  assert(c.phase_==c.FAULT&&c.status.find("未收到UART")!=std::string::npos);}
 {BC7215AClimate c;c.send_probe();c.driver.io_error=true;c.loop();
  assert(c.phase_==c.FAULT&&pins[0]&&c.status.find("BUSY")!=std::string::npos);
  c.retry_communication();assert(!c.driver.io_error&&c.phase_==c.PROBE_WAKE);}
 {BC7215AClimate c;c.bus.flush_failure=true;c.send_probe();c.loop();
  assert(c.phase_==c.FAULT&&pins[0]&&c.status.find("UART")!=std::string::npos);
  assert(c.bus.tx.size()==1);c.bus.flush_failure=false;c.retry_communication();
  assert(!c.stream.io_error&&c.phase_==c.PROBE_WAKE);}
 {BC7215AClimate c;for(auto p:{c.PROBE,c.PAIR,c.EXTRA,c.LISTEN,c.TX_ON,c.TX_SETTINGS,c.TX_OFF,c.PROBE_WAKE,c.PROBE_TX_WAIT}){
  c.phase_=p;c.retry_communication();assert(c.phase_==p&&c.bus.tx.empty());}}
 {BC7215AClimate c;c.pending_=c.known_=c.paired_ok_=true;c.fault_("test");
  assert(!c.pending_&&!c.known_&&!c.paired_ok_&&!c.paired.value&&c.saved_==42&&pins[0]);}
 {BC7215AClimate c;for(int i=0;i<300;i++)c.bus.rx.push_back(uint8_t(i));
  while(c.stream.available())c.stream.read();c.stream.log_trace();
  assert(c.stream.rx_count==300&&has_log("omitted 44 bytes"));}
 {BC7215AClimate c;c.begin_probe_();c.advance(50);pins[0]=true;c.advance(50);
  assert(c.phase_==c.FAULT&&c.bus.tx.empty()&&pins[0]);}
 std::cout<<"PASS: 11 communication/adapter scenarios using production functions; mocked hardware\n";
}
'''
args.build_dir.mkdir(parents=True, exist_ok=True)
source = args.build_dir/'communication_test.cpp'
exe = args.build_dir/'communication_test.exe'
source.write_text(pre+adapter+adapter_methods+mock+methods+tests, encoding='utf-8')
subprocess.run([args.cxx, *args.cxx_arg, '-std=c++17', '-Wall', '-Wextra', str(source), '-o', str(exe)], check=True)
subprocess.run([str(exe.resolve())], check=True)

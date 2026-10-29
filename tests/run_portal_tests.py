"""Compile production Portal with upstream save_wifi_sta lifecycle and HTTP doubles.

No hardware, radio, or network is accessed. The ESPHome method bodies are read
from the same local runtime used for the release build, not reimplemented here.
"""
from pathlib import Path
import argparse
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--cxx',required=True)
p.add_argument('--build-dir',type=Path,required=True)
p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
p.add_argument('--esphome-root',type=Path,required=True,help='Path to the ESPHome Python package directory containing components/wifi')
a=p.parse_args()
b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
portal=(a.source/'components/bc7215a_web').resolve()
upstream=(a.esphome_root/'components/wifi/wifi_component.cpp').read_text(encoding='utf-8')

def extract(signature):
    start=upstream.index(signature)
    end=upstream.index('{',start)+1;depth=1
    while depth:
        depth+=(upstream[end]=='{')-(upstream[end]=='}');end+=1
    return upstream[start:end]

host=r'''
#pragma once
#include <cassert>
#include <cstring>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#define ESP_LOGD(...)
enum {HTTP_GET,HTTP_POST};
struct AsyncWebServerResponse {int status;std::string content_type,body;std::map<std::string,std::string>headers;void addHeader(std::string k,std::string v){headers[k]=v;}};
struct AsyncWebServerRequest {
 static constexpr int URL_BUF_SIZE=128;
 std::string url;int method_=HTTP_GET,status=0;
 std::map<std::string,std::string>headers,args;
 AsyncWebServerResponse response{};
 std::string_view url_to(char*){return url;}
 int method(){return method_;}
 std::optional<std::string>get_header(std::string k){if(!headers.count(k))return{};return headers[k];}
 std::string arg(std::string k){return args.count(k)?args[k]:"";}
 AsyncWebServerResponse*beginResponse(int s,std::string type,std::string body){response={s,type,body,{}};return &response;}
 void send(AsyncWebServerResponse*r){status=r->status;}
 void send(int s,const char*type="",const char*body=""){status=s;response={s,type,body,{}};}
};
struct AsyncWebHandler {virtual ~AsyncWebHandler()=default;virtual bool canHandle(AsyncWebServerRequest*)const=0;virtual void handleRequest(AsyncWebServerRequest*)=0;};
struct JsonArray;
struct JsonValue {template<typename T>JsonValue&operator=(const T&){return *this;}template<typename T>T to(){return {};}};
struct JsonObject {JsonValue operator[](const char*){return {};}};
struct JsonArray {template<typename T>T add(){return {};}};
namespace esphome {
namespace setup_priority {constexpr float WIFI=250.0f;}
struct Component {
 std::map<std::string,std::function<void()>>timeouts;
 std::map<std::string,uint32_t>delays;
 virtual ~Component()=default;virtual void setup(){}virtual float get_setup_priority()const{return 0;}
 void set_timeout(std::string name,uint32_t ms,std::function<void()>fn){timeouts[name]=std::move(fn);delays[name]=ms;}
 void run_timeout(std::string name){auto fn=std::move(timeouts.at(name));timeouts.erase(name);fn();}
};
struct Application {std::string get_name(){return "bc7215a-c3-aabbcc";}};
inline Application App;
namespace web_server_base {struct WebServerBase {void add_handler(AsyncWebHandler*){}};}
namespace web_server {struct WebServer {void handleRequest(AsyncWebServerRequest*r){r->send(200);}};}
namespace json {struct JsonBuilder {JsonObject root(){return {};}std::string serialize(){return "{}";}};}
namespace wifi {
enum {WIFI_COMPONENT_STATE_CONNECTED,WIFI_COMPONENT_STATE_CONNECTING,WIFI_COMPONENT_STATE_COOLDOWN,WIFI_COMPONENT_STATE_DISABLED};
struct SavedWifiSettings {char ssid[33];char password[65];};
struct WiFiAP {std::string ssid,password;void set_ssid(const char*s){ssid=s;}void set_password(const char*s){password=s;}};
struct Pref {SavedWifiSettings saved{};void save(const SavedWifiSettings*s){saved=*s;}};
struct WiFiScanResult {bool get_is_hidden()const{return false;}std::string get_ssid()const{return "fixture";}int get_rssi()const{return -45;}bool get_with_auth()const{return true;}};
struct WiFiComponent {
 int state_=WIFI_COMPONENT_STATE_CONNECTED,selected_sta_index_=0,starts=0,retries=0,disables=0,enables=0;
 bool ap_active=false,skip_cooldown_next_cycle_=false;
 std::string actual_ssid="Old Wi-Fi",requested_ssid,requested_password;
 std::vector<std::string>calls;
 std::vector<WiFiAP>sta_;std::vector<WiFiScanResult>scans;
 Pref pref_;
 bool is_connected(){return state_==WIFI_COMPONENT_STATE_CONNECTED;}
 bool is_ap_active(){return ap_active;}
 void clear_sta(){sta_.clear();selected_sta_index_=-1;}
 void init_sta(size_t n){sta_.reserve(n);}
 void add_sta(const WiFiAP&ap){sta_.push_back(ap);}
 WiFiAP get_sta(){return sta_.at(selected_sta_index_);}
 auto&get_scan_result(){return scans;}
 void disable(){calls.push_back("disable");disables++;state_=WIFI_COMPONENT_STATE_DISABLED;ap_active=false;actual_ssid.clear();}
 void enable(){calls.push_back("enable");enables++;start_connecting(get_sta());}
 void retry_connect(){retries++;start_connecting(get_sta());}
 void start_connecting(const WiFiAP&ap){calls.push_back("connect");starts++;requested_ssid=ap.ssid;requested_password=ap.password;state_=WIFI_COMPONENT_STATE_CONNECTING;actual_ssid.clear();}
 void set_sta(const WiFiAP&ap);
 void save_wifi_sta(const char*ssid,const char*password);
 void connect_soon_();
};
inline WiFiComponent*global_wifi_component=nullptr;
struct ScanResultsLock {explicit ScanResultsLock(WiFiComponent*){}};
}
struct Preferences {int syncs=0;void sync(){syncs++;}};
inline Preferences preferences;
inline Preferences*global_preferences=&preferences;
}
'''

(b/'host.h').write_text(host,encoding='utf-8')
for rel in ('core/component.h','core/application.h','components/web_server_base/web_server_base.h','components/web_server/web_server.h','components/wifi/wifi_component.h','components/json/json_util.h'):
    dest=b/'esphome'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text('#pragma once\n#include "host.h"\n')

methods='\nnamespace esphome::wifi {\n'+ '\n'.join(extract(sig) for sig in (
    'void WiFiComponent::set_sta(const WiFiAP &ap)',
    'void WiFiComponent::save_wifi_sta(const char *ssid, const char *password)',
    'void WiFiComponent::connect_soon_()'))+'\n}\n'

tests=r'''
using namespace esphome;
AsyncWebServerRequest save_request(std::string ssid="New Wi-Fi",std::string password="test12345") {
 AsyncWebServerRequest r;r.url="/provision/save";r.method_=HTTP_POST;
 r.headers={{"X-BC7215-Request","1"},{"Origin","http://bc7215a-c3-aabbcc.local"},{"Host","bc7215a-c3-aabbcc.local"}};
 r.args={{"ssid",ssid},{"psk",password}};return r;
}
int main(){
 web_server_base::WebServerBase base;web_server::WebServer server;
 bc7215a_web::Portal portal(&base,&server);
 wifi::WiFiComponent network;wifi::global_wifi_component=&network;
 // Prove the regression with the *upstream* save implementation first:
 network.save_wifi_sta("Stored only","test12345");
 assert(network.actual_ssid=="Old Wi-Fi"&&network.starts==0&&network.is_connected());
 auto request=save_request();assert(portal.canHandle(&request));portal.handleRequest(&request);
 assert(request.status==200&&network.starts==0&&network.is_connected()); // HTTP completes before disconnect.
 assert(portal.delays.at("save_wifi")>=300);
 portal.run_timeout("save_wifi");
 assert(network.disables==1&&network.enables==1&&network.starts==1&&!network.is_connected());
 assert(network.requested_ssid=="New Wi-Fi"&&network.requested_password=="test12345");
 assert(std::string(network.pref_.saved.ssid)=="New Wi-Fi");
 assert((network.calls==std::vector<std::string>{"disable","enable","connect"}));

 // AP provisioning must not switch the fallback radio off, including failed
 // credentials or an already in-flight connection to a previous SSID.
 network=wifi::WiFiComponent{};network.ap_active=true;network.state_=wifi::WIFI_COMPONENT_STATE_CONNECTING;
 request=save_request("Provisioned Wi-Fi");portal.handleRequest(&request);assert(request.status==200);
 portal.run_timeout("save_wifi");
 assert(network.ap_active&&network.disables==0&&network.enables==0&&network.starts==1);
 assert(network.requested_ssid=="Provisioned Wi-Fi");
 // Leave the failed attempt unconnected; AP remains available for corrections.
 assert(!network.is_connected()&&network.ap_active);
 request=save_request("Corrected Wi-Fi");portal.handleRequest(&request);portal.run_timeout("save_wifi");
 assert(network.ap_active&&network.starts==2&&network.requested_ssid=="Corrected Wi-Fi");
 network=wifi::WiFiComponent{};network.ap_active=true;network.state_=wifi::WIFI_COMPONENT_STATE_COOLDOWN;
 request=save_request("From cooldown");portal.handleRequest(&request);portal.run_timeout("save_wifi");
 assert(network.ap_active&&network.starts==1&&network.retries==0&&network.disables==0);
 assert(std::string(network.pref_.saved.ssid)=="From cooldown");

 // Rapid submissions replace the same deferred job; only the last one applies.
 network=wifi::WiFiComponent{};
 request=save_request("First");portal.handleRequest(&request);
 request=save_request("Last");portal.handleRequest(&request);assert(portal.timeouts.size()==1);
 portal.run_timeout("save_wifi");assert(network.starts==1&&network.requested_ssid=="Last");
 // No response falsely claims a connection was established.
 assert(request.response.body=="{\"accepted\":true}");

 // Rejected submissions must never schedule a radio change or persist values.
 for(int which=0;which<6;which++){
  request=save_request();
  if(which==0)request.headers.erase("X-BC7215-Request");
  if(which==1)request.headers["Origin"]="http://unrelated.example";
  if(which==2)request.method_=HTTP_GET;
  if(which==3)request.args["ssid"]="";
  if(which==4)request.args["psk"]="short";
  if(which==5)request.args["ssid"]=std::string(33,'x');
  portal.handleRequest(&request);assert((request.status==400||request.status==403)&&portal.timeouts.empty());
 }
 request=save_request("Open network","");portal.handleRequest(&request);assert(request.status==200);portal.run_timeout("save_wifi");
 assert(network.requested_ssid=="Open network"&&network.requested_password.empty());
 std::cout<<"PASS production Portal: upstream save-only regression, delayed connected reconnect, AP preserved on retries, last submission wins, accepted-only response, origin/marker/length validation and open network\n";
}
'''
(b/'main.cpp').write_text('#include "portal.h"\n'+methods+tests,encoding='utf-8')
exe=b/'portal_test.exe'
subprocess.run([a.cxx,'c++','-std=c++17','-I'+str(b),'-I'+str(portal),str(portal/'portal.cpp'),str(b/'main.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

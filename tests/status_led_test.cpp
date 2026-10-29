#include "status_led.h"
#include <cassert>
#include <iostream>
using namespace esphome::bc7215a;
int main(){
 for(int bits=0;bits<8;bits++){
  const bool p=bits&1,a=bits&2,w=bits&4;
  auto mode=status_led_mode(p,a,w);
  auto want=p?StatusLedMode::REMOTE_PAIR:a?StatusLedMode::AP:!w?StatusLedMode::WIFI_OFF:StatusLedMode::ONLINE;
  assert(mode==want);
 }
 struct Test {StatusLedMode mode;int color;bool blink;};
 for(auto t:{Test{StatusLedMode::WIFI_OFF,4,false},Test{StatusLedMode::AP,4,true},Test{StatusLedMode::REMOTE_PAIR,1,true},Test{StatusLedMode::ONLINE,2,false}}){
  assert(status_led_color(t.mode,0)==t.color&&status_led_color(t.mode,499)==t.color);
  assert(status_led_color(t.mode,500)==(t.blink?0:t.color));
  assert(status_led_color(t.mode,1000)==t.color);
 }
 std::cout<<"PASS RGB: four states, all 8 priority combinations, 500 ms blink boundary\n";
}

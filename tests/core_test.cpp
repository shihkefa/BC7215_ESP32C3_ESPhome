#include "bc7215a_climate.h"
#include "pairing_fingerprint.h"
#include <iostream>
using namespace esphome;
using namespace esphome::bc7215a;
struct Fixture {
  uart::UARTComponent bus;
  UARTStream stream{&bus}; BC7215 driver{stream,0,1}; BC7215AC ac{driver};
  text_sensor::TextSensor status;
  BC7215AClimate c;
  InternalGPIOPin mod,busy;
  Fixture() {
    ir_count=0;replace_ok=parse_ok=init_ok=true;preference_save_ok=preference_sync_ok=true;
    generate_ok=true;erase_feature=false;
    c.ac_=&ac;c.driver_=&driver;c.stream_=&stream;c.status_=&status;
    c.mod_=&mod;c.busy_=&busy;
    c.candidate_.magic=0xBC720103;strcpy(c.candidate_.version,"test-lib");
    c.candidate_.base.count=1;c.candidate_.base.status[0]=0x12;
    auto &pkt=c.candidate_.base.data[0];pkt.bitLen=40;
    pkt.data[0]=9;pkt.data[1]=1;pkt.data[2]=0;pkt.data[3]=1;pkt.data[4]=0;
    c.saved_=c.candidate_;c.paired_ok_=true;c.current_.power=true;c.known_=true;
    c.pref_=global_preferences->make_preference<PairingData>(0xBC720103);
    assert(c.apply_pairing_(c.candidate_));c.listen_();
  }
  void sample(int feature,int t=25,int mode=1,int fan=0,int power=1) {
    ac.sampleCount=1;ac.sampleStatus[0]=0x12;
    ac.sampleData[0]={};ac.sampleData[0].bitLen=40;
    auto *d=ac.sampleData[0].data;d[0]=t-16;d[1]=mode;d[2]=fan;d[3]=power;d[4]=feature;
    c.received_();
  }

};
int main() {
{Fixture x;x.c.current_.power=false;x.c.desired_.power=true;x.c.begin_command_();
   assert(x.c.phase_==x.c.TX_ON&&ir_count==1);
   x.ac.tx_busy=false;test_time+=100;const auto ack_at=test_time;x.c.loop();
   assert(x.c.phase_==x.c.TX_SETTINGS&&ir_count==2&&test_time-ack_at>=40);
   x.ac.tx_busy=false;test_time+=3001;x.c.loop(); // Available ACK beats deadline.
   assert(x.c.phase_==x.c.LISTEN&&x.c.known_);}
{Fixture x;
   x.c.current_.mode=1;assert(x.c.matter_running_mode()==3&&x.c.matter_running_state()==2);
   x.c.current_.mode=2;assert(x.c.matter_running_mode()==4&&x.c.matter_running_state()==1);
   x.c.current_.mode=0;assert(x.c.matter_running_mode()==0&&x.c.matter_running_state()==0);
   x.c.current_.mode=4;assert(x.c.matter_running_state()==4);
   x.c.known_=false;assert(x.c.matter_running_state()==0&&x.c.matter_running_mode()==0);
   x.c.known_=true;x.c.current_.power=false;assert(x.c.matter_running_state()==0);}
{Fixture x;x.c.phase_=x.c.PROBE;x.c.loop();
   assert(x.c.phase_==x.c.LISTEN&&x.c.paired_ok_&&!x.c.is_pairing()&&ir_count==0);}
{Fixture x;
   for(uint8_t value: {1,2,3,4,5,6}){x.c.matter_set_fan_mode(value);
    assert(x.c.pending_&&x.c.desired_.fan==(value==4?3:value>=5?0:value));
    x.c.begin_command_();x.c.finish_command_();
    assert(x.c.matter_fan_mode()==(value==4?3:value>=5?5:value));}
   x.c.matter_set_fan_mode(9);assert(!x.c.pending_);
   x.c.matter_set_fan_mode(0);assert(x.c.pending_&&!x.c.desired_.power);
   x.c.begin_command_();x.c.finish_command_();assert(x.c.matter_fan_mode()==0);
   const int before=ir_count;x.c.matter_set_fan_mode(1);x.c.begin_command_();
   assert(ir_count==before&&!x.c.current_.power&&x.c.current_.fan==1);}
{Fixture x;x.c.publish_settings_();assert(x.c.matter_system_mode()==3);
   x.c.matter_set_target_temperature(2850);assert(x.c.pending_&&x.c.desired_.temperature==29);
   x.c.begin_command_();x.c.finish_command_();assert(x.c.matter_target_temperature_centi()==2900);
   x.c.matter_set_system_mode(4);assert(x.c.pending_&&x.c.desired_.mode==2);
   x.c.begin_command_();x.c.finish_command_();assert(x.c.matter_system_mode()==4);
   x.c.matter_set_power(false);assert(x.c.pending_&&!x.c.desired_.power);
   x.c.begin_command_();x.c.finish_command_();assert(x.c.matter_system_mode()==0);
   x.c.matter_set_power(true);assert(x.c.pending_&&x.c.desired_.power&&x.c.desired_.mode==2);}
{Fixture x;x.c.arm_rx_watch_();x.c.rx_recovery_attempts_=254;
   for(int i=0;i<4;i++) {
     test_time+=5000;x.c.loop();test_time+=40;x.c.loop();x.c.loop();
   }
   assert(x.driver.aborts==4&&x.c.rx_recovery_attempts_==255&&ir_count==0);
   parse_ok=true;x.sample(0);assert(x.c.rx_watch_);
   // Frequent actual UART activity defers recovery, even after a successful parse.
   for(int i=0;i<30;i++){test_time+=500;x.stream.rx_count++;x.c.loop();}
   assert(x.driver.aborts==4);
   test_time+=1500;x.c.loop();assert(x.driver.aborts==5);
  }
{Fixture x;x.driver.ack=false;x.c.recover_reception();
   test_time+=40;x.c.loop();test_time+=499;x.c.loop();
   assert(x.c.rx_recovery_stage_==2);
   test_time++;x.c.loop();assert(!x.c.rx_recovery_stage_&&x.c.rx_watch_&&ir_count==0);
   assert(x.status.state.find("未確認回覆")!=std::string::npos);
  }
{Fixture x;test_time=0xfffffff0U;
   parse_ok=false;
   x.c.arm_rx_watch_();const auto since=x.c.rx_watch_since_;
   test_time+=500;x.sample(7);assert(x.c.rx_watch_since_==since);
   test_time+=1000;x.c.loop();assert(x.driver.aborts==1); // millis wraparound
  }
{Fixture x;parse_ok=false;
   for(int i=0;i<20;i++){x.sample(7);test_time+=2000;x.c.loop();}
   assert(x.driver.aborts==0&&!x.c.rx_watch_&&x.c.known_); // Unmatched remote is filtered, not reset.
   parse_ok=true;x.sample(0);x.driver.partial=true;x.stream.rx_count++;
   x.c.loop();test_time+=1500;x.c.loop();assert(x.c.rx_watch_&&x.driver.aborts==0);
   test_time+=1500;x.c.loop();assert(x.driver.aborts==1); // New partial stall after success.
  }
{Fixture x;x.c.arm_rx_watch_();
   for(int i=0;i<20;i++){test_time+=500;x.stream.rx_count++;x.c.loop();}
   assert(x.driver.aborts==0); // Continuous data: keep receiving, no hardware interruption.
   test_time+=1499;x.c.loop();assert(x.driver.aborts==0);
   test_time++;x.c.loop();assert(x.driver.aborts==1); // Quiet window reached.
  }
{Fixture x;
   // A healthy idle receiver must never be reset just because nobody pressed a remote.
   test_time+=60000;x.c.loop();assert(x.driver.aborts==0);
  }
{Fixture x;parse_ok=false;x.sample(7);x.c.pending_=true;
   test_time+=2000;x.c.service_rx_watch_();assert(x.driver.aborts==0);
   x.c.recover_reception();assert(x.driver.aborts==0);
   x.c.pending_=false;parse_ok=true;x.sample(0);test_time+=2000;x.c.loop();
   assert(!x.c.rx_watch_&&x.driver.aborts==0); // No ERR/partial incident means no automatic recovery needed
   x.c.phase_=x.c.PAIR;x.c.recover_reception();assert(x.driver.aborts==0);
  }
{Fixture x;init_ok=false;x.c.recover_reception();
   assert(x.c.phase_==x.c.FAULT&&ir_count==0&&!x.c.paired_ok_);
   std::cout<<"RX watchdog: silent stream, ERR isolation, persistent retries, ACK timeout, wraparound, phase guards, preserved state and no IR passed\n";
  }
{Fixture x;
   const auto fingerprint=pairing_fingerprint(x.c.candidate_);
   parse_ok=false;x.sample(9,28);
   assert(x.c.phase_==x.c.LISTEN&&x.c.paired_ok_&&x.c.known_);
   assert(x.c.rx_parse_failure_streak_==1&&x.c.rx_parse_failures_==1);
   assert(test_base.data[0]==9&&test_base.data[4]==0); // Original 25C sample restored.
   assert(pairing_fingerprint(x.c.candidate_)==fingerprint&&ir_count==0);
   parse_ok=true;x.sample(0,27);
   assert(x.c.current_.temperature==27&&x.c.known_&&x.c.rx_parse_failure_streak_==0);
   assert(x.c.rx_parse_failures_==1&&ir_count==0);
   replace_ok=false;x.sample(7,29);
   assert(x.c.phase_==x.c.LISTEN&&test_base.data[4]==0&&ir_count==0);
   replace_ok=true;x.sample(0,26);
   assert(x.c.current_.temperature==26&&x.c.known_&&ir_count==0);
  }
{Fixture x;parse_ok=false;init_ok=false;x.sample(8);
   assert(x.c.phase_==x.c.FAULT&&!x.c.paired_ok_&&!x.c.known_&&ir_count==0);
  }
{Fixture x;
   for(int i=0;i<100;i++) {
     parse_ok=false;x.sample(7);assert(x.c.phase_==x.c.LISTEN);
     parse_ok=true;x.sample(0,26);assert(x.c.known_&&x.c.current_.temperature==26);
   }
   assert(x.c.rx_parse_failures_==100&&x.c.rx_parse_failure_streak_==0&&ir_count==0);
   std::cout<<"RX recovery: parse/replace failure rollback, next-frame recovery, fault preservation, 100 cycles passed\n";
  }
{Fixture x; x.c.known_=true; x.c.matter_set_system_mode(4);
   test_time+=300; x.c.matter_set_target_temperature(2850); x.c.loop();
   assert(ir_count==0&&x.c.pending_);test_time+=699;x.c.loop();assert(ir_count==0);
   test_time+=1;x.c.loop();assert(ir_count==1&&sent_t==29&&x.c.active_.mode==2);}
{Fixture x; x.c.known_=true; x.c.matter_set_target_temperature(2450);
   assert(x.c.desired_.temperature==24);climate::ClimateCall c;c.t=27;x.c.control(c);
   x.c.loop();assert(ir_count==1&&sent_t==27);}
{Fixture x; x.c.known_=true; climate::ClimateCall call;call.t=28;x.c.control(call);x.c.begin_command_();
   const int sent=ir_count;test_time+=3001;x.c.loop();
   assert(x.c.paired_ok_&&!x.c.known_&&!x.c.pending_&&x.c.phase_==x.c.LISTEN&&x.c.rx_recovery_stage_==1);
   test_time+=40;x.c.loop();x.c.loop();assert(ir_count==sent&&x.c.paired_ok_);}
{Fixture x;
   // A second client change during an active command inherits that active
   // command, and completing it must not overwrite the queued follow-up.
   climate::ClimateCall fan;fan.f=climate::CLIMATE_FAN_LOW;x.c.control(fan);
   x.c.begin_command_();assert(x.c.phase_==x.c.TX_SETTINGS&&sent_f==1&&ir_count==1);
   climate::ClimateCall temp;temp.t=29;x.c.control(temp);
   assert(x.c.pending_&&x.c.active_.temperature==25&&x.c.desired_.temperature==29&&x.c.desired_.fan==1);
   x.ac.tx_busy=false;x.c.loop();
   assert(x.c.phase_==x.c.LISTEN&&x.c.pending_&&x.c.current_.temperature==25&&x.c.current_.fan==1);
   x.c.loop();assert(x.c.phase_==x.c.TX_SETTINGS&&sent_t==29&&sent_f==1&&ir_count==2);
   x.ac.tx_busy=false;x.c.loop();
   assert(!x.c.pending_&&x.c.current_.temperature==29&&x.c.current_.fan==1&&x.c.known_);
  }
{Fixture x;
   // A later explicit OFF remains queued throughout the ON/settings sequence.
   x.c.current_.power=false;
   climate::ClimateCall on;on.m=climate::CLIMATE_MODE_COOL;x.c.control(on);x.c.begin_command_();
   assert(x.c.phase_==x.c.TX_ON&&ir_count==1);
   climate::ClimateCall off;off.m=climate::CLIMATE_MODE_OFF;x.c.control(off);
   assert(x.c.pending_&&!x.c.desired_.power&&x.c.explicit_off_);
   x.ac.tx_busy=false;x.c.loop();assert(x.c.phase_==x.c.TX_SETTINGS&&x.c.pending_&&ir_count==2);
   x.ac.tx_busy=false;x.c.loop();assert(x.c.current_.power&&x.c.pending_&&!x.c.desired_.power);
   x.c.loop();assert(x.c.phase_==x.c.TX_OFF&&ir_count==3&&!x.c.pending_);
   x.ac.tx_busy=false;x.c.loop();assert(!x.c.current_.power&&x.c.known_&&ir_count==3);
  }
{Fixture x; x.c.recover_reception();test_time+=40;x.c.loop();x.c.loop();
   const auto first=x.status.publications;
   const auto fingerprint=pairing_fingerprint(x.c.candidate_);
   for(int i=0;i<10;i++){test_time+=5000;x.c.loop();test_time+=40;x.c.loop();x.c.loop();}
   assert(x.status.publications==first&&x.driver.aborts==11&&ir_count==0);
   assert(pairing_fingerprint(x.c.candidate_)==fingerprint);
   x.driver.ack=false;test_time+=5000;x.c.loop();test_time+=40;x.c.loop();test_time+=500;x.c.loop();
   assert(x.status.publications==first+1);
   x.driver.ack=true;test_time+=5000;x.c.loop();test_time+=40;x.c.loop();x.c.loop();
   assert(x.status.publications==first+2&&ir_count==0);}
  {Fixture x;const auto before=pairing_fingerprint(x.c.candidate_);
   x.c.next_match();assert(x.c.phase_==x.c.LISTEN&&x.c.paired_ok_&&ir_count==0);
   assert(pairing_fingerprint(x.c.candidate_)==before);}
  for(int invalid=0;invalid<3;invalid++) {
    Fixture x;
    if(invalid==0)x.c.saved_=PairingData{};
    if(invalid==1)strcpy(x.c.saved_.version,"old-library");
    if(invalid==2)x.c.saved_.base.count=0;
    x.c.phase_=x.c.PROBE;x.c.loop();
    assert(x.c.phase_==x.c.UNPAIRED&&!x.c.is_pairing()&&!x.c.paired_ok_&&!x.c.known_);
    assert(status_led_mode(x.c.is_pairing(),false,false)==StatusLedMode::WIFI_OFF);
    assert(status_led_mode(x.c.is_pairing(),true,false)==StatusLedMode::AP);
    for(int n=0;n<10;n++){test_time+=1000;x.c.loop();}
    assert(x.c.phase_==x.c.UNPAIRED&&ir_count==0);
    x.c.start_pairing();assert(x.c.phase_==x.c.PAIR&&x.c.is_pairing());
    x.c.cancel_pairing();assert(x.c.phase_==x.c.UNPAIRED&&!x.c.is_pairing()&&ir_count==0);
  }
  {Fixture x;
   x.c.current_.fan=3;x.sample(1,25,1,-1);
   assert(x.c.phase_==x.c.LISTEN&&x.c.paired_ok_&&!x.c.known_&&x.c.current_.fan==3&&ir_count==0);
   assert(x.status.state.find("部分設定無法辨識")!=std::string::npos);
   x.sample(0,26,1,1);
   assert(x.c.phase_==x.c.LISTEN&&x.c.paired_ok_&&x.c.known_&&x.c.current_.fan==1&&ir_count==0);
   // Repeated remote updates remain RX-only, including opaque unsupported bits.
   for(int i=0;i<100;i++)x.sample(i%4,20+i%11,1,i%4);
   assert(x.c.phase_==x.c.LISTEN&&ir_count==0);
  }
  {Fixture x;
   assert(x.c.traits().swings.empty());
   climate::ClimateCall removed;removed.s=climate::CLIMATE_SWING_VERTICAL;x.c.control(removed);
   assert(!x.c.pending_&&ir_count==0); // Stale clients cannot send a removed feature.
   climate::ClimateCall low;low.f=climate::CLIMATE_FAN_LOW;x.c.control(low);
   x.c.begin_command_();assert(ir_count==1&&sent_f==1);
   x.c.finish_command_();assert(x.c.current_.fan==1&&x.c.known_);
  }
  {Fixture x;x.c.matter_set_target_temperature(2800);
   assert(x.c.pending_&&x.c.matter_command_debounce_);const auto since=x.c.matter_command_since_;
   climate::ClimateCall unsupported;unsupported.s=climate::CLIMATE_SWING_VERTICAL;
   x.c.control(unsupported);
   assert(x.c.pending_&&x.c.matter_command_debounce_&&x.c.matter_command_since_==since);
   test_time+=699;x.c.loop();assert(ir_count==0);
   test_time++;x.c.loop();assert(ir_count==1&&sent_t==28);
  }
  {Fixture x;x.c.current_.power=false;
   climate::ClimateCall set;set.t=28;x.c.control(set);
   assert(!x.c.pending_&&!x.c.current_.power&&x.c.current_.temperature==28&&ir_count==0);}
  {Fixture x;const auto pairing=x.c.candidate_;
   x.c.pref_.save(&pairing);assert(preferences[0xBC720103].size()==sizeof(PairingData));
   // A legacy learned-sample preference is not read or changed by startup.
   preferences[0xBC720106]={0xff,0x33,0x81};const auto legacy=preferences[0xBC720106];
   BC7215AClimate reboot;InternalGPIOPin mod,busy;reboot.parent_=&x.bus;reboot.mod_=&mod;reboot.busy_=&busy;
   reboot.setup();assert(memcmp(&pairing,&reboot.saved_,sizeof(pairing))==0);
   assert(preferences[0xBC720106]==legacy&&ir_count==0);
  }

{Fixture x;
   const auto fingerprint=pairing_fingerprint(x.c.candidate_);
   const auto base=test_base;
   x.ac.sampleCount=1;x.ac.sampleStatus[0]=0xB5;x.ac.sampleData[0].bitLen=9;
   x.c.received_();
   assert(x.c.rx_rejected_==1&&x.c.rx_parse_failures_==0&&x.c.known_);
   assert(!memcmp(&base,&test_base,sizeof(base))&&x.driver.aborts==0);
   // No UART packets arrive after the error: the background loop must recover.
   test_time+=1499;x.c.loop();assert(x.driver.aborts==0);
   test_time++;x.c.loop();assert(x.driver.aborts==1&&x.c.rx_recovery_stage_==1);
   x.c.start_pairing();x.c.next_match();
   assert(x.c.phase_==x.c.LISTEN&&pairing_fingerprint(x.c.candidate_)==fingerprint);
   test_time+=39;x.c.loop();assert(x.driver.shutdowns==0);
   test_time++;x.c.loop();assert(x.driver.shutdowns==1&&x.c.rx_recovery_stage_==2);
   x.c.loop();assert(!x.c.rx_recovery_stage_&&x.c.rx_watch_&&x.c.known_);
   // ACK is not proof that IR reception works. Keep the next attempt scheduled.
   test_time+=4999;x.c.loop();assert(x.driver.aborts==1);
   test_time++;x.c.loop();assert(x.driver.aborts==2);
   test_time+=40;x.c.loop();x.c.loop();
   test_time+=15000;x.c.loop();assert(x.driver.aborts==3);
   test_time+=40;x.c.loop();x.c.loop();
   test_time+=5000;x.c.loop();assert(x.driver.aborts==4&&ir_count==0);
   test_time+=40;x.c.loop();x.c.loop();
   x.sample(0,28);assert(x.c.rx_watch_&&x.c.current_.temperature==28);
   // Regression: valid RX followed by total silence (partial=0) still recovers.
   test_time+=4999;x.c.loop();assert(x.driver.aborts==4);
   test_time++;x.c.loop();assert(x.driver.aborts==5);
  }
{Fixture x;
   assert(x.c.traits().flags==0);
   x.c.setup_environment_sensors_();
   assert(std::isnan(x.c.current_temperature)&&std::isnan(x.c.current_humidity));
   sensor::Sensor temperature,humidity;
   temperature.publish_state(23.5f);
   x.c.set_temperature_sensor(&temperature);
   assert(x.c.traits().flags==climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
   x.c.set_humidity_sensor(&humidity);
   assert(x.c.traits().flags==(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE|climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY));
   x.c.setup_environment_sensors_();
   assert(x.c.current_temperature==23.5f&&std::isnan(x.c.current_humidity));
   const float target=x.c.target_temperature;
   const auto publishes=x.c.publishes;
   temperature.publish_state(27.25f);humidity.publish_state(63.0f);
   assert(x.c.current_temperature==27.25f&&x.c.current_humidity==63.0f&&x.c.publishes==publishes+2);
   assert(x.c.target_temperature==target&&!x.c.pending_&&ir_count==0);
   x.c.phase_=x.c.FAULT;temperature.publish_state(22.0f);
   assert(x.c.current_temperature==22.0f&&x.c.phase_==x.c.FAULT&&ir_count==0);
   temperature.publish_state(NAN);humidity.publish_state(101.0f);
   assert(std::isnan(x.c.current_temperature)&&std::isnan(x.c.current_humidity));
   humidity.publish_state(-1.0f);assert(std::isnan(x.c.current_humidity));
   temperature.publish_state(INFINITY);assert(std::isnan(x.c.current_temperature));
   temperature.publish_state(24.0f);humidity.publish_state(0.0f);
   assert(x.c.current_temperature==24.0f&&x.c.current_humidity==0.0f);
   humidity.publish_state(100.0f);assert(x.c.current_humidity==100.0f);
   assert(x.c.target_temperature==target&&!x.c.pending_&&ir_count==0);
   std::cout<<"Environment sensors: optional traits, initial/live/invalid/recovered states, fault reporting and no IR passed\\n";
  }
{Fixture x;x.c.arm_rx_watch_();parse_ok=true;x.sample(0,27);
   assert(x.c.rx_watch_&&x.c.rx_monitor_);
   test_time+=1500;x.c.loop();assert(x.driver.aborts==1);
   test_time+=40;x.c.loop();x.c.loop();
   x.c.pending_=true;test_time+=10000;x.c.service_rx_watch_();
   assert(x.driver.aborts==1&&x.c.rx_watch_); // Queued HA command has priority.
   x.c.pending_=false;x.c.finish_command_();assert(x.c.rx_watch_);
   x.c.phase_=x.c.PAIR;test_time+=10000;x.c.service_rx_watch_();
   assert(x.driver.aborts==1&&x.c.rx_watch_); // Pairing pauses guard, doesn't cancel it.
   x.c.phase_=x.c.LISTEN;x.c.service_rx_watch_();assert(x.driver.aborts==2);
  }
  std::cout << "PASS: core pairing, normal TX/RX, unknown fan recovery, retired feature no-op, environment, Matter batching, no IR replay\n";
}

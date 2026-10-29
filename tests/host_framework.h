#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <functional>
inline uint32_t test_time = 0;
inline uint32_t millis() { return test_time; }
inline uint32_t micros() { return test_time * 1000; }
inline void delay(unsigned ms) { test_time += ms; }
inline int digitalRead(int) { return 0; }
#define LOW 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGCONFIG(...) ((void)0)
#define LOG_PIN(...) ((void)0)
#define LOG_STR(x) x
class Stream {
 public:
  virtual ~Stream() = default;
  virtual int available()=0; virtual int read()=0; virtual int peek()=0;
  virtual void flush()=0; virtual size_t write(uint8_t)=0;
};
namespace esphome {
namespace sensor {
struct Sensor {
  float state=NAN;
  std::vector<std::function<void(float)>> callbacks;
  void add_on_state_callback(std::function<void(float)> cb){callbacks.push_back(cb);}
  void publish_state(float value){state=value;for(auto &cb:callbacks)cb(value);}
};
}
struct Component {
  virtual ~Component() = default;
  virtual void setup() {} virtual void loop() {} virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void status_set_error(const char*) {} void status_clear_error() {}
};
namespace setup_priority { constexpr float AFTER_WIFI = 200; }
struct InternalGPIOPin { int pin=0; void setup() {} int get_pin() { return pin; } };
inline bool preference_save_ok=true, preference_sync_ok=true;
inline std::map<uint32_t,std::vector<uint8_t>> preferences;
struct ESPPreferenceObject {
  uint32_t key=0;
  template<class T> bool save(const T *p) {
    if (!preference_save_ok) return false;
    preferences[key]=std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+sizeof(T)); return true;
  }
  template<class T> bool load(T *p) {
    auto it=preferences.find(key);
    if(it==preferences.end() || it->second.size()!=sizeof(T)) return false;
    memcpy(p,it->second.data(),sizeof(T)); return true;
  }
};
struct Preferences {
  template<class T> ESPPreferenceObject make_preference(uint32_t key) { return {key}; }
  bool sync() { return preference_sync_ok; }
};
inline Preferences preference_store;
inline Preferences *global_preferences=&preference_store;
namespace text_sensor { struct TextSensor { std::string state; unsigned publications=0; void publish_state(const char *s) {state=s;publications++;} }; }
namespace binary_sensor { struct BinarySensor {bool state=false; void publish_state(bool s) {state=s;}}; }
namespace uart {
enum class UARTFlushResult {UART_FLUSH_RESULT_SUCCESS};
constexpr int UART_CONFIG_PARITY_NONE=0;
struct UARTComponent {
  int available(){return 0;} bool read_byte(uint8_t*){return false;}
  bool peek_byte(uint8_t*){return false;} void write_byte(uint8_t){}
  UARTFlushResult flush(){return UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;}
};
struct UARTDevice {UARTComponent *parent_=nullptr; void check_uart_settings(int,int,int,int){};};
}
namespace climate {
constexpr uint32_t CLIMATE_SUPPORTS_CURRENT_TEMPERATURE = 1 << 0;
constexpr uint32_t CLIMATE_SUPPORTS_CURRENT_HUMIDITY = 1 << 3;
enum ClimateMode {CLIMATE_MODE_OFF,CLIMATE_MODE_AUTO,CLIMATE_MODE_COOL,CLIMATE_MODE_HEAT,CLIMATE_MODE_DRY,CLIMATE_MODE_FAN_ONLY};
enum ClimateFanMode {CLIMATE_FAN_AUTO,CLIMATE_FAN_LOW,CLIMATE_FAN_MEDIUM,CLIMATE_FAN_HIGH};
enum ClimateSwingMode {CLIMATE_SWING_OFF,CLIMATE_SWING_VERTICAL,CLIMATE_SWING_HORIZONTAL,CLIMATE_SWING_BOTH};
struct ClimateTraits {
  uint32_t flags=0;
  void add_feature_flags(uint32_t value){flags|=value;}
  std::vector<ClimateSwingMode> swings;
  void set_supported_modes(std::initializer_list<ClimateMode>){}
  void set_supported_fan_modes(std::initializer_list<ClimateFanMode>){}
  void set_supported_swing_modes(std::initializer_list<ClimateSwingMode> m){swings=m;}
  void set_visual_min_temperature(int){} void set_visual_max_temperature(int){}
  void set_visual_temperature_step(int){}
};
struct ClimateCall {
  std::optional<ClimateMode> m; std::optional<ClimateFanMode> f;
  std::optional<ClimateSwingMode> s; std::optional<float> t;
  std::function<void(const ClimateCall &)> on_perform;
  void set_mode(ClimateMode value) { m=value; }
  void set_fan_mode(ClimateFanMode value) { f=value; }
  void set_target_temperature(float value) { t=value; }
  void perform() const { if (on_perform) on_perform(*this); }
  auto get_mode()const{return m;} auto get_fan_mode()const{return f;}
  auto get_swing_mode()const{return s;} auto get_target_temperature()const{return t;}
};
struct Climate {
  float current_temperature=NAN, current_humidity=NAN;
  unsigned publishes=0;
  float target_temperature=25; ClimateMode mode=CLIMATE_MODE_OFF;
  std::optional<ClimateFanMode> fan_mode;
  ClimateSwingMode swing_mode=CLIMATE_SWING_OFF;
  struct Restore {void apply(Climate*){}};
  std::optional<Restore> restore_state_(){return {};}
  virtual ~Climate()=default;
  virtual ClimateTraits traits(){return {};}
  virtual void control(const ClimateCall&){}
  ClimateCall make_call(){ ClimateCall call; call.on_perform=[this](const ClimateCall &value){control(value);}; return call; }
  void publish_state(){publishes++;}
};
}
}

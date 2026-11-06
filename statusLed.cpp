#include "statusLed.h"
#include "sdControl.h"

#define LED_GPIO 2        // same pin as LED_ON / LED_OFF in ESPWebDAV.ino
#define SEQ_MAX  240      // worst case is about 170 steps

// IP address pattern (one digit at a time, the dots are longer pauses):
//   start     3 fast flashes
//   digit 1-9 that many flashes
//   digit 0   one long flash
//   dot       long pause
//   end       3 fast flashes
#define FAST_ON      80
#define FAST_OFF    120
#define START_PAUSE 1000
#define BLINK_ON    250
#define BLINK_OFF   300
#define ZERO_ON     900
#define ZERO_OFF    400
#define DIGIT_PAUSE 600   // added after each digit
#define DOT_PAUSE  1200   // added after each octet

uint8_t  StatusLed::_mode = LEDMODE_AUTO;
bool     StatusLed::_owned = false;
bool     StatusLed::_announced = false;
bool     StatusLed::_playing = false;
uint16_t StatusLed::_seq[SEQ_MAX];
uint8_t  StatusLed::_n = 0;
uint8_t  StatusLed::_i = 0;
uint32_t StatusLed::_stepStart = 0;

static inline void ledWrite(bool on) { digitalWrite(LED_GPIO, on ? LOW : HIGH); }

const char* StatusLed::modeName(uint8_t m) {
  return m == LEDMODE_ON ? "ON" : (m == LEDMODE_OFF ? "OFF" : "AUTO");
}

void StatusLed::setMode(uint8_t m) {
  _mode = m > LEDMODE_OFF ? LEDMODE_AUTO : m;
}

void StatusLed::cardOwned(bool ours) {
  _owned = ours;
  if(_mode == LEDMODE_AUTO && !_playing) ledWrite(ours);   // show it at once, loop() may be busy serving
}

static void addStep(uint16_t* seq, uint8_t& n, uint16_t on, uint16_t off) {
  if(n + 2 > SEQ_MAX) return;
  seq[n++] = on;
  seq[n++] = off;
}

static void lengthenPause(uint16_t* seq, uint8_t n, uint16_t extra) {
  if(n) seq[n - 1] += extra;   // the last entry is always an off time
}

void StatusLed::buildIpSequence(const IPAddress& ip) {
  _n = 0;
  for(int k = 0; k < 3; k++) addStep(_seq, _n, FAST_ON, FAST_OFF);
  lengthenPause(_seq, _n, START_PAUSE);
  for(int o = 0; o < 4; o++) {
    String s(ip[o]);
    for(unsigned int c = 0; c < s.length(); c++) {
      int d = s[c] - '0';
      if(d == 0) addStep(_seq, _n, ZERO_ON, ZERO_OFF);
      else for(int k = 0; k < d; k++) addStep(_seq, _n, BLINK_ON, BLINK_OFF);
      lengthenPause(_seq, _n, DIGIT_PAUSE);
    }
    lengthenPause(_seq, _n, DOT_PAUSE);
  }
  for(int k = 0; k < 3; k++) addStep(_seq, _n, FAST_ON, FAST_OFF);
}

void StatusLed::announceIp(const IPAddress& ip) {
  buildIpSequence(ip);
  _i = 0;
  _stepStart = millis();
  _playing = _n > 0;
  _announced = true;
}

void StatusLed::announceIpOnce(const IPAddress& ip) {
  if(_announced || _mode != LEDMODE_AUTO) return;
  announceIp(ip);
}

void StatusLed::handle(bool connecting, bool connected) {
  uint32_t now = millis();

  if(_playing) {
    if(now - _stepStart >= _seq[_i]) {   // wrap-safe
      _i++;
      _stepStart = now;
      if(_i >= _n) _playing = false;
    }
    if(_playing) {
      ledWrite((_i & 1) == 0);   // even entries are on times
      return;
    }
  }

  if(_mode == LEDMODE_ON)  { ledWrite(true);  return; }
  if(_mode == LEDMODE_OFF) { ledWrite(false); return; }

  bool on;
  if(_owned)                         on = true;
  else if(connecting)                on = false;
  else if(!connected)                on = true;
  else if(sdcontrol.reservedForPrinter()) on = (now % 2000) < 1000;
  else if(sdcontrol.printerActive()) { uint32_t t = now % 1000; on = t < 60 || (t >= 200 && t < 260); }
  else                               on = (now % 1000) < 50;
  ledWrite(on);
}

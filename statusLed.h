// Status LED (GPIO2, active low). Non-blocking: handle() is called from loop().
//   Wi-Fi connecting          off
//   no Wi-Fi                  solid on
//   connected, card free      one short flash per second
//   printer using the card    two short flashes per second
//   Printer mode (reserved)   slow even blink, 1 s on / 1 s off
//   this board owns the card  solid on while it does
// After the first connect since power-up the IP address is blinked once, see announceIp().
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

#define LEDMODE_AUTO 0   // status patterns above
#define LEDMODE_ON   1   // always on
#define LEDMODE_OFF  2   // always off (also no IP blink at power-up)

class StatusLed {
public:
  static void setMode(uint8_t m);
  static uint8_t mode() { return _mode; }
  static void handle(bool connecting, bool connected);
  static void cardOwned(bool ours);                // called when this board takes / releases the card
  static void announceIpOnce(const IPAddress& ip); // first call since power-up only, and only in AUTO mode
  static void announceIp(const IPAddress& ip);     // always (M60 IP)
  static const char* modeName(uint8_t m);

private:
  static void buildIpSequence(const IPAddress& ip);
  static uint8_t _mode;
  static bool _owned, _announced, _playing;
  static uint16_t _seq[];         // alternating on / off durations in ms
  static uint8_t _n, _i;
  static uint32_t _stepStart;
};

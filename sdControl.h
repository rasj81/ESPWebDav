#ifndef _SD_CONTROL_H_
#define _SD_CONTROL_H_

#include <Arduino.h>  // for IRAM_ATTR

#define SPI_BLOCKOUT_PERIOD	5000UL   // default wait after the printer last used the card (PRINTER_WAIT=)

class SDControl {
public:
  SDControl() { }
  static void setup();
  static void takeBusControl();
  static void relinquishBusControl();
  static bool canWeTakeBus();          // false while the printer is using the card or it's reserved for the printer
  static bool printerActive();         // printer used the card within the last PRINTER_WAIT seconds
  static bool takePrinterTouched();    // true (once) if the printer used the card since the last call

  // Who may use the card: the network (default) or only the printer (web switch / M58)
  static void reserveForPrinter(bool r) { _reserved = r; }
  static bool reservedForPrinter() { return _reserved; }
  static void setWaitMs(unsigned long ms) { _waitMs = ms; }
  static unsigned long waitMs() { return _waitMs; }
 
private:
  static void onCsFalling();  // interrupt handler; IRAM_ATTR is on the definition in sdControl.cpp
  static volatile unsigned long _lastPrinterUse;
  static volatile bool _printerUsed;
  static volatile bool _printerTouched;
  static volatile bool _weTookBus;
  static volatile unsigned long _waitMs;
  static bool _reserved;
};

extern SDControl sdcontrol;

#endif

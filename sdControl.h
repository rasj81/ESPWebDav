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

  // DIAGNOSTIC BUILD (branch diagnostic-cs-sense): measure what the printer does on the card pins.
  static void sample();                // call from loop(): looks for activity on the SD clock/command pins
  static String diagSummary();         // short XML-safe, space-free text; used as the reject message

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
  static volatile uint32_t _csEdges;    // DIAGNOSTIC: falling edges on CS_SENSE while we did not own the bus
  static volatile uint32_t _lastEdgeMs; // DIAGNOSTIC: millis() of the latest edge (0 = none)
  static uint32_t _clkSeenMs;           // DIAGNOSTIC: last time the SD clock pin was seen changing (0 = never)
  static uint32_t _cmdSeenMs;           // DIAGNOSTIC: same for the SD command pin
  static uint32_t _lastSampleMs;
  static uint32_t _prevReportEdges;
  static uint32_t _prevReportMs;
  static bool _reserved;
};

extern SDControl sdcontrol;

#endif

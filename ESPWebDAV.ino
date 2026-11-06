// Using the WebDAV server with Rigidbot 3D printer.
// Printer controller is a variation of Rambo running Marlin firmware

#include "serial.h"
#include "parser.h"
#include "config.h"
#include "network.h"
#include "gcode.h"
#include "sdControl.h"
#include "firmware.h"
#include "statusLed.h"

// DIAGNOSTIC BUILD: 1 = start in Printer mode, so the board does not touch the SD card at power-up
// (no SETUP.INI, no FIRMWARE.BIN check, no log writes). Wi-Fi comes from the settings saved in flash.
// Set to 0 for the normal behaviour.
#define DIAG_START_IN_PRINTER_MODE 0

// Stay off the card for this long after power-up, so the printer finishes its own card start first.
// The printer powers the card and this board together; both start at once and the printer
// (SPI build) was seen to hang with a blank screen while this board read the card. Set 0 for no wait.
#define BOOT_HOLDOFF_MS 20000

// LED is connected to GPIO2 on this board
#define INIT_LED			{pinMode(2, OUTPUT);}
#define LED_ON				{digitalWrite(2, LOW);}
#define LED_OFF				{digitalWrite(2, HIGH);}

// ------------------------
void setup() {
	SERIAL_INIT(115200);
	SERIAL_ECHOLN("");
	SERIAL_ECHOLN("ESPWebDAV build " __DATE__ " " __TIME__);
	INIT_LED;
	blink();

#if DIAG_START_IN_PRINTER_MODE
	// DIAGNOSTIC: never touch the card until "Use card from here" (or M58 NETWORK).
	// Every card access in this sketch is already refused while the card is reserved for the printer.
	sdcontrol.reserveForPrinter(true);
#endif
	
#if BOOT_HOLDOFF_MS
	// card lines stay untouched (inputs) while we wait
	for(uint32_t t = 0; t < BOOT_HOLDOFF_MS; t += 500) { LED_ON; delay(50); LED_OFF; delay(450); }
#endif

	sdcontrol.setup();

	// FIRMWARE.BIN on the card: install it and restart (renamed to FIRMWARE.BAK afterwards)
	firmwareFromCard();

	// ----- WIFI -------
  if(config.load() == 1) { // Connected before
    if(!network.start()) {
      SERIAL_ECHOLN("Connect failed. Check SETUP.INI, or set the Wi-Fi with serial commands:");
      gcode.printHelp();
    }
  }
  else {
    SERIAL_ECHOLN("Welcome to FYSETC: www.fysetc.com");
    SERIAL_ECHOLN("Please set the Wi-Fi first: put SETUP.INI on the card, or e.g.");
    SERIAL_ECHOLN("  M50 SSID=MyWiFi   then   M50 PASSWORD=secret   then   M52");
    gcode.printHelp();
  }
}

// ------------------------
void loop() {
  // handle the request
	network.handle();

  // Handle gcode
  gcode.Handle();

  // DIAGNOSTIC BUILD: watch the SD clock/command pins for printer activity
  sdcontrol.sample();

  // blink
  statusBlink();
}

// ------------------------
void blink()	{
// ------------------------
	LED_ON; 
	delay(100); 
	LED_OFF; 
	delay(400);
}

// ------------------------
void errorBlink()	{
// ------------------------
	for(int i = 0; i < 100; i++)	{
		LED_ON; 
		delay(50); 
		LED_OFF; 
		delay(50);
	}
}

void statusBlink() {
  StatusLed::handle(network.isConnecting(), network.isConnected());
}

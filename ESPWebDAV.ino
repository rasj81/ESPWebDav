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

	// BOOT_WAIT (SETUP.INI, kept in flash): stay off the card after power-up so the printer finishes its
	// own card start first. Read from flash because SETUP.INI is on the card we are staying away from.
	// A change in SETUP.INI therefore applies from the next power-up after it was read.
	for(uint32_t t = 0; t < (uint32_t)Config::savedBootWait() * 1000UL; t += 500) { LED_ON; delay(50); LED_OFF; delay(450); }

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

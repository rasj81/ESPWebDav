#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

volatile unsigned long SDControl::_lastPrinterUse = 0;
volatile bool SDControl::_printerUsed = false;
volatile bool SDControl::_printerTouched = false;
volatile bool SDControl::_weTookBus = false;
volatile unsigned long SDControl::_waitMs = SPI_BLOCKOUT_PERIOD;
bool SDControl::_reserved = false;

// esp8266 core 2.5+ resets the chip if an interrupt handler is not in IRAM,
// so this can't be a plain lambda any more.
void IRAM_ATTR SDControl::onCsFalling() {
	if(!_weTookBus) {
		_lastPrinterUse = millis();
		_printerUsed = true;
		_printerTouched = true;
	}
}

void SDControl::setup() {
  // ----- GPIO -------
	// Detect when other master uses SPI bus
	pinMode(CS_SENSE, INPUT);
	// IRAM addresses start with 0x401; 0x402... means flash and the core will abort
	Serial.printf("CS interrupt handler at %p\n", (void*)&onCsFalling);
	attachInterrupt(digitalPinToInterrupt(CS_SENSE), onCsFalling, FALLING);

	// wait for other master to assert SPI bus first
	delay(_waitMs);
}

// ------------------------
void SDControl::takeBusControl()	{
// ------------------------
	_weTookBus = true;
	//LED_ON;
	pinMode(MISO_PIN, SPECIAL);	
	pinMode(MOSI_PIN, SPECIAL);	
	pinMode(SCLK_PIN, SPECIAL);	
	pinMode(SD_CS, OUTPUT);
}

// ------------------------
void SDControl::relinquishBusControl()	{
// ------------------------
	pinMode(MISO_PIN, INPUT);	
	pinMode(MOSI_PIN, INPUT);	
	pinMode(SCLK_PIN, INPUT);	
	pinMode(SD_CS, INPUT);
	//LED_OFF;
	_weTookBus = false;
}

bool SDControl::printerActive() {
	// wrap-safe: compare elapsed time, not end times
	return _printerUsed && (millis() - _lastPrinterUse < _waitMs);
}

bool SDControl::canWeTakeBus() {
	return !_reserved && !printerActive();
}

bool SDControl::takePrinterTouched() {
	noInterrupts();
	bool t = _printerTouched;
	_printerTouched = false;
	interrupts();
	return t;
}

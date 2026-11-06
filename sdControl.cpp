#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

volatile unsigned long SDControl::_lastPrinterUse = 0;
volatile bool SDControl::_printerUsed = false;
volatile bool SDControl::_printerTouched = false;
volatile bool SDControl::_weTookBus = false;
volatile unsigned long SDControl::_waitMs = SPI_BLOCKOUT_PERIOD;
bool SDControl::_reserved = false;

// ---- diagnostic state (the bus-sharing rules themselves are unchanged) ----
volatile uint32_t SDControl::_csEdges = 0;
volatile uint32_t SDControl::_lastEdgeMs = 0;
uint32_t SDControl::_clkSeenMs = 0;
uint32_t SDControl::_cmdSeenMs = 0;
uint32_t SDControl::_lastSampleMs = 0;
uint32_t SDControl::_prevReportEdges = 0;
uint32_t SDControl::_prevReportMs = 0;

// esp8266 core 2.5+ resets the chip if an interrupt handler is not in IRAM,
// so this can't be a plain lambda any more.
void IRAM_ATTR SDControl::onCsFalling() {
	if(!_weTookBus) {
		uint32_t now = millis();
		_lastPrinterUse = now;
		_printerUsed = true;
		_printerTouched = true;
		_csEdges++;        // DIAGNOSTIC
		_lastEdgeMs = now; // DIAGNOSTIC
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


// ------------------------
// DIAGNOSTIC BUILD. No interrupts on the clock line (it can run at MHz and would starve the chip):
// read the pins in a short loop and note whether the level ever changes.
void SDControl::sample() {
// ------------------------
	if(_weTookBus) return;                       // our own SPI traffic would look like activity
	uint32_t now = millis();
	if(now - _lastSampleMs < 100) return;
	_lastSampleMs = now;

	int clk0 = digitalRead(SCLK_PIN), cmd0 = digitalRead(MOSI_PIN);
	bool clkMoved = false, cmdMoved = false;
	for(int i = 0; i < 400; i++) {
		if(digitalRead(SCLK_PIN) != clk0) clkMoved = true;
		if(digitalRead(MOSI_PIN) != cmd0) cmdMoved = true;
	}
	if(clkMoved) _clkSeenMs = now ? now : 1;
	if(cmdMoved) _cmdSeenMs = now ? now : 1;
}

// ------------------------
// Characters limited to [A-Za-z0-9_=.-]: the text is sent inside an XML href by rejectClient().
String SDControl::diagSummary() {
// ------------------------
	uint32_t now = millis();
	noInterrupts();
	uint32_t edges = _csEdges, last = _lastEdgeMs, lastUse = _lastPrinterUse;
	interrupts();
	uint32_t dEdges = edges - _prevReportEdges, dMs = now - _prevReportMs;
	_prevReportEdges = edges; _prevReportMs = now;

	long left = (long)_waitMs - (long)(now - lastUse);
	if(!_printerUsed || left < 0) left = 0;

	char b[220];
	snprintf(b, sizeof(b),
		"Printer_busy_diag_up%lus_cs%d_edges%lu_last%lums_d%lu_in%lums_clk%lu_cmd%lu_wait%lus_left%ldms",
		(unsigned long)(now / 1000), digitalRead(CS_SENSE), (unsigned long)edges,
		(unsigned long)(last ? now - last : 0), (unsigned long)dEdges, (unsigned long)dMs,
		(unsigned long)(_clkSeenMs ? now - _clkSeenMs : 0), (unsigned long)(_cmdSeenMs ? now - _cmdSeenMs : 0),
		(unsigned long)(_waitMs / 1000), left);
	return String(b);
}

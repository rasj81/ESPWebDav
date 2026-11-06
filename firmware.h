#pragma once
// Firmware update from FIRMWARE.BIN on the card (at startup, or M57).
// Make the file in Arduino IDE with Sketch > Export Compiled Binary and rename it.
// A gzip-compressed .bin works too (the boot loader unpacks it).
#include <Arduino.h>

#define FW_FILE     "FIRMWARE.BIN"
#define FW_FILE_GZ  "FIRMWARE.GZ"    // same, compressed with gzip -9 (needed on 1MB boards once the firmware is over ~500 KB)
#define FW_DONE     "FIRMWARE.BAK"   // installed: renamed so it isn't installed again
#define FW_BAD      "FIRMWARE.BAD"   // rejected: renamed so it isn't tried again

// Returns 0 if there is no FIRMWARE.BIN, a negative number on failure.
// On success the board restarts with the new firmware and this never returns.
int firmwareFromCard();

// Human-readable text for the last Update error
String firmwareError();

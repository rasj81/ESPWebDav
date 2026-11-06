#include "firmware.h"
#include <SPI.h>
#include <SdFat.h>
#include "sdfat_compat.h"
#include <Updater.h>
#include <utility>
#include "https.h"      // for HTTPS_SUPPORTED (= core 3.x)
#include "gzcomp.h"
#include "pins.h"
#include "sdControl.h"
#include "logger.h"

// the file type SdFat::open returns (File on SdFat v1, File32 or FsFile on v2)
using FsFile_t = decltype(std::declval<SdFat&>().open("", O_RDONLY));

#define FW_TMP "FWTEMP.GZ"   // compressed copy made on the card when the .bin is too big

String firmwareError() {
#if HTTPS_SUPPORTED
  return Update.getErrorString();
#else
  return String("error ") + Update.getError();
#endif
}

// ---- compressing FIRMWARE.BIN on the card ----
struct GzJob {
  FsFile_t* in;
  FsFile_t* out;
  uint32_t done, size, nextDot;
};
static int gzRead(void* c, uint8_t* b, size_t n) { return ((GzJob*)c)->in->read(b, n); }
static bool gzWrite(void* c, const uint8_t* b, size_t n) { return ((GzJob*)c)->out->write(b, n) == n; }
static void gzTick(void* c) {
  GzJob* j = (GzJob*)c;
  j->done = j->in->curPosition();
  if(j->done >= j->nextDot) { Serial.print('.'); j->nextDot += j->size / 40 + 1; digitalWrite(2, !digitalRead(2)); }
  yield();
}

// Write src gzip-compressed to FW_TMP. Returns the compressed size, 0 on failure.
static uint32_t compressOnCard(SdFat& sd, const char* src, uint32_t size) {
  Serial.printf("Firmware update: %s is too big for the free flash, compressing it on the card", src);
  unsigned long t0 = millis();
  FsFile_t in = sd.open(src, O_RDONLY);
  if(sd.exists(FW_TMP)) sd.remove(FW_TMP);
  FsFile_t out = sd.open(FW_TMP, O_WRONLY | O_CREAT | O_TRUNC);
  uint32_t n = 0;
  if(in && out) {
    GzJob j = { &in, &out, 0, size, 0 };
    n = gz_compress(gzRead, gzWrite, gzTick, &j);
  }
  in.close();
  out.close();
  Serial.printf("\nFirmware update: compressed %u -> %u bytes in %lu s\n", (unsigned)size, (unsigned)n, (millis() - t0) / 1000);
  if(!n) sd.remove(FW_TMP);
  return n;
}

// Flash the image in 'name' (plain or gzip). Returns "" on success, else the reason.
static String flashFrom(SdFat& sd, const char* name) {
  FsFile_t f = sd.open(name, O_RDONLY);
  if(!f) return "card read error";
  uint32_t size = f.fileSize();
  int magic = f.peek();
  String err;
  if(size < 4096 || (magic != 0xE9 && magic != 0x1F))  // firmware images start with 0xE9, gzip files with 0x1F
    err = "not an ESP8266 firmware .bin (or gzipped .bin) file";
  else if(!Update.begin(size))
    err = firmwareError() + " (needs " + String(size) + " bytes, " + String(ESP.getFreeSketchSpace()) + " free)";
  uint8_t buf[512];
  uint32_t done = 0, nextDot = 0;
  Serial.print("Firmware update: installing");
  while(!err.length() && done < size) {
    int n = f.read(buf, sizeof(buf));
    if(n <= 0) { err = "card read error"; break; }
    if(Update.write(buf, n) != (size_t)n) { err = firmwareError(); break; }
    done += n;
    if(done >= nextDot) { Serial.print('.'); nextDot += size / 40 + 1; digitalWrite(2, !digitalRead(2)); }
    yield();
  }
  Serial.println();
  if(!err.length() && !Update.end()) err = firmwareError();
  if(err.length()) Update.end(true);     // throw away a partial update
  f.close();
  return err;
}

int firmwareFromCard() {
  if(!sdcontrol.canWeTakeBus()) {
    Serial.println("Firmware update: card busy (printer), not checked");
    return -1;
  }
  sdcontrol.takeBusControl();
  SdFat sd;
  if(!sd.begin(SD_CS, SPI_FULL_SPEED)) {
    sdcontrol.relinquishBusControl();
    return -2;
  }
  const char* name = sd.exists(FW_FILE) ? FW_FILE : (sd.exists(FW_FILE_GZ) ? FW_FILE_GZ : nullptr);
  if(!name) {
    sdcontrol.relinquishBusControl();
    return 0;                            // nothing to install
  }

  uint32_t size = 0;
  int magic = -1;
  {
    FsFile_t f = sd.open(name, O_RDONLY);
    if(f) { size = f.fileSize(); magic = f.peek(); f.close(); }
  }
  Serial.printf("Firmware update: %s found (%u bytes)\n", name, (unsigned)size);
  pinMode(2, OUTPUT);

  // An uncompressed image that won't fit next to the running firmware is gzipped first
  const char* src = name;
  String err;
  if(magic == 0xE9 && size + 4096 > ESP.getFreeSketchSpace()) {
    if(compressOnCard(sd, name, size)) src = FW_TMP;
    else err = "could not compress it on the card (card full, or not enough memory)";
  }
  if(!err.length()) err = flashFrom(sd, src);
  bool ok = !err.length();

  // rename so the same file isn't installed (or retried) on every start
  if(src != name) sd.remove(FW_TMP);
  const char* to = ok ? FW_DONE : FW_BAD;
  if(sd.exists(to)) sd.remove(to);
  sd.rename(name, to);
  sdcontrol.relinquishBusControl();

  if(ok) {
    Serial.printf("Firmware update: done, %s renamed to %s, restarting\n", name, FW_DONE);
    delay(300);
    ESP.restart();
    while(true) delay(100);
  }
  Serial.printf("Firmware update FAILED: %s (file renamed to %s, old firmware kept)\n", err.c_str(), FW_BAD);
  logger.net(LOG_SPARSE, "Firmware update from %s FAILED: %s (renamed to %s)", name, err.c_str(), FW_BAD);
  return -3;
}

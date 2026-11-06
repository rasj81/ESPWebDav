// Lets this sketch build with both SD card libraries shipped by the esp8266 core:
//   - core 2.x  -> ESP8266SdFat v1 (SdFat 1.x)
//   - core 3.x  -> ESP8266SdFat v2 (SdFat 2.x)
// Include this after <SdFat.h>.
#ifndef _SDFAT_COMPAT_H_
#define _SDFAT_COMPAT_H_

#include <SdFat.h>

// Only SdFat 2.x defines SDFAT_FILE_TYPE (in SdFatConfig.h).
#ifdef SDFAT_FILE_TYPE
  #define SDFAT_V2 1
  // v2: begin(csPin, maxSck) takes the clock in Hz; SPI_FULL_SPEED is a number
  typedef uint32_t SdSpeed_t;
#else
  #define SDFAT_V2 0
  // v1: begin(csPin, SPISettings); SPI_FULL_SPEED is an SPISettings
  typedef SPISettings SdSpeed_t;
#endif

// Read a file's last-modified date and time in FAT format.
static inline bool sdGetModifyDateTime(FatFile* f, uint16_t* fatDate, uint16_t* fatTime) {
#if SDFAT_V2
  return f->getModifyDateTime(fatDate, fatTime);
#else
  dir_t dir;
  if (!f->dirEntry(&dir)) return false;
  *fatDate = dir.lastWriteDate;
  *fatTime = dir.lastWriteTime;
  return true;
#endif
}

// FAT date/time field helpers (v1 calls them FAT_*, v2 calls them FS_*)
#if SDFAT_V2
  #define SD_YEAR(d)   FS_YEAR(d)
  #define SD_MONTH(d)  FS_MONTH(d)
  #define SD_DAY(d)    FS_DAY(d)
  #define SD_HOUR(t)   FS_HOUR(t)
  #define SD_MINUTE(t) FS_MINUTE(t)
  #define SD_SECOND(t) FS_SECOND(t)
#else
  #define SD_YEAR(d)   FAT_YEAR(d)
  #define SD_MONTH(d)  FAT_MONTH(d)
  #define SD_DAY(d)    FAT_DAY(d)
  #define SD_HOUR(t)   FAT_HOUR(t)
  #define SD_MINUTE(t) FAT_MINUTE(t)
  #define SD_SECOND(t) FAT_SECOND(t)
#endif

#endif

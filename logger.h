// Buffered log files on the SD card: network.log and webdav.log
//
// Three areas, each with its own level set in SETUP.INI:
//   LOG_NETWORK -> network.log  (Wi-Fi, IP, signal, SETUP.INI, rename)
//   LOG_FILES   -> webdav.log   (uploads, downloads, deletes, moves, folders)
//   LOG_WEBDAV  -> webdav.log   (individual WebDAV requests, rejections)
//
// Log lines are kept in RAM and written to the card only when this board owns
// the SPI bus (never while the printer is using the card).
#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <Arduino.h>

// Levels (same scale for every area)
#define LOG_OFF      0   // nothing
#define LOG_SPARSE   1   // errors and failures only
#define LOG_NORMAL   2   // errors + normal events
#define LOG_VERBOSE  3   // everything

// Defaults when SETUP.INI doesn't set them
#define LOG_NETWORK_DEFAULT  LOG_NORMAL
#define LOG_FILES_DEFAULT    LOG_NORMAL
#define LOG_WEBDAV_DEFAULT   LOG_SPARSE

#define LOG_BUF_MAX   4096          // max bytes held in RAM per log before lines are dropped
#define LOG_FILE_MAX  (256UL*1024)  // rotate a log file to *-old.log above this size

class Logger {
public:
  void setLevels(uint8_t network, uint8_t files, uint8_t webdav);
  uint8_t networkLevel() { return _net; }

  // printf-style; the line is kept only if lvl <= that area's level.
  // A timestamp is added in front of each line.
  void net(uint8_t lvl, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void files(uint8_t lvl, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void web(uint8_t lvl, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

  bool pending() { return _netBuf.length() || _davBuf.length() || _netDropped || _davDropped; }
  // Write buffered lines to the card. Caller must own the SPI bus.
  void flush();

private:
  void add(String& buf, uint32_t& dropped, const char* fmt, va_list ap);
  void flushOne(String& buf, uint32_t& dropped, const char* path, const char* oldPath);

  uint8_t _net = LOG_NETWORK_DEFAULT, _files = LOG_FILES_DEFAULT, _web = LOG_WEBDAV_DEFAULT;
  String _netBuf, _davBuf;
  uint32_t _netDropped = 0, _davDropped = 0;
};

extern Logger logger;

#endif

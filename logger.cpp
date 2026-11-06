#include <time.h>
#include <stdarg.h>
#include "logger.h"
#include "ESPWebDAV.h"

static uint8_t clampLevel(uint8_t l) { return l > LOG_VERBOSE ? LOG_VERBOSE : l; }

void Logger::setLevels(uint8_t network, uint8_t files, uint8_t webdav) {
  _net = clampLevel(network);
  _files = clampLevel(files);
  _web = clampLevel(webdav);
  // drop anything buffered before the settings were known for areas now off
  if(_net == LOG_OFF) { _netBuf = String(); _netDropped = 0; }
  if(_files == LOG_OFF && _web == LOG_OFF) { _davBuf = String(); _davDropped = 0; }
}

void Logger::add(String& buf, uint32_t& dropped, const char* fmt, va_list ap) {
  // Real date/time once NTP has set the clock, otherwise seconds since boot
  char ts[24];
  time_t now = time(nullptr);
  if(now > 1600000000) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);
  }
  else {
    snprintf(ts, sizeof(ts), "boot+%lus", (unsigned long)(millis() / 1000));
  }

  char line[200];
  vsnprintf(line, sizeof(line), fmt, ap);

  if(buf.length() + strlen(ts) + strlen(line) + 3 > LOG_BUF_MAX) {
    dropped++;
    return;
  }
  buf += ts;
  buf += ' ';
  buf += line;
  buf += "\r\n";

#ifdef DEBUG
  Serial.print(ts); Serial.print(' '); Serial.println(line);
#endif
}

void Logger::net(uint8_t lvl, const char* fmt, ...) {
  if(lvl == LOG_OFF || lvl > _net) return;
  va_list ap; va_start(ap, fmt);
  add(_netBuf, _netDropped, fmt, ap);
  va_end(ap);
}

void Logger::files(uint8_t lvl, const char* fmt, ...) {
  if(lvl == LOG_OFF || lvl > _files) return;
  va_list ap; va_start(ap, fmt);
  add(_davBuf, _davDropped, fmt, ap);
  va_end(ap);
}

void Logger::web(uint8_t lvl, const char* fmt, ...) {
  if(lvl == LOG_OFF || lvl > _web) return;
  va_list ap; va_start(ap, fmt);
  add(_davBuf, _davDropped, fmt, ap);
  va_end(ap);
}

void Logger::flushOne(String& buf, uint32_t& dropped, const char* path, const char* oldPath) {
  if(dropped) {
    String note = "(" + String(dropped) + " log lines dropped: card was busy)\r\n";
    if(!::dav.appendLog(path, oldPath, note)) return;
    dropped = 0;
  }
  if(buf.length() && ::dav.appendLog(path, oldPath, buf))
    buf = String();
}

void Logger::flush() {
  if(!pending() || !::dav.isSdReady()) return;
  flushOne(_netBuf, _netDropped, "network.log", "network-old.log");
  flushOne(_davBuf, _davDropped, "webdav.log", "webdav-old.log");
}

Logger logger;

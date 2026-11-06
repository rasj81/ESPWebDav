#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <Arduino.h>
#include <IPAddress.h>

#define WIFI_SSID_LEN 32
#define WIFI_PASSWD_LEN 64
#define DEVICE_NAME_LEN 32
#define TZ_LEN 48

#define EEPROM_SIZE 2048    // settings at 0, HTTPS certificate at 1024 (https.h)
#include "statusLed.h"   // LEDMODE_*
#define CONFIG_VERSION 9   // bump when fields are added after 'name'
#define BOOT_WAIT_DEFAULT 10  // seconds
#define DOMAIN_LEN 64
#define DAV_USER_LEN 32
#define DAV_PASSWD_LEN 64

#define IP_MODE_DHCP   0
#define IP_MODE_STATIC 1

// Weakest Wi-Fi security the board will accept (WIFI_SECURITY=)
#define SEC_OPEN 0
#define SEC_WEP  1
#define SEC_WPA  2
#define SEC_WPA2 3   // default: strongest this chip supports
// Weakest encryption algorithm accepted (WIFI_CIPHER=)
#define CIPH_TKIP 0  // TKIP or AES
#define CIPH_AES  1  // AES (CCMP) only, default

// Stored in EEPROM. Keep the order of existing fields so older saved data still loads.
typedef struct config_type
{
  unsigned char flag; // Was saved before?
  char ssid[32];
  char psw[64];
  char name[DEVICE_NAME_LEN + 1]; // device name from HOSTNAME= ("" = default)
  // --- fields below are only valid when ver == CONFIG_VERSION ---
  unsigned char ver;
  unsigned char ipMode;   // IP_MODE_DHCP or IP_MODE_STATIC
  unsigned char logNet;   // LOG_NETWORK level 0-3
  uint8_t ip[4];
  uint8_t subnet[4];
  uint8_t gateway[4];
  uint8_t dns[4];
  char tz[TZ_LEN];        // POSIX time zone, e.g. EST5EDT,M3.2.0,M11.1.0 ("" = UTC)
  unsigned char logFiles; // LOG_FILES level 0-3   (added in version 3)
  unsigned char logWeb;   // LOG_WEBDAV level 0-3  (added in version 3)
  unsigned char secMin;   // SEC_*  (added in version 4)
  unsigned char cipherMin;// CIPH_* (added in version 4)
  unsigned char https;    // HTTPS=1 file manager over https (added in version 5)
  char domain[DOMAIN_LEN];// part of HOSTNAME after the first '.' (added in version 5)
  unsigned char auth;     // AUTH=1 login required (added in version 6)
  char davUser[DAV_USER_LEN + 1];
  char davPsw[DAV_PASSWD_LEN + 1];
  unsigned char printerWait; // PRINTER_WAIT seconds (added in version 7)
  unsigned char led;      // LED=AUTO|ON|OFF, LEDMODE_* (added in version 8)
  unsigned char bootWait; // BOOT_WAIT seconds off the card after power-up (added in version 9)
}CONFIG_TYPE;

// Results of Config::setValue()
#define SET_OK        0
#define SET_SSID      1
#define SET_PASSWORD  2
#define SET_EMPTY    -1   // SSID/PASSWORD left blank
#define SET_UNKNOWN  -2   // no such setting
#define SET_BAD      -3   // value not valid for that setting

class Config	{
public:
  int setValue(const String& key, String value);  // key in upper case, same names as SETUP.INI
  int loadSD();
	unsigned char load();
	// BOOT_WAIT as saved in flash, readable before the card is touched (default if never saved)
	static unsigned char savedBootWait();
  char* ssid();
  void ssid(char* ssid);
  char* password();
  void password(char* password);
  void save(const char*ssid,const char*password);
  void save();
  int save_ip(const char *ip);   // caller must own the SPI bus and dav must be started
  int disarmSD();                // caller must own the SPI bus and dav must be started
  bool disarmPending() { return loadedFromSD && renameIni; }
  const char* deviceName();
  const char* deviceDomain();
  bool staticIp() { return data.ipMode == IP_MODE_STATIC; }
  IPAddress ip()      { return IPAddress(data.ip[0], data.ip[1], data.ip[2], data.ip[3]); }
  IPAddress subnet()  { return IPAddress(data.subnet[0], data.subnet[1], data.subnet[2], data.subnet[3]); }
  IPAddress gateway() { return IPAddress(data.gateway[0], data.gateway[1], data.gateway[2], data.gateway[3]); }
  IPAddress dns()     { return IPAddress(data.dns[0], data.dns[1], data.dns[2], data.dns[3]); }
  void applyLogLevels();   // also applies PRINTER_WAIT
  uint8_t secMin()    { return data.secMin; }
  uint8_t cipherMin() { return data.cipherMin; }
  const char* timeZone() { return data.tz; }
  bool https()         { return data.https; }
  // login is only enforced when AUTH=1 and both DAV_USER and DAV_PASSWORD are set
  bool authOn()        { return data.auth && data.davUser[0] && data.davPsw[0]; }
  const char* davUser()     { return data.davUser; }
  const char* davPassword() { return data.davPsw; }
  bool renameOn()      { return renameIni; }
  void printSettings();   // M50 with no argument

protected:
  void setDefaults();
  void validate();
  CONFIG_TYPE data;
  bool renameIni = false;   // RENAME=1 in SETUP.INI
  bool loadedFromSD = false; // settings came from SETUP.INI this boot
};

extern Config config;

#endif

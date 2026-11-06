#include "statusLed.h"
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include "sdfat_compat.h"
#include <EEPROM.h>
#include "pins.h"
#include "config.h"
#include "serial.h"
#include "sdControl.h"
#include "network.h"
#include "logger.h"
#include "ESPWebDAV.h"

static bool parseIp(String s, uint8_t out[4]) {
  IPAddress a;
  s.trim();
  if(!a.fromString(s)) return false;
  for(int i = 0; i < 4; i++) out[i] = a[i];
  return true;
}

static String ipStr(const uint8_t b[4]) {
  return IPAddress(b[0], b[1], b[2], b[3]).toString();
}

// Defaults for the fields that SETUP.INI may or may not set
void Config::setDefaults() {
  data.ver = CONFIG_VERSION;
  data.ipMode = IP_MODE_DHCP;
  data.logNet = LOG_NETWORK_DEFAULT;
  data.logFiles = LOG_FILES_DEFAULT;
  data.logWeb = LOG_WEBDAV_DEFAULT;
  data.secMin = SEC_WPA2;
  data.cipherMin = CIPH_AES;
  memset(data.ip, 0, sizeof(data.ip));
  memset(data.subnet, 0, sizeof(data.subnet));
  memset(data.gateway, 0, sizeof(data.gateway));
  memset(data.dns, 0, sizeof(data.dns));
  data.tz[0] = '\0';
  data.name[0] = '\0';
  data.https = 0;
  data.domain[0] = '\0';
  data.auth = 0;
  data.davUser[0] = '\0';
  data.davPsw[0] = '\0';
  data.printerWait = SPI_BLOCKOUT_PERIOD / 1000;
  data.led = LEDMODE_AUTO;
  data.bootWait = BOOT_WAIT_DEFAULT;
}

// Make sure data loaded from EEPROM (possibly written by older firmware) is sane
void Config::validate() {
  data.ssid[WIFI_SSID_LEN - 1] = '\0';
  data.psw[WIFI_PASSWD_LEN - 1] = '\0';
  data.name[DEVICE_NAME_LEN] = '\0';
  for(int i = 0; data.name[i]; i++) {
    char c = data.name[i];
    if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      data.name[0] = '\0';
      break;
    }
  }
  if(data.ver == 2) {
    // version 2 had one LOG level (0 off, 1 actions, 2 verbose) and no per-area levels
    uint8_t old = data.logNet;
    data.logNet   = old == 0 ? LOG_OFF : (old == 1 ? LOG_NORMAL : LOG_VERBOSE);
    data.logFiles = data.logNet;
    data.logWeb   = old == 0 ? LOG_OFF : (old == 1 ? LOG_SPARSE : LOG_VERBOSE);
    data.ver = 3;
  }
  if(data.ver == 3) {
    // version 3 had no Wi-Fi security settings
    data.secMin = SEC_WPA2;
    data.cipherMin = CIPH_AES;
    data.ver = 4;
  }
  if(data.ver == 4) {
    // version 4 had no HTTPS setting and didn't keep the domain
    data.https = 0;
    data.domain[0] = '\0';
    data.ver = 5;
  }
  if(data.ver == 5) {
    // version 5 had no login settings
    data.auth = 0;
    data.davUser[0] = '\0';
    data.davPsw[0] = '\0';
    data.ver = 6;
  }
  if(data.ver == 6) {
    // version 6 had a fixed 20 s wait after the printer used the card
    data.printerWait = SPI_BLOCKOUT_PERIOD / 1000;
    data.ver = 7;
  }
  if(data.ver == 7) {
    // version 7 had no LED setting
    data.led = LEDMODE_AUTO;
    data.ver = 8;
  }
  if(data.ver == 8) {
    // version 8 had no BOOT_WAIT setting
    data.bootWait = BOOT_WAIT_DEFAULT;
    data.ver = CONFIG_VERSION;
  }
  if(data.ver != CONFIG_VERSION) {
    // saved by older firmware: keep ssid/password/name, reset the rest
    char name[DEVICE_NAME_LEN + 1];
    strcpy(name, data.name);
    setDefaults();
    strcpy(data.name, name);
  }
  data.tz[TZ_LEN - 1] = '\0';
  data.domain[DOMAIN_LEN - 1] = '\0';
  if(data.https > 1) data.https = 0;
  if(data.auth > 1) data.auth = 0;
  if(data.led > LEDMODE_OFF) data.led = LEDMODE_AUTO;
  if(data.bootWait > 60) data.bootWait = BOOT_WAIT_DEFAULT;
  if(data.printerWait < 1 || data.printerWait > 60) data.printerWait = SPI_BLOCKOUT_PERIOD / 1000;
  data.davUser[DAV_USER_LEN] = '\0';
  data.davPsw[DAV_PASSWD_LEN] = '\0';
  if(data.ipMode != IP_MODE_STATIC) data.ipMode = IP_MODE_DHCP;
  if(data.logNet > LOG_VERBOSE) data.logNet = LOG_NETWORK_DEFAULT;
  if(data.logFiles > LOG_VERBOSE) data.logFiles = LOG_FILES_DEFAULT;
  if(data.logWeb > LOG_VERBOSE) data.logWeb = LOG_WEBDAV_DEFAULT;
  if(data.secMin > SEC_WPA2) data.secMin = SEC_WPA2;
  if(data.cipherMin > CIPH_AES) data.cipherMin = CIPH_AES;
  // static IP needs at least an address and a subnet mask
  if(data.ipMode == IP_MODE_STATIC && (ip() == IPAddress(0,0,0,0) || subnet() == IPAddress(0,0,0,0))) {
    data.ipMode = IP_MODE_DHCP;
    logger.net(LOG_SPARSE, "Static IP selected but IP or SUBNET missing/invalid: using DHCP");
  }
}

// Apply one setting (same names as SETUP.INI). Used by SETUP.INI and by the serial commands.
// The key must already be upper case; the value is trimmed.
int Config::setValue(const String& sKEY, String sValue) {
  sValue.trim();
  if(sKEY == "SSID") {
    if(sValue.length()>0) {
      memset(data.ssid,'\0',WIFI_SSID_LEN);
      sValue.toCharArray(data.ssid,WIFI_SSID_LEN);
      return SET_SSID;
    }
    return SET_EMPTY;
  }
  else if(sKEY == "PASSWORD") {
    if(sValue.length()>0) {
      memset(data.psw,'\0',WIFI_PASSWD_LEN);
      sValue.toCharArray(data.psw,WIFI_PASSWD_LEN);
      return SET_PASSWORD;
    }
    return SET_EMPTY;
  }
  else if(sKEY == "HOSTNAME") {
    // Keep only characters valid in a network name: letters, digits and '-'
    sValue.toLowerCase();
    // "printer1.home.lan" -> name "printer1", domain "home.lan"
    int dot = sValue.indexOf('.');
    data.domain[0] = '\0';
    if(dot >= 0) sValue.substring(dot + 1).toCharArray(data.domain, DOMAIN_LEN);
    if(dot >= 0) sValue.remove(dot);
    int n = 0;
    for(unsigned int i = 0; i < sValue.length() && n < DEVICE_NAME_LEN; i++) {
      char c = sValue[i];
      if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
        data.name[n++] = c;
    }
    data.name[n] = '\0';
    SERIAL_ECHO("HOSTNAME: "); SERIAL_ECHOLN(data.name);
  }
  else if(sKEY == "RENAME") {
    // 1 = rename SETUP.INI to SETUP.BAK once Wi-Fi connects and settings are saved
    renameIni = (sValue.toInt() == 1);
  }
  else if(sKEY == "IP_MODE" || sKEY == "DHCP") {
    // IP_MODE=DHCP or IP_MODE=STATIC (DHCP=1 / DHCP=0 also accepted)
    sValue.toUpperCase();
    if(sKEY == "DHCP")
      data.ipMode = (sValue == "0" || sValue == "NO" || sValue == "OFF") ? IP_MODE_STATIC : IP_MODE_DHCP;
    else
      data.ipMode = (sValue == "STATIC") ? IP_MODE_STATIC : IP_MODE_DHCP;
  }
  else if(sKEY == "IP" || sKEY == "IP_ADDRESS") {
    if(!parseIp(sValue, data.ip)) return SET_BAD;
  }
  else if(sKEY == "SUBNET" || sKEY == "NETMASK") {
    if(!parseIp(sValue, data.subnet)) return SET_BAD;
  }
  else if(sKEY == "GATEWAY") {
    if(!parseIp(sValue, data.gateway)) return SET_BAD;
  }
  else if(sKEY == "DNS") {
    if(!parseIp(sValue, data.dns)) return SET_BAD;
  }
  else if(sKEY == "LOG_NETWORK" || sKEY == "LOG_FILES" || sKEY == "LOG_WEBDAV") {
    // 0 = off, 1 = sparse (errors), 2 = normal, 3 = verbose
    int l = sValue.toInt();
    uint8_t lvl = (l < 0) ? LOG_OFF : (l > LOG_VERBOSE ? LOG_VERBOSE : l);
    if(sKEY == "LOG_NETWORK")    data.logNet = lvl;
    else if(sKEY == "LOG_FILES") data.logFiles = lvl;
    else                         data.logWeb = lvl;
  }
  else if(sKEY == "WIFI_SECURITY") {
    // weakest security accepted; AUTO/blank = WPA2 (the strongest this chip supports)
    sValue.toUpperCase();
    if(sValue == "OPEN" || sValue == "NONE") data.secMin = SEC_OPEN;
    else if(sValue == "WEP")                 data.secMin = SEC_WEP;
    else if(sValue == "WPA")                 data.secMin = SEC_WPA;
    else if(sValue == "WPA2" || sValue == "AUTO" || sValue == "") data.secMin = SEC_WPA2;
    else return SET_BAD;   // e.g. WPA3, which this chip can't do
  }
  else if(sKEY == "WIFI_CIPHER") {
    // weakest algorithm accepted; AUTO/blank = AES only
    sValue.toUpperCase();
    if(sValue == "TKIP" || sValue == "TKIP/AES" || sValue == "AES/TKIP") data.cipherMin = CIPH_TKIP;
    else if(sValue == "AES" || sValue == "CCMP" || sValue == "AUTO" || sValue == "") data.cipherMin = CIPH_AES;
    else return SET_BAD;
  }
  else if(sKEY == "TZ") {
    sValue.toCharArray(data.tz, TZ_LEN);
  }
  else if(sKEY == "PRINTER_WAIT") {
    // seconds the board stays off the card after the printer last used it (1-60)
    int w = sValue.toInt();
    if(w < 1 || w > 60) return SET_BAD;
    data.printerWait = w;
  }
  else if(sKEY == "BOOT_WAIT") {
    // seconds to stay off the card after power-up (0-60), so the printer can start first
    if(sValue.length() == 0) return SET_BAD;
    int w = sValue.toInt();
    if(w < 0 || w > 60 || (w == 0 && sValue != "0")) return SET_BAD;
    data.bootWait = w;
  }
  else if(sKEY == "LED") {
    // AUTO = status patterns, ON / OFF = always on / off
    sValue.toUpperCase();
    if(sValue == "AUTO" || sValue == "") data.led = LEDMODE_AUTO;
    else if(sValue == "ON" || sValue == "1") data.led = LEDMODE_ON;
    else if(sValue == "OFF" || sValue == "0") data.led = LEDMODE_OFF;
    else return SET_BAD;
  }
  else if(sKEY == "AUTH") {
    // 1 = WebDAV and the file manager ask for DAV_USER / DAV_PASSWORD
    sValue.toUpperCase();
    if(sValue == "1" || sValue == "ON" || sValue == "YES") data.auth = 1;
    else if(sValue == "0" || sValue == "OFF" || sValue == "NO" || sValue == "") data.auth = 0;
    else return SET_BAD;
  }
  else if(sKEY == "DAV_USER") {
    if(sValue.indexOf(':') >= 0 || sValue.length() > DAV_USER_LEN) return SET_BAD;   // ':' can't be in a user name
    sValue.toCharArray(data.davUser, DAV_USER_LEN + 1);
  }
  else if(sKEY == "DAV_PASSWORD") {
    if(sValue.length() > DAV_PASSWD_LEN) return SET_BAD;
    sValue.toCharArray(data.davPsw, DAV_PASSWD_LEN + 1);
  }
  else if(sKEY == "HTTPS") {
    // 1 = file manager over https:// with a certificate the board makes itself
    sValue.toUpperCase();
    if(sValue == "1" || sValue == "ON" || sValue == "YES") data.https = 1;
    else if(sValue == "0" || sValue == "OFF" || sValue == "NO" || sValue == "") data.https = 0;
    else return SET_BAD;
  }
  else return SET_UNKNOWN;
  return SET_OK;
}

int Config::loadSD() {
  SdFat sdfat;

  SERIAL_ECHOLN("Going to load config from INI file");

  if(!sdcontrol.canWeTakeBus()) {
    SERIAL_ECHOLN("Marlin is controling the bus");
    logger.net(LOG_SPARSE, "SETUP.INI not read: printer was using the SD card");
    return -1;
  }
  sdcontrol.takeBusControl();
  
  if(!sdfat.begin(SD_CS, SPI_FULL_SPEED)) {
    SERIAL_ECHOLN("Initial SD failed");
    logger.net(LOG_SPARSE, "SETUP.INI not read: SD card init failed");
    sdcontrol.relinquishBusControl();
    return -2;
  }

  auto file = sdfat.open("SETUP.INI", O_RDONLY);  // File32 on SdFat v2, File on v1
  if (!file) {
    SERIAL_ECHOLN("Open INI file failed");
    logger.net(LOG_NORMAL, "SETUP.INI not found on card");
    sdcontrol.relinquishBusControl();
    return -3;
  }

  setDefaults();
  renameIni = false;

  // Read KEY=VALUE lines. Keys are not case sensitive; lines starting with # or ; are comments.
  int rst = 0,step = 0;
  bool firstLine = true;
  String buffer,sKEY,sValue;
  while (file.available()) { // check for EOF
    buffer = file.readStringUntil('\n');
    buffer.replace("\r", ""); // Delete all CR
    if(firstLine && buffer.startsWith("\xEF\xBB\xBF")) buffer.remove(0, 3); // UTF-8 BOM
    firstLine = false;
    buffer.trim();
    if(buffer.length() == 0) continue; // Empty line
    if(buffer[0] == '#' || buffer[0] == ';') continue; // comment
    int iS = buffer.indexOf('='); // Get the seperator
    if(iS < 0) continue; // Bad line
    sKEY = buffer.substring(0,iS);
    sKEY.trim();
    sKEY.toUpperCase();
    sValue = buffer.substring(iS+1);
    sValue.trim();
    int r = setValue(sKEY, sValue);
    if(r == SET_SSID || r == SET_PASSWORD) step++;
    else if(r == SET_EMPTY) { rst = (sKEY == "SSID") ? -4 : -5; goto FAIL; }
    else if(r == SET_UNKNOWN) logger.net(LOG_SPARSE, "SETUP.INI: unknown setting '%s' ignored", sKEY.c_str());
    else if(r == SET_BAD) logger.net(LOG_SPARSE, "SETUP.INI: bad value for %s: '%s'", sKEY.c_str(), sValue.c_str());
  }
  if(step != 2) { // We miss ssid or password
    SERIAL_ECHOLN("Please check your SSDI or PASSWORD in ini file");
    logger.net(LOG_SPARSE, "SETUP.INI: SSID or PASSWORD missing, using saved settings");
    rst = -6;
    goto FAIL;
  }

  FAIL:
  file.close();
  sdcontrol.relinquishBusControl();
  loadedFromSD = (rst == 0);
  if(rst == -4 || rst == -5) logger.net(LOG_SPARSE, "SETUP.INI: SSID or PASSWORD is empty, using saved settings");
  if(loadedFromSD) {
    validate();
    applyLogLevels();
    logger.net(LOG_NORMAL, "SETUP.INI loaded: SSID=%s HOSTNAME=%s IP_MODE=%s RENAME=%d LOG_NETWORK=%d LOG_FILES=%d LOG_WEBDAV=%d TZ=%s",
      data.ssid, deviceName(), staticIp() ? "STATIC" : "DHCP", renameIni ? 1 : 0, data.logNet, data.logFiles, data.logWeb,
      data.tz[0] ? data.tz : "UTC");
    static const char* secNames[] = {"OPEN", "WEP", "WPA", "WPA2"};
    logger.net(LOG_NORMAL, "SETUP.INI security: WIFI_SECURITY=%s (minimum) WIFI_CIPHER=%s",
      secNames[data.secMin], data.cipherMin == CIPH_AES ? "AES" : "TKIP/AES");
    logger.net(LOG_NORMAL, "SETUP.INI HTTPS=%d AUTH=%d DAV_USER=%s", data.https, data.auth, data.davUser);
    if(data.auth && !authOn())
      logger.net(LOG_SPARSE, "AUTH=1 but DAV_USER or DAV_PASSWORD is empty: login is NOT required");
    if(staticIp())
      logger.net(LOG_NORMAL, "SETUP.INI static IP: IP=%s SUBNET=%s GATEWAY=%s DNS=%s",
        ipStr(data.ip).c_str(), ipStr(data.subnet).c_str(), ipStr(data.gateway).c_str(), ipStr(data.dns).c_str());
  }
  return rst;
}

// Read just BOOT_WAIT from flash. Needed before the card is touched, i.e. before SETUP.INI can be read.
unsigned char Config::savedBootWait() {
  CONFIG_TYPE d;
  EEPROM.begin(EEPROM_SIZE);
  uint8_t *p = (uint8_t*)(&d);
  for (unsigned int i = 0; i < sizeof(d); i++) *(p + i) = EEPROM.read(i);
  EEPROM.end();
  if(d.ver != CONFIG_VERSION || d.bootWait > 60) return BOOT_WAIT_DEFAULT;
  return d.bootWait;
}

unsigned char Config::load() {
  // Try to get the config from ini file
  if(0 == loadSD())
  {
    return 1; // Return as connected before
  }

  SERIAL_ECHOLN("Going to load config from EEPROM");

  EEPROM.begin(EEPROM_SIZE);
  uint8_t *p = (uint8_t*)(&data);
  for (unsigned int i = 0; i < sizeof(data); i++)
  {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.end();

  validate();
  applyLogLevels();

  if(data.flag == 1) {
    SERIAL_ECHOLN("Going to use the old config to connect the network");
    logger.net(LOG_NORMAL, "Using settings saved in flash: SSID=%s HOSTNAME=%s IP_MODE=%s",
      data.ssid, deviceName(), staticIp() ? "STATIC" : "DHCP");
    return 1;
  }
  SERIAL_ECHOLN("We didn't connect the network before");
  logger.net(LOG_SPARSE, "No SETUP.INI and no saved settings: use M50/M51/M52 or add SETUP.INI");
  return 0;
}

void Config::applyLogLevels() {
  sdcontrol.setWaitMs(data.printerWait * 1000UL);
  StatusLed::setMode(data.led);
  logger.setLevels(data.logNet, data.logFiles, data.logWeb);
}

const char* Config::deviceDomain() {
  return data.domain[0] ? data.domain : "local";
}

const char* Config::deviceName() {
  return data.name[0] ? data.name : HOSTNAME;
}

char* Config::ssid() {
  return data.ssid;
}

void Config::ssid(char* ssid) {
  if(ssid == NULL) return;
  strncpy(data.ssid,ssid,WIFI_SSID_LEN);
}

char* Config::password() {
  return data.psw;
}

void Config::password(char* password) {
  if(password == NULL) return;
  strncpy(data.psw,password,WIFI_PASSWD_LEN);
}

void Config::save(const char*ssid,const char*password) {
  if(ssid ==NULL || password==NULL)
    return;
  strncpy(data.ssid, ssid, WIFI_SSID_LEN);
  strncpy(data.psw, password, WIFI_PASSWD_LEN);
  save();
}

void Config::save() {
  EEPROM.begin(EEPROM_SIZE);
  data.flag = 1;
  data.ver = CONFIG_VERSION;
  uint8_t *p = (uint8_t*)(&data);
  for (unsigned int i = 0; i < sizeof(data); i++)
  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.end();  // commits only if something changed
}

// Save the IP address to ip.gcode on the card (printing it shows the IP on the printer's screen)
int Config::save_ip(const char *ip) {
  char buf[21] = "M117 ";
  strncat(buf,ip,15);
  if(!dav.sdWriteFile("ip.gcode", buf)) {
    logger.net(LOG_SPARSE, "Could not write ip.gcode");
    return -3;
  }
  return 0;
}

// Rename SETUP.INI to SETUP.BAK if RENAME=1 was set, so the card is "disarmed".
// Called only after Wi-Fi connected and the settings were saved to EEPROM.
int Config::disarmSD() {
  if(!disarmPending()) return 0;

  if(!dav.sdRename("SETUP.INI", "SETUP.BAK")) {
    logger.net(LOG_SPARSE, "RENAME=1: renaming SETUP.INI to SETUP.BAK FAILED");
    renameIni = false;   // don't retry forever
    return -3;
  }
  logger.net(LOG_NORMAL, "RENAME=1: SETUP.INI renamed to SETUP.BAK (settings are saved in flash)");
  loadedFromSD = false;
  return 0;
}

// All settings in SETUP.INI form, for M50 (the password is not shown)
void Config::printSettings() {
  static const char* secNames[] = {"OPEN", "WEP", "WPA", "WPA2"};
  Serial.printf("SSID=%s\n", data.ssid);
  Serial.printf("PASSWORD=%s\n", data.psw[0] ? "(set, hidden)" : "");
  Serial.printf("WIFI_SECURITY=%s\n", secNames[data.secMin]);
  Serial.printf("WIFI_CIPHER=%s\n", data.cipherMin == CIPH_AES ? "AES" : "TKIP/AES");
  if(data.domain[0]) Serial.printf("HOSTNAME=%s.%s\n", deviceName(), data.domain);
  else               Serial.printf("HOSTNAME=%s\n", deviceName());
  Serial.printf("RENAME=%d\n", renameIni ? 1 : 0);
  Serial.printf("IP_MODE=%s\n", staticIp() ? "STATIC" : "DHCP");
  Serial.printf("IP=%s\n", ipStr(data.ip).c_str());
  Serial.printf("SUBNET=%s\n", ipStr(data.subnet).c_str());
  Serial.printf("GATEWAY=%s\n", ipStr(data.gateway).c_str());
  Serial.printf("DNS=%s\n", ipStr(data.dns).c_str());
  Serial.printf("HTTPS=%d\n", data.https);
  Serial.printf("PRINTER_WAIT=%d\n", data.printerWait);
  Serial.printf("LED=%s\n", StatusLed::modeName(data.led));
  Serial.printf("BOOT_WAIT=%d\n", data.bootWait);
  Serial.printf("AUTH=%d%s\n", data.auth, (data.auth && !authOn()) ? "  (not active: DAV_USER or DAV_PASSWORD is empty)" : "");
  Serial.printf("DAV_USER=%s\n", data.davUser);
  Serial.printf("DAV_PASSWORD=%s\n", data.davPsw[0] ? "(set, hidden)" : "");
  Serial.printf("LOG_NETWORK=%d\n", data.logNet);
  Serial.printf("LOG_FILES=%d\n", data.logFiles);
  Serial.printf("LOG_WEBDAV=%d\n", data.logWeb);
  Serial.printf("TZ=%s\n", data.tz);
}

Config config;

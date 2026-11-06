#include "statusLed.h"
#include "network.h"
#include "serial.h"
#include "config.h"
#include "pins.h"
#include "ESP8266WiFi.h"
#include <ESP8266mDNS.h>
#include <time.h>
#include "ESPWebDAV.h"
#include "sdControl.h"
#include "logger.h"
#include "https.h"

String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3])  ;
}

// Names for the security details the radio reports for each scanned network
static const char* authText(int a) {
  switch(a) {
    case AUTH_OPEN:         return "open";
    case AUTH_WEP:          return "WEP";
    case AUTH_WPA_PSK:      return "WPA-PSK";
    case AUTH_WPA2_PSK:     return "WPA2-PSK";
    case AUTH_WPA_WPA2_PSK: return "WPA/WPA2-PSK";
    default:                return "unsupported (WPA3/Enterprise?)";
  }
}
static const char* cipherText(int c) {
  switch(c) {
    case CIPHER_NONE:      return "none";
    case CIPHER_WEP40:     return "WEP40";
    case CIPHER_WEP104:    return "WEP104";
    case CIPHER_TKIP:      return "TKIP";
    case CIPHER_CCMP:      return "AES-CCMP";
    case CIPHER_TKIP_CCMP: return "TKIP+AES";
    default:               return "unknown";
  }
}

// Human-readable text for the most common Wi-Fi disconnect reasons
static const char* reasonText(int r) {
  switch(r) {
    case 2:   return "auth expired";
    case 4:   return "assoc expired";
    case 8:   return "left network";
    case 15:  return "4-way handshake timeout (wrong password?)";
    case 200: return "beacon timeout (weak signal?)";
    case 201: return "network not found (SSID wrong or 5GHz-only?)";
    case 202: return "authentication failed (wrong password?)";
    case 203: return "association failed";
    case 204: return "handshake timeout (wrong password?)";
    default:  return "";
  }
}

void Network::logStatus(const char* why) {
  logger.net(LOG_NORMAL, "%s: %s, SSID=%s, IP=%s, RSSI=%d dBm, channel=%d, BSSID=%s, free heap=%u",
    why, WiFi.status() == WL_CONNECTED ? "connected" : "NOT connected",
    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel(),
    WiFi.BSSIDstr().c_str(), ESP.getFreeHeap());
}

// Scan for networks and list them with their security (serial + network.log)
void Network::scanReport() {
  int n = WiFi.scanNetworks();
  Serial.printf("Wi-Fi scan: %d networks\n", n);
  logger.net(LOG_SPARSE, "Wi-Fi scan found %d networks:", n);
  for(int i = 0; i < n && i < 15; i++) {
    // security mode, pairwise (unicast) cipher, group (broadcast) cipher and 802.11 modes
    const bss_info* bi = WiFi.getScanInfoByIndex(i);
    char sec[96];
    if(bi)
      snprintf(sec, sizeof(sec), "%s, cipher %s, group %s, 802.11%s%s%s%s",
        authText(bi->authmode), cipherText(bi->pairwise_cipher), cipherText(bi->group_cipher),
        bi->phy_11b ? "b" : "", bi->phy_11g ? "g" : "", bi->phy_11n ? "n" : "",
        bi->is_hidden ? ", hidden" : "");
    else
      snprintf(sec, sizeof(sec), "encryption type %d", (int)WiFi.encryptionType(i));
    bool match = WiFi.SSID(i) == config.ssid();
    Serial.printf("  %s\"%s\" ch %d, %d dBm, %s\n", match ? "* " : "  ", WiFi.SSID(i).c_str(),
      WiFi.channel(i), WiFi.RSSI(i), sec);
    logger.net(LOG_SPARSE, "  %s\"%s\" ch %d, %d dBm, %s", match ? "* " : "  ", WiFi.SSID(i).c_str(),
      WiFi.channel(i), WiFi.RSSI(i), sec);
  }
  WiFi.scanDelete();
}

bool Network::start() {
  wifiConnected = false;
  wifiConnecting = true;

  logger.net(LOG_NORMAL, "Starting Wi-Fi: SSID=%s, hostname=%s, %s", config.ssid(), config.deviceName(),
    config.staticIp() ? "static IP" : "DHCP");

  // Log every disconnect (with reason) and every new IP address
  onDisconnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
    logger.net(LOG_SPARSE, "Wi-Fi disconnected from %s, reason %d %s", e.ssid.c_str(), (int)e.reason, reasonText((int)e.reason));
    // also on serial, so a failed first connect can be diagnosed without the log file
    static int lastReason = -1;
    if((int)e.reason != lastReason) {
      lastReason = (int)e.reason;
      Serial.printf("\nWi-Fi: reason %d %s\n", (int)e.reason, reasonText((int)e.reason));
    }
  });
  onGotIp = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& e) {
    logger.net(LOG_NORMAL, "Got IP %s, mask %s, gateway %s", e.ip.toString().c_str(), e.mask.toString().c_str(), e.gw.toString().c_str());
  });

  // Set hostname first
  WiFi.hostname(config.deviceName());
  // Reduce startup surge current
  WiFi.setAutoConnect(false);
  WiFi.mode(WIFI_STA);
  // Let the radio pick b/g/n with the access point (forcing 11n-only fails with some routers)
  // Keep the radio awake: power-save mode slows WebDAV transfers a lot
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  // Reconnect by itself if the access point drops us
  WiFi.setAutoReconnect(true);

  if(config.staticIp()) {
    IPAddress dns = config.dns();
    if(dns == IPAddress(0,0,0,0)) dns = config.gateway();
    if(!WiFi.config(config.ip(), config.gateway(), config.subnet(), dns))
      logger.net(LOG_SPARSE, "Static IP settings were rejected, falling back to DHCP");
    else
      logger.net(LOG_NORMAL, "Using static IP %s, subnet %s, gateway %s, DNS %s", config.ip().toString().c_str(),
        config.subnet().toString().c_str(), config.gateway().toString().c_str(), dns.toString().c_str());
  }
  else {
    WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0));  // use DHCP
  }

  // Look up the network first: shows its security, enforces the WIFI_SECURITY/WIFI_CIPHER
  // minimums, and lets us join the strongest access point directly
  int32_t apChannel = 0;
  uint8_t apBssid[6];
  bool apFound = false;
  if(config.secMin() <= SEC_WEP) WiFi.enableInsecureWEP(true);
  int found = WiFi.scanNetworks(false, true, 0, (uint8*)config.ssid());
  int best = -1;
  for(int i = 0; i < found; i++)
    if(WiFi.SSID(i) == config.ssid() && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
  if(best >= 0) {
    const bss_info* bi = WiFi.getScanInfoByIndex(best);
    int auth = bi ? bi->authmode : -1;
    int pc = bi ? bi->pairwise_cipher : -1;
    Serial.printf("Wi-Fi: found \"%s\" ch %d, %d dBm, %s, cipher %s\n", config.ssid(),
      WiFi.channel(best), WiFi.RSSI(best), authText(auth), cipherText(pc));
    logger.net(LOG_NORMAL, "Found \"%s\" ch %d, %d dBm, %s, cipher %s, group %s", config.ssid(),
      WiFi.channel(best), WiFi.RSSI(best), authText(auth), cipherText(pc), bi ? cipherText(bi->group_cipher) : "?");

    // strongest security the network offers, on the WIFI_SECURITY scale
    int offered = auth == AUTH_OPEN ? SEC_OPEN : auth == AUTH_WEP ? SEC_WEP : auth == AUTH_WPA_PSK ? SEC_WPA :
                  (auth == AUTH_WPA2_PSK || auth == AUTH_WPA_WPA2_PSK) ? SEC_WPA2 : -1;
    bool aesOffered = (pc == CIPHER_CCMP || pc == CIPHER_TKIP_CCMP);
    const char* refuse = nullptr;
    if(offered < 0)                                   refuse = "it uses security this chip can't do (WPA3-only or Enterprise?)";
    else if(offered < config.secMin())                refuse = "its security is weaker than WIFI_SECURITY allows";
    else if(offered >= SEC_WPA && config.cipherMin() == CIPH_AES && !aesOffered)
                                                      refuse = "it only offers TKIP and WIFI_CIPHER=AES";
    if(refuse) {
      Serial.printf("Wi-Fi: not joining \"%s\": %s\n", config.ssid(), refuse);
      logger.net(LOG_SPARSE, "Not joining \"%s\": %s", config.ssid(), refuse);
      WiFi.scanDelete();
      wifiConnecting = false;
      return false;
    }
    apChannel = WiFi.channel(best);
    memcpy(apBssid, WiFi.BSSID(best), 6);
    apFound = true;
  }
  else {
    Serial.printf("Wi-Fi: \"%s\" not seen in a scan, trying anyway\n", config.ssid());
    logger.net(LOG_SPARSE, "\"%s\" not seen in a scan (out of range, wrong SSID, or channel 12-14?), trying anyway", config.ssid());
  }
  WiFi.scanDelete();

  // Lengths help spot stray spaces or hidden characters (the password itself is never printed)
  Serial.printf("Wi-Fi: joining \"%s\" (%d chars), password %d chars\n",
    config.ssid(), (int)strlen(config.ssid()), (int)strlen(config.password()));
  if(apFound)
    WiFi.begin(config.ssid(), config.password(), apChannel, apBssid);
  else
    WiFi.begin(config.ssid(), config.password());

  // Wait for connection
  unsigned int timeout = 0;
  while(WiFi.status() != WL_CONNECTED) {
    //blink();
    SERIAL_ECHO(".");
    if(timeout++ > WIFI_CONNECT_TIMEOUT/100) {
      SERIAL_ECHOLN("");
      int s = (int)WiFi.status();
      const char* st = s == WL_NO_SSID_AVAIL ? "network not found (SSID wrong, out of range or 5GHz-only?)" :
                       s == WL_CONNECT_FAILED ? "connect failed" :
                       s == WL_WRONG_PASSWORD ? "wrong password" :
                       s == WL_DISCONNECTED ? "disconnected" : "";
      logger.net(LOG_SPARSE, "Wi-Fi connect timed out after %lu s (status %d %s)", WIFI_CONNECT_TIMEOUT / 1000, s, st);
      Serial.printf("Wi-Fi connect failed: status %d %s\n", s, st);

      // Show what the radio can actually see, to tell "not found" from "rejected"
      WiFi.disconnect();
      scanReport();
      // write network.log to the card anyway so the failure can be read there
      if(sdcontrol.canWeTakeBus()) {
        sdcontrol.takeBusControl();
        if(dav.initSD(SD_CS, SPI_FULL_SPEED))
          logger.flush();
        sdcontrol.relinquishBusControl();
      }
      wifiConnecting = false;
      return false;
    }
    else
      delay(100);
  }

  SERIAL_ECHOLN("");
  SERIAL_ECHO("Connected to "); SERIAL_ECHOLN(config.ssid());
  SERIAL_ECHO("IP address: "); SERIAL_ECHOLN(WiFi.localIP());
  SERIAL_ECHO("RSSI: "); SERIAL_ECHOLN(WiFi.RSSI());
  SERIAL_ECHO("Mode: "); SERIAL_ECHOLN(WiFi.getPhyMode());
  SERIAL_ECHO("Asscess to SD at the Run prompt : \\\\"); SERIAL_ECHO(WiFi.localIP());SERIAL_ECHOLN("\\DavWWWRoot");
  logStatus("Wi-Fi connected");
  StatusLed::announceIpOnce(WiFi.localIP());   // blink the address once after power-up

  // Announce <name>.local on the network (Bonjour/mDNS)
  if(mdnsStarted) { MDNS.end(); mdnsStarted = false; }   // M52 reconnect, maybe with a new name
  if(MDNS.begin(config.deviceName())) {
    mdnsStarted = true;
    MDNS.addService("http", "tcp", SERVER_PORT);
    MDNS.addService("webdav", "tcp", SERVER_PORT);
    if(config.https()) MDNS.addService("https", "tcp", HTTPS_PORT);
    SERIAL_ECHO("Name: "); SERIAL_ECHO(config.deviceName()); SERIAL_ECHOLN(".local");
    logger.net(LOG_NORMAL, "mDNS name: %s.local", config.deviceName());
  }
  else {
    logger.net(LOG_SPARSE, "mDNS failed to start");
  }
  // A non-.local domain only works if your router/DNS registers DHCP hostnames
  if(strcmp(config.deviceDomain(), "local") != 0) {
    SERIAL_ECHO("Name: "); SERIAL_ECHO(config.deviceName()); SERIAL_ECHO("."); SERIAL_ECHOLN(config.deviceDomain());
  }

  // Get the time from the internet so log lines (and the HTTPS certificate) carry a real date
  applyTime();

  wifiConnected = true;
  wifiConnecting = false;

  // Settings worked, keep them in flash
  config.save();

  // Start the SD/DAV side now if the card is free, otherwise handle() retries
  davPending = true;
  busBusyLogged = false;
  SERIAL_ECHOLN("Going to start DAV server");
  startDAVServer();

  return true;
}

// Mount the card, start the DAV server, then do the card jobs that need it:
// write ip.gcode, rename SETUP.INI if RENAME=1, write buffered logs.
int Network::startDAVServer() {
  lastDavTry = millis();
  if(!sdcontrol.canWeTakeBus()) {
    if(!busBusyLogged) {
      logger.net(LOG_NORMAL, "SD card busy (printer using it), will keep retrying to start the WebDAV server");
      busBusyLogged = true;
    }
    return -1;
  }
  sdcontrol.takeBusControl();
  
  // start the SD DAV server
  if(!dav.init(SD_CS, SPI_FULL_SPEED, SERVER_PORT))   {
    DBG_PRINT("ERROR: "); DBG_PRINTLN("Failed to initialize SD Card");
    initFailed = true;
    // can't write the log to a card that won't mount; it stays in RAM
    logger.net(LOG_SPARSE, "SD card init FAILED, WebDAV will reject requests");
  }
  else {
    initFailed = false;
    logger.net(LOG_NORMAL, "WebDAV server started: \\\\%s\\DavWWWRoot or http://%s.local/",
      WiFi.localIP().toString().c_str(), config.deviceName());
    config.save_ip(IpAddress2String(WiFi.localIP()).c_str());
    // settings are now in EEPROM, so SETUP.INI can be disarmed if RENAME=1
    config.disarmSD();
    logger.flush();
    lastFlush = millis();
  }
  
  sdcontrol.relinquishBusControl();
  davPending = false;
  DBG_PRINTLN("FYSETC WebDAV server started");
  return 0;
}

void Network::printStatus() {
  bool up = WiFi.status() == WL_CONNECTED;
  if(up) {
    Serial.printf("Wi-Fi: connected to \"%s\", ch %d, %d dBm, BSSID %s\n", WiFi.SSID().c_str(), WiFi.channel(),
      WiFi.RSSI(), WiFi.BSSIDstr().c_str());
    Serial.printf("IP: %s (%s), subnet %s, gateway %s, DNS %s\n", WiFi.localIP().toString().c_str(),
      config.staticIp() ? "static" : "DHCP", WiFi.subnetMask().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
  }
  else {
    Serial.printf("Wi-Fi: NOT connected (status %d)%s\n", (int)WiFi.status(),
      wifiConnecting ? ", connecting" : ", M52 connects");
  }
  Serial.printf("Name: %s.local", config.deviceName());
  if(strcmp(config.deviceDomain(), "local") != 0) Serial.printf(", %s.%s", config.deviceName(), config.deviceDomain());
  Serial.println();
  const char* card = !wifiConnected ? "not started (no Wi-Fi)" : davPending ? "waiting for the printer to free the card" :
                     initFailed ? "SD card FAILED to start" : sdcontrol.reservedForPrinter() ? "card in Printer mode (M58 NETWORK to share it)" :
                     sdcontrol.canWeTakeBus() ? "ready" : "ready (printer is using the card right now)";
  Serial.printf("WebDAV: %s\n", card);
  if(up) {
    Serial.printf("  Windows: \\\\%s\\DavWWWRoot   Mac/Linux: http://%s.local/\n", WiFi.localIP().toString().c_str(), config.deviceName());
    Serial.printf("File manager: %s://%s.local/_manager\n", https.running() ? "https" : "http", config.deviceName());
  }
  Serial.printf("HTTPS: %s\n", !config.https() ? "off (HTTPS=0)" : https.running() ? "on, port 443" : "on, not started yet (M56 for details)");
  Serial.printf("Free memory: %u bytes (largest block %u)\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
  Serial.println("Build: " __DATE__ " " __TIME__);
}

void Network::applyTime() {
  if(config.timeZone()[0])
    configTime(config.timeZone(), "pool.ntp.org", "time.nist.gov");
  else
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

bool Network::isConnected() {
  return wifiConnected;
}

bool Network::isConnecting() {
  return wifiConnecting;
}

// a client is waiting and FS is ready and other SPI master is not using the bus
bool Network::ready() {
  if(!isConnected() || davPending) return false;
  
  // do it only if there is a need to read FS
	if(!dav.isClientWaiting())	return false;
	
	// Printer mode (web switch / M58): the card is left to the printer entirely
	if(sdcontrol.reservedForPrinter()) {
		dav.rejectClient("Card is in Printer mode");
		return false;
	}

	// has other master been using the bus in last few seconds
	if(!sdcontrol.canWeTakeBus()) {
		dav.rejectClient(sdcontrol.diagSummary()); // DIAGNOSTIC BUILD: was "Printer is using the card"
		return false;
	}

	// the card didn't start earlier (e.g. the printer had it): try again now
	if(initFailed) {
	  sdcontrol.takeBusControl();
	  // full speed first, then slower: the lines now also reach the printer's slot
	  static const SdSpeed_t speeds[] = { SPI_FULL_SPEED, SPI_HALF_SPEED, SPI_QUARTER_SPEED };
	  int tries = 0;
	  for(const SdSpeed_t& sp : speeds) {
	    tries++;
	    initFailed = !dav.initSD(SD_CS, sp);
	    if(!initFailed) break;
	  }
	  char why[64] = "";
	  if(initFailed) snprintf(why, sizeof(why), "_err0x%02X_data0x%02X_tried%d",
	                          (unsigned)(dav.sdErrorCode() >> 8), (unsigned)(dav.sdErrorCode() & 0xFF), tries);
	  sdcontrol.relinquishBusControl();
	  if(initFailed) {
	    dav.rejectClient((String("Failed to initialize SD Card") + why).c_str());
	    return false;
	  }
	  logger.net(LOG_NORMAL, "SD card started");
	}

	return true;
}

// Take the card; if the printer used it since we last did, read it again from scratch
// (the printer may have reset the card or changed files)
void Network::takeCard() {
  sdcontrol.takeBusControl();
  if(sdcontrol.takePrinterTouched()) {
    if(!dav.initSD(SD_CS, SPI_FULL_SPEED))
      logger.net(LOG_SPARSE, "SD card did not answer after the printer used it");
  }
}

void Network::handle() {
  if(wifiConnected) MDNS.update();

  // DAV server couldn't start yet because the printer had the card
  if(wifiConnected && davPending && millis() - lastDavTry > DAV_RETRY_MS)
    startDAVServer();

  // periodic Wi-Fi status: every minute at LOG_NETWORK=3, every 15 minutes at 2, never below
  unsigned long statusEvery = (logger.networkLevel() >= LOG_VERBOSE ? 60UL : 900UL) * 1000UL;
  if(wifiConnected && logger.networkLevel() >= LOG_NORMAL && millis() - lastStatus > statusEvery) {
    lastStatus = millis();
    logStatus("Status");
  }

  // HTTPS server follows the HTTPS setting; its TLS handshakes happen before the card is taken
  https.handle(wifiConnected && WiFi.isConnected());
  if(wifiConnected && !davPending) dav.acceptSecure();

  if(network.ready()) {
	  takeCard();
	  dav.handleClient();
	  if(!dav.gaveBusBack())
	    logger.flush();
	  lastFlush = millis();
	  sdcontrol.relinquishBusControl();
	}
  else if(!davPending && !initFailed && logger.pending() && millis() - lastFlush > LOG_FLUSH_MS
          && sdcontrol.canWeTakeBus()) {
    lastFlush = millis();
    takeCard();
    logger.flush();
    sdcontrol.relinquishBusControl();
  }
}

Network network;

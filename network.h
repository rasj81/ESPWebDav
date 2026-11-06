#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <ESP8266WiFi.h>

#define HOSTNAME		"FYSETC"
#define SERVER_PORT		80

#define WIFI_CONNECT_TIMEOUT 30000UL
#define DAV_RETRY_MS         2000UL     // retry starting the DAV server while the printer holds the card
#define LOG_FLUSH_MS         5000UL     // write buffered log lines this often (when the card is free)

class Network {
public:
  Network() { initFailed = false; wifiConnecting = true; wifiConnected = false; davPending = false; }
  bool start();
  int startDAVServer();
  bool isConnected();
  bool isConnecting();
  void handle();
  bool ready();
  void scanReport();
  void printStatus();        // M53
  void applyTime();          // (re)start internet time with the TZ setting

private:
  void logStatus(const char* why);
  void takeCard();

  bool wifiConnected;
  bool wifiConnecting;
  bool initFailed;
  bool davPending;           // Wi-Fi is up but the DAV server/SD card isn't started yet
  bool busBusyLogged = false;
  unsigned long lastDavTry = 0;
  unsigned long lastFlush = 0;
  unsigned long lastStatus = 0;
  WiFiEventHandler onDisconnected, onGotIp;
  bool mdnsStarted = false;
};

extern Network network;

#endif

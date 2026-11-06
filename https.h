#pragma once
// HTTPS for the file manager: a self-signed certificate made on the board and a TLS
// server on port 443. The certificate and its key are kept in flash (not on the card).
#include <Arduino.h>
#include "certgen.h"
#if __has_include(<core_version.h>)
  #include <core_version.h>
#endif

// HTTPS needs esp8266 core 3.x (BearSSL server + second stack for the crypto)
#if defined(ARDUINO_ESP8266_MAJOR) && ARDUINO_ESP8266_MAJOR >= 3
  #define HTTPS_SUPPORTED 1
  #include <WiFiServerSecure.h>
#else
  #define HTTPS_SUPPORTED 0
#endif

#define HTTPS_PORT        443
#define CERT_EEPROM_OFS   1024     // config uses the start of the EEPROM area
#define CERT_MAX_DER      900
#define CERT_DAYS         825      // longest validity Apple devices accept
#define CERT_RENEW_DAYS   30       // make a new one this close to expiry
#define HTTPS_CLOCK_WAIT  20000UL  // wait this long for the internet time before making a certificate

class HttpsService {
public:
  void handle(bool wifiUp);   // starts/stops the server to follow the HTTPS setting
  bool running();
  void printInfo();           // M56
  bool renew();               // M56 NEW: new key and certificate now
  // the certificate, for the "download certificate" link (nullptr if none)
  const uint8_t* certDer(size_t& len);

private:
#if HTTPS_SUPPORTED
  bool start();
  void stop();
  bool loadStored();
  bool create();
  String namesKey();
  void fillSubject(CertSubject& s, String names[]);

  BearSSL::WiFiServerSecure* server = nullptr;
  BearSSL::X509List* chain = nullptr;
  BearSSL::PrivateKey* key = nullptr;
  BearSSL::ServerSessions* cache = nullptr;
#endif
  bool failed = false;          // don't retry a failed start every loop
  unsigned long wifiSince = 0;  // when Wi-Fi came up (0 = down)
  uint32_t certHash = 0;        // names + IP the running certificate was made for
  uint8_t* der = nullptr;       // certificate DER (kept for the download link)
  size_t derLen = 0;
  uint32_t notBefore = 0, notAfter = 0;
};

extern HttpsService https;

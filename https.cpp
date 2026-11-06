#include "https.h"
#include "certgen.h"
#include "config.h"
#include "logger.h"
#include "ESPWebDAV.h"
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <time.h>

HttpsService https;

#if HTTPS_SUPPORTED
#include <StackThunk.h>
#include <bearssl/bearssl.h>

#define CERT_MAGIC 0x31435745UL   // "EWC1"

// What is kept in flash after the config settings
struct CertBlob {
  uint32_t magic;
  uint32_t namesHash;            // which names/IP the certificate was made for
  uint32_t notBefore, notAfter;
  uint8_t priv[32];              // P-256 private key
  uint8_t pub[65];               // P-256 public key (uncompressed)
  uint8_t pad;
  uint16_t derLen;
  uint8_t der[CERT_MAX_DER];
};
static_assert(CERT_EEPROM_OFS + sizeof(CertBlob) <= EEPROM_SIZE, "EEPROM_SIZE too small for the certificate");
static_assert(CERT_EEPROM_OFS >= sizeof(CONFIG_TYPE), "certificate would overlap the settings");

// ---- the crypto runs on BearSSL's own 6 KB stack (the Arduino loop stack is too small) ----
struct CertJob {
  int step;                      // 1 = make key, 2 = sign certificate
  uint8_t seed[48];
  uint8_t kbuf[BR_EC_KBUF_PRIV_MAX_SIZE];
  br_ec_private_key sk;
  CertSubject subj;
  uint8_t serial[16];
  uint32_t nb, na;
  CertBlob* st;
  uint8_t tbs[CERT_MAX_DER];
  bool ok;
};

extern "C" void cert_job_run(CertJob* j) {
  const br_ec_impl* ec = &br_ec_p256_m15;
  j->ok = false;
  if(j->step == 1) {
    br_hmac_drbg_context rng;
    br_hmac_drbg_init(&rng, &br_sha256_vtable, j->seed, sizeof(j->seed));
    if(br_ec_keygen(&rng.vtable, ec, &j->sk, j->kbuf, BR_EC_secp256r1) != 32) return;
    br_ec_public_key pk;
    uint8_t pbuf[BR_EC_KBUF_PUB_MAX_SIZE];
    if(br_ec_compute_pub(ec, &pk, pbuf, &j->sk) != 65) return;
    memcpy(j->st->priv, j->sk.x, 32);
    memcpy(j->st->pub, pk.q, 65);
    j->ok = true;
  }
  else {
    j->sk.x = j->kbuf;           // the struct may have moved; point back at the key bytes
    size_t tl = cg_build_tbs(j->tbs, sizeof(j->tbs), j->serial, &j->subj, j->nb, j->na, j->st->pub);
    if(!tl) return;
    br_sha256_context h;
    uint8_t hash[32], sig[80];
    br_sha256_init(&h);
    br_sha256_update(&h, j->tbs, tl);
    br_sha256_out(&h, hash);
    size_t sl = br_ecdsa_i15_sign_asn1(ec, &br_sha256_vtable, hash, &j->sk, sig);
    if(!sl) return;
    size_t cl = cg_build_cert(j->st->der, CERT_MAX_DER, j->tbs, tl, sig, sl);
    if(!cl) return;
    j->st->derLen = (uint16_t)cl;
    j->ok = true;
  }
}
make_stack_thunk(cert_job_run);
extern "C" void thunk_cert_job_run(CertJob* j);

static uint32_t fnv1a(const String& s) {
  uint32_t h = 2166136261UL;
  for(unsigned i = 0; i < s.length(); i++) { h ^= (uint8_t)s[i]; h *= 16777619UL; }
  return h;
}

static bool clockOk() { return time(nullptr) > 1735689600; }   // after 2025-01-01

// Unix time of this firmware's build, used when the internet time isn't available
static uint32_t buildTime() {
  static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* d = __DATE__;      // "Nov  5 2026"
  struct tm t = {};
  t.tm_mon = (strstr(mon, String(d).substring(0, 3).c_str()) - mon) / 3;
  t.tm_mday = atoi(d + 4);
  t.tm_year = atoi(d + 7) - 1900;
  t.tm_hour = 12;
  return (uint32_t)mktime(&t);
}

// "name|domain|ip": a new certificate is made when this changes
String HttpsService::namesKey() {
  return String(config.deviceName()) + "|" + config.deviceDomain() + "|" + WiFi.localIP().toString();
}

void HttpsService::fillSubject(CertSubject& s, String names[]) {
  names[0] = config.deviceName();
  names[1] = names[0] + ".local";
  names[2] = names[0] + "." + config.deviceDomain();
  memset(&s, 0, sizeof(s));
  s.cn = names[0].c_str();
  s.dns[0] = names[0].c_str();
  s.dns[1] = names[1].c_str();
  s.dnsCount = 2;
  if(strcmp(config.deviceDomain(), "local") != 0) s.dns[s.dnsCount++] = names[2].c_str();
  IPAddress ip = WiFi.localIP();
  for(int i = 0; i < 4; i++) s.ip[i] = ip[i];
  s.hasIp = ip != IPAddress(0, 0, 0, 0);
}

// Use the certificate in flash if it still matches the names and isn't about to expire
bool HttpsService::loadStored() {
  EEPROM.begin(EEPROM_SIZE);
  const CertBlob* st = (const CertBlob*)(EEPROM.getConstDataPtr() + CERT_EEPROM_OFS);
  bool ok = st->magic == CERT_MAGIC && st->derLen > 0 && st->derLen <= CERT_MAX_DER
            && st->namesHash == fnv1a(namesKey());
  if(ok && clockOk() && (uint32_t)time(nullptr) + CERT_RENEW_DAYS * 86400UL > st->notAfter) {
    logger.net(LOG_NORMAL, "HTTPS certificate expires soon, making a new one");
    ok = false;
  }
  if(ok) {
    der = (uint8_t*)malloc(st->derLen);
    if(!der) ok = false;
  }
  if(ok) {
    memcpy(der, st->der, st->derLen);
    derLen = st->derLen;
    notBefore = st->notBefore;
    notAfter = st->notAfter;
    uint8_t kd[130];
    size_t kl = cg_build_key(kd, sizeof(kd), st->priv, st->pub);
    key = new BearSSL::PrivateKey(kd, kl);
    memset(kd, 0, sizeof(kd));
  }
  EEPROM.end();
  return ok;
}

// New key + self-signed certificate, saved to flash
bool HttpsService::create() {
  Serial.println("HTTPS: making a new certificate (a few seconds)...");
  unsigned long t0 = millis();
  CertJob* j = (CertJob*)calloc(1, sizeof(CertJob));
  CertBlob* st = (CertBlob*)calloc(1, sizeof(CertBlob));
  bool ok = j && st;
  String names[3];
  if(ok) {
    ESP.random(j->seed, sizeof(j->seed));
    uint32_t m = micros();
    for(int i = 0; i < 4; i++) j->seed[i] ^= (uint8_t)(m >> (8 * i));
    ESP.random(j->serial, sizeof(j->serial));
    fillSubject(j->subj, names);
    uint32_t now = clockOk() ? (uint32_t)time(nullptr) : buildTime();
    j->nb = now - 86400UL;                        // a day early, in case the PC's clock is behind
    j->na = j->nb + CERT_DAYS * 86400UL;
    j->st = st;

    stack_thunk_add_ref();
    j->step = 1;
    thunk_cert_job_run(j);
    ok = j->ok;
    yield();
    if(ok) {
      j->step = 2;
      thunk_cert_job_run(j);
      ok = j->ok;
    }
    stack_thunk_del_ref();
  }
  if(ok) {
    st->magic = CERT_MAGIC;
    st->namesHash = fnv1a(namesKey());
    st->notBefore = j->nb;
    st->notAfter = j->na;
    EEPROM.begin(EEPROM_SIZE);
    memcpy(EEPROM.getDataPtr() + CERT_EEPROM_OFS, st, sizeof(CertBlob));
    ok = EEPROM.end();
    if(!ok) logger.net(LOG_SPARSE, "HTTPS: could not save the certificate to flash");
  }
  if(ok) {
    der = (uint8_t*)malloc(st->derLen);
    ok = der != nullptr;
  }
  if(ok) {
    memcpy(der, st->der, st->derLen);
    derLen = st->derLen;
    notBefore = st->notBefore;
    notAfter = st->notAfter;
    uint8_t kd[130];
    size_t kl = cg_build_key(kd, sizeof(kd), st->priv, st->pub);
    key = new BearSSL::PrivateKey(kd, kl);
    memset(kd, 0, sizeof(kd));
    char a[11], b[11];
    cg_date_str(notBefore, a); cg_date_str(notAfter, b);
    Serial.printf("HTTPS: certificate made in %lu ms, valid %s to %s%s\n", millis() - t0, a, b,
      clockOk() ? "" : " (no internet time yet: dated from the firmware build)");
    logger.net(LOG_NORMAL, "HTTPS: new certificate for %s, valid %s to %s (%lu ms)", namesKey().c_str(), a, b, millis() - t0);
  }
  else {
    Serial.println("HTTPS: making the certificate FAILED");
    logger.net(LOG_SPARSE, "HTTPS: making the certificate FAILED (free heap %u)", ESP.getFreeHeap());
  }
  if(j) { memset(j, 0, sizeof(CertJob)); free(j); }
  if(st) { memset(st, 0, sizeof(CertBlob)); free(st); }
  return ok;
}

bool HttpsService::start() {
  if(!loadStored() && !create()) { stop(); return false; }
  chain = new BearSSL::X509List(der, derLen);
  if(!key || !key->isEC() || !chain->getCount()) {
    logger.net(LOG_SPARSE, "HTTPS: stored certificate or key is unreadable");
    stop();
    return false;
  }
  cache = new BearSSL::ServerSessions(4);   // lets browsers skip the slow part of reconnecting
  server = new BearSSL::WiFiServerSecure(HTTPS_PORT);
  server->setECCert(chain, BR_KEYTYPE_EC, key);
  server->setCache(cache);
  server->begin();
  certHash = fnv1a(namesKey());
  dav.setSecureServer(server);
  Serial.printf("HTTPS: file manager at https://%s.local/_manager\n", config.deviceName());
  logger.net(LOG_NORMAL, "HTTPS server started on port %d, free heap %u", HTTPS_PORT, ESP.getFreeHeap());
  return true;
}

void HttpsService::stop() {
  dav.setSecureServer(nullptr);
  if(server) { server->stop(); delete server; server = nullptr; }
  delete cache; cache = nullptr;
  delete chain; chain = nullptr;
  delete key; key = nullptr;
  free(der); der = nullptr; derLen = 0;
}

bool HttpsService::running() { return server != nullptr; }

void HttpsService::handle(bool wifiUp) {
  if(!wifiUp) { wifiSince = 0; return; }
  if(!wifiSince) wifiSince = millis() | 1;

  if(!config.https()) {
    if(server) { stop(); logger.net(LOG_NORMAL, "HTTPS server stopped (HTTPS=0)"); }
    failed = false;
    return;
  }
  if(server) {
    // a new IP from DHCP or a new name needs a new certificate (checked every 10 s)
    static unsigned long lastCheck = 0;
    if(millis() - lastCheck > 10000UL) {
      lastCheck = millis();
      if(fnv1a(namesKey()) != certHash) {
        logger.net(LOG_NORMAL, "HTTPS: name or IP changed, renewing the certificate");
        stop();
        failed = !start();
      }
    }
    return;
  }
  if(failed) return;
  // wait a little for the internet time so the certificate gets the right dates
  if(!clockOk() && millis() - wifiSince < HTTPS_CLOCK_WAIT) return;
  failed = !start();
}

bool HttpsService::renew() {
  stop();
  EEPROM.begin(EEPROM_SIZE);
  memset(EEPROM.getDataPtr() + CERT_EEPROM_OFS, 0xFF, 4);   // forget the stored one
  EEPROM.end();
  failed = false;
  if(!config.https() || !WiFi.isConnected()) {
    Serial.println("HTTPS: old certificate removed; a new one is made when HTTPS=1 and Wi-Fi is up");
    return true;
  }
  failed = !start();
  return !failed;
}

const uint8_t* HttpsService::certDer(size_t& len) {
  len = derLen;
  return der;
}

void HttpsService::printInfo() {
  Serial.printf("HTTPS setting: %s\n", config.https() ? "HTTPS=1 (on)" : "HTTPS=0 (off)");
  if(server)
    Serial.printf("Server: running on port %d, free heap %u, largest block %u\n", HTTPS_PORT,
      ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
  else if(config.https())
    Serial.println(failed ? "Server: FAILED to start (see network.log); M56 NEW tries again"
                          : "Server: not started yet (waits for Wi-Fi and the time)");
  else
    Serial.println("Server: off");
  if(!der) return;
  char a[11], b[11];
  cg_date_str(notBefore, a); cg_date_str(notAfter, b);
  String names[3];
  CertSubject s;
  fillSubject(s, names);
  Serial.printf("Certificate: %s", s.dns[0]);
  for(int i = 1; i < s.dnsCount; i++) Serial.printf(", %s", s.dns[i]);
  if(s.hasIp) Serial.printf(", %d.%d.%d.%d", s.ip[0], s.ip[1], s.ip[2], s.ip[3]);
  Serial.printf("\nValid: %s to %s\n", a, b);
  br_sha256_context h;
  uint8_t fp[32];
  br_sha256_init(&h);
  br_sha256_update(&h, der, derLen);
  br_sha256_out(&h, fp);
  Serial.print("SHA-256 fingerprint: ");
  for(int i = 0; i < 32; i++) Serial.printf(i ? ":%02X" : "%02X", fp[i]);
  Serial.printf("\nDownload it from http://%s.local/_cert to trust it on your computer\n", config.deviceName());
}

#else  // core 2.x: no HTTPS

void HttpsService::handle(bool wifiUp) {
  static bool told = false;
  if(wifiUp && config.https() && !told) {
    told = true;
    logger.net(LOG_SPARSE, "HTTPS=1 ignored: needs esp8266 core 3.x");
  }
}
bool HttpsService::running() { return false; }
bool HttpsService::renew() { return false; }
const uint8_t* HttpsService::certDer(size_t& len) { len = 0; return nullptr; }
void HttpsService::printInfo() { Serial.println("HTTPS: not available with this esp8266 core (needs 3.x)"); }

#endif

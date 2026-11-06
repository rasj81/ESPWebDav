#include "certgen.h"
#include <string.h>

// ---- minimal DER writer ----
// Each constructed element reserves 3 length bytes when opened; close() fills in the
// real (shortest) length and slides the contents back. Elements are always < 64 KB.
namespace {
struct Der {
  uint8_t* b; size_t cap; size_t n = 0; bool err = false;
  size_t stack[8]; int sp = 0;
  Der(uint8_t* buf, size_t c) : b(buf), cap(c) {}
  void put(uint8_t v) { if(n < cap) b[n++] = v; else err = true; }
  void put(const uint8_t* p, size_t len) {
    if(n + len > cap) { err = true; return; }
    memcpy(b + n, p, len); n += len;
  }
  void open(uint8_t tag) {
    put(tag);
    if(sp >= 8 || n + 3 > cap) { err = true; return; }
    stack[sp++] = n; n += 3;
  }
  void close() {
    if(err || sp == 0) { err = true; return; }
    size_t start = stack[--sp];
    size_t len = n - (start + 3);
    size_t hl = len < 128 ? 1 : (len < 256 ? 2 : 3);
    memmove(b + start + hl, b + start + 3, len);
    if(hl == 1) b[start] = (uint8_t)len;
    else if(hl == 2) { b[start] = 0x81; b[start + 1] = (uint8_t)len; }
    else { b[start] = 0x82; b[start + 1] = (uint8_t)(len >> 8); b[start + 2] = (uint8_t)len; }
    n = start + hl + len;
  }
  void prim(uint8_t tag, const uint8_t* p, size_t len) {   // short primitive element
    put(tag);
    if(len < 128) put((uint8_t)len); else { put(0x81); put((uint8_t)len); }
    put(p, len);
  }
  void str(uint8_t tag, const char* s) { prim(tag, (const uint8_t*)s, strlen(s)); }
  void uint(const uint8_t* p, size_t len) {               // unsigned big-endian INTEGER
    while(len > 1 && p[0] == 0) { p++; len--; }
    put(0x02);
    bool pad = p[0] & 0x80;
    put((uint8_t)(len + (pad ? 1 : 0)));
    if(pad) put(0);
    put(p, len);
  }
};

// Pre-encoded object identifiers (full TLV)
const uint8_t OID_ECDSA_SHA256[] = {0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x02};
const uint8_t OID_EC_PUBKEY[]    = {0x06,0x07,0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01};
const uint8_t OID_P256[]         = {0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07};
const uint8_t OID_CN[]           = {0x06,0x03,0x55,0x04,0x03};
const uint8_t OID_ORG[]          = {0x06,0x03,0x55,0x04,0x0A};
const uint8_t OID_BASIC[]        = {0x06,0x03,0x55,0x1D,0x13};
const uint8_t OID_KEYUSAGE[]     = {0x06,0x03,0x55,0x1D,0x0F};
const uint8_t OID_EXTKEYUSAGE[]  = {0x06,0x03,0x55,0x1D,0x25};
const uint8_t OID_SAN[]          = {0x06,0x03,0x55,0x1D,0x11};
const uint8_t OID_SERVERAUTH[]   = {0x06,0x08,0x2B,0x06,0x01,0x05,0x05,0x07,0x03,0x01};
const uint8_t BOOL_TRUE[]        = {0x01,0x01,0xFF};

// Days since 1970-01-01 -> civil date (H. Hinnant's algorithm)
void civil(uint32_t days, int& y, int& m, int& d) {
  long z = (long)days + 719468;
  long era = z / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

void two(char* p, int v) { p[0] = '0' + (v / 10) % 10; p[1] = '0' + v % 10; }

// UTCTime before 2050, GeneralizedTime from 2050 on (RFC 5280 4.1.2.5)
void putTime(Der& d, uint32_t t) {
  int y, mo, da;
  civil(t / 86400, y, mo, da);
  uint32_t s = t % 86400;
  char buf[16]; char* p = buf;
  if(y < 2050) { two(p, y % 100); p += 2; }
  else { two(p, y / 100); two(p + 2, y % 100); p += 4; }
  two(p, mo); two(p + 2, da); two(p + 4, s / 3600); two(p + 6, (s / 60) % 60); two(p + 8, s % 60);
  p[10] = 'Z';
  d.prim(y < 2050 ? 0x17 : 0x18, (const uint8_t*)buf, (p - buf) + 11);
}

void putName(Der& d, const char* cn) {
  d.open(0x30);
    d.open(0x31); d.open(0x30); d.put(OID_ORG, sizeof(OID_ORG)); d.str(0x0C, "ESPWebDAV"); d.close(); d.close();
    d.open(0x31); d.open(0x30); d.put(OID_CN, sizeof(OID_CN)); d.str(0x0C, cn); d.close(); d.close();
  d.close();
}
} // namespace

size_t cg_build_tbs(uint8_t* out, size_t cap, const uint8_t serial[16], const CertSubject* s,
                    uint32_t notBefore, uint32_t notAfter, const uint8_t pub[65]) {
  Der d(out, cap);
  d.open(0x30);
    d.open(0xA0); const uint8_t v3[] = {0x02,0x01,0x02}; d.put(v3, 3); d.close();   // version 3
    uint8_t sn[16]; memcpy(sn, serial, 16); sn[0] &= 0x7F; sn[0] |= 0x40;           // positive, 16 bytes
    d.uint(sn, 16);
    d.open(0x30); d.put(OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)); d.close();
    putName(d, s->cn);                                                              // issuer
    d.open(0x30); putTime(d, notBefore); putTime(d, notAfter); d.close();           // validity
    putName(d, s->cn);                                                              // subject (same: self-signed)
    d.open(0x30);                                                                   // public key
      d.open(0x30); d.put(OID_EC_PUBKEY, sizeof(OID_EC_PUBKEY)); d.put(OID_P256, sizeof(OID_P256)); d.close();
      d.open(0x03); d.put(0x00); d.put(pub, 65); d.close();
    d.close();
    d.open(0xA3); d.open(0x30);                                                     // extensions
      // basicConstraints (critical): not a CA
      d.open(0x30); d.put(OID_BASIC, sizeof(OID_BASIC)); d.put(BOOL_TRUE, 3);
        d.open(0x04); d.open(0x30); d.close(); d.close();
      d.close();
      // keyUsage (critical): digitalSignature
      d.open(0x30); d.put(OID_KEYUSAGE, sizeof(OID_KEYUSAGE)); d.put(BOOL_TRUE, 3);
        d.open(0x04); const uint8_t ku[] = {0x03,0x02,0x07,0x80}; d.put(ku, 4); d.close();
      d.close();
      // extKeyUsage: TLS server (required by Apple and Chrome)
      d.open(0x30); d.put(OID_EXTKEYUSAGE, sizeof(OID_EXTKEYUSAGE));
        d.open(0x04); d.open(0x30); d.put(OID_SERVERAUTH, sizeof(OID_SERVERAUTH)); d.close(); d.close();
      d.close();
      // subjectAltName: the names and address the board answers to
      d.open(0x30); d.put(OID_SAN, sizeof(OID_SAN));
        d.open(0x04); d.open(0x30);
          for(int i = 0; i < s->dnsCount && i < CG_MAX_DNS; i++) d.str(0x82, s->dns[i]);
          if(s->hasIp) d.prim(0x87, s->ip, 4);
        d.close(); d.close();
      d.close();
    d.close(); d.close();
  d.close();
  return d.err ? 0 : d.n;
}

size_t cg_build_cert(uint8_t* out, size_t cap, const uint8_t* tbs, size_t tbsLen,
                     const uint8_t* sig, size_t sigLen) {
  Der d(out, cap);
  d.open(0x30);
    d.put(tbs, tbsLen);
    d.open(0x30); d.put(OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)); d.close();
    d.open(0x03); d.put(0x00); d.put(sig, sigLen); d.close();
  d.close();
  return d.err ? 0 : d.n;
}

size_t cg_build_key(uint8_t* out, size_t cap, const uint8_t priv[32], const uint8_t pub[65]) {
  Der d(out, cap);
  d.open(0x30);
    const uint8_t one[] = {0x02,0x01,0x01}; d.put(one, 3);
    d.prim(0x04, priv, 32);
    d.open(0xA0); d.put(OID_P256, sizeof(OID_P256)); d.close();
    d.open(0xA1); d.open(0x03); d.put(0x00); d.put(pub, 65); d.close(); d.close();
  d.close();
  return d.err ? 0 : d.n;
}

void cg_date_str(uint32_t t, char out[11]) {
  int y, m, d;
  civil(t / 86400, y, m, d);
  out[0] = '0' + (y / 1000) % 10; out[1] = '0' + (y / 100) % 10; two(out + 2, y % 100);
  out[4] = '-'; two(out + 5, m); out[7] = '-'; two(out + 8, d); out[10] = 0;
}

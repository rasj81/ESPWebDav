// Builds a self-signed X.509 certificate (ECDSA P-256 / SHA-256) and its key in DER form.
// Plain C++ with no Arduino or crypto library code: the caller does the key
// generation and signing, so this part can be tested on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define CG_MAX_DNS 4

struct CertSubject {
  const char* cn;                // common name shown by browsers
  const char* dns[CG_MAX_DNS];   // subjectAltName DNS entries
  int dnsCount;
  uint8_t ip[4];                 // subjectAltName IP entry
  bool hasIp;
};

// The part of the certificate that gets signed. Returns its length, 0 if 'cap' is too small.
// pub is the 65-byte uncompressed P-256 point (0x04 || X || Y).
size_t cg_build_tbs(uint8_t* out, size_t cap, const uint8_t serial[16], const CertSubject* s,
                    uint32_t notBefore, uint32_t notAfter, const uint8_t pub[65]);

// Full certificate: tbs + algorithm + DER ECDSA signature. Returns length, 0 on overflow.
size_t cg_build_cert(uint8_t* out, size_t cap, const uint8_t* tbs, size_t tbsLen,
                     const uint8_t* sig, size_t sigLen);

// EC private key in SEC1 ("BEGIN EC PRIVATE KEY") DER form. Returns length, 0 on overflow.
size_t cg_build_key(uint8_t* out, size_t cap, const uint8_t priv[32], const uint8_t pub[65]);

// "YYYY-MM-DD" for a Unix time (UTC), for messages.
void cg_date_str(uint32_t t, char out[11]);

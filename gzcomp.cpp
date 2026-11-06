#include "gzcomp.h"
#include <stdlib.h>
#include <string.h>

namespace {

const int W      = 4096;          // window (max distance)
const int BUFSZ  = 2 * W;         // window + data still to compress
const int MINM   = 3;
const int MAXM   = 258;
const int HBITS  = 12;
const int HSIZE  = 1 << HBITS;
const int CHAIN  = 48;            // candidates tried per position (speed vs. size)

struct State {
  uint8_t buf[BUFSZ];
  uint16_t head[HSIZE];           // newest position + 1 for each hash (0 = none)
  uint16_t prev[W];               // previous position + 1 with the same hash
  uint8_t out[512];
  int outLen;
  uint32_t bits;                  // bit buffer, filled from the low end (deflate is LSB first)
  int nbits;
  uint32_t total;                 // compressed bytes written
  bool err;
  gz_write_fn wr;
  void* ctx;
};

void flushOut(State& s) {
  if(s.outLen && !s.err) {
    if(!s.wr(s.ctx, s.out, s.outLen)) s.err = true;
    s.total += s.outLen;
  }
  s.outLen = 0;
}
void putByte(State& s, uint8_t b) {
  s.out[s.outLen++] = b;
  if(s.outLen == (int)sizeof(s.out)) flushOut(s);
}
void putBits(State& s, uint32_t v, int n) {    // n <= 16
  s.bits |= v << s.nbits;
  s.nbits += n;
  while(s.nbits >= 8) { putByte(s, (uint8_t)s.bits); s.bits >>= 8; s.nbits -= 8; }
}
// Huffman codes are defined most-significant bit first
void putCode(State& s, uint32_t code, int n) {
  uint32_t r = 0;
  for(int i = 0; i < n; i++) { r = (r << 1) | (code & 1); code >>= 1; }
  putBits(s, r, n);
}
// fixed literal/length code (RFC 1951 3.2.6)
void putLit(State& s, int v) {
  if(v < 144)      putCode(s, 0x30 + v, 8);
  else if(v < 256) putCode(s, 0x190 + (v - 144), 9);
  else if(v < 280) putCode(s, v - 256, 7);
  else             putCode(s, 0xC0 + (v - 280), 8);
}

const uint16_t lenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const uint8_t  lenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const uint16_t distBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const uint8_t  distExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

void putMatch(State& s, int len, int dist) {
  int i = 28;
  while(lenBase[i] > len) i--;
  putLit(s, 257 + i);
  if(lenExtra[i]) putBits(s, len - lenBase[i], lenExtra[i]);
  int d = 29;
  while(distBase[d] > dist) d--;
  putCode(s, d, 5);
  if(distExtra[d]) putBits(s, dist - distBase[d], distExtra[d]);
}

uint32_t crc32Update(uint32_t crc, const uint8_t* p, size_t n) {
  static const uint32_t t[16] = {
    0x00000000,0x1DB71064,0x3B6E20C8,0x26D930AC,0x76DC4190,0x6B6B51F4,0x4DB26158,0x5005713C,
    0xEDB88320,0xF00F9344,0xD6D6A3E8,0xCB61B38C,0x9B64C2B0,0x86D3D2D4,0xA00AE278,0xBDBDF21C};
  crc = ~crc;
  while(n--) {
    crc ^= *p++;
    crc = (crc >> 4) ^ t[crc & 15];
    crc = (crc >> 4) ^ t[crc & 15];
  }
  return ~crc;
}

inline int hash3(const uint8_t* p) {
  return ((p[0] << 8) ^ (p[1] << 4) ^ p[2]) & (HSIZE - 1);
}

} // namespace

uint32_t gz_compress(gz_read_fn rd, gz_write_fn wr, gz_tick_fn tick, void* ctx) {
  State* sp = (State*)calloc(1, sizeof(State));
  if(!sp) return 0;
  State& s = *sp;
  s.wr = wr;
  s.ctx = ctx;

  static const uint8_t hdr[10] = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 2, 3};   // gzip, deflate, max compression, Unix
  for(int i = 0; i < 10; i++) putByte(s, hdr[i]);
  putBits(s, 1, 1);      // last block
  putBits(s, 1, 2);      // fixed Huffman codes

  uint32_t crc = 0, isize = 0;
  int len = 0, pos = 0;
  bool eof = false;
  int ticks = 0;

  while(!s.err) {
    // keep at least MAXM bytes ahead of pos unless the input has ended
    if(!eof && len - pos < MAXM) {
      if(pos >= W + MAXM || len == BUFSZ) {
        // slide the window down by W
        memmove(s.buf, s.buf + W, len - W);
        len -= W; pos -= W;
        for(int i = 0; i < HSIZE; i++) s.head[i] = s.head[i] > W ? s.head[i] - W : 0;
        for(int i = 0; i < W; i++)     s.prev[i] = s.prev[i] > W ? s.prev[i] - W : 0;
      }
      while(len < BUFSZ && !eof) {
        int n = rd(ctx, s.buf + len, BUFSZ - len);
        if(n < 0) { s.err = true; break; }
        if(n == 0) { eof = true; break; }
        crc = crc32Update(crc, s.buf + len, n);
        isize += n;
        len += n;
      }
      if(tick && (++ticks & 7) == 0) tick(ctx);
      continue;
    }
    if(pos >= len) break;

    int bestLen = 0, bestDist = 0;
    int avail = len - pos;
    if(avail >= MINM) {
      int h = hash3(s.buf + pos);
      int cand = s.head[h];
      int maxLen = avail < MAXM ? avail : MAXM;
      for(int tries = 0; cand && tries < CHAIN; tries++) {
        int p = cand - 1;
        int dist = pos - p;
        if(dist <= 0 || dist > W) break;
        if(s.buf[p + bestLen] == s.buf[pos + bestLen] && s.buf[p] == s.buf[pos]) {
          int l = 0;
          while(l < maxLen && s.buf[p + l] == s.buf[pos + l]) l++;
          if(l > bestLen) { bestLen = l; bestDist = dist; if(l == maxLen) break; }
        }
        int next = s.prev[p & (W - 1)];
        if(next >= cand) break;                 // stale entry from an older window
        cand = next;
      }
    }

    int step = (bestLen >= MINM) ? bestLen : 1;
    if(bestLen >= MINM) putMatch(s, bestLen, bestDist);
    else putLit(s, s.buf[pos]);
    // remember every position we pass, so later data can refer back to it
    for(int i = 0; i < step; i++, pos++) {
      if(len - pos >= MINM) {
        int h = hash3(s.buf + pos);
        s.prev[pos & (W - 1)] = s.head[h];
        s.head[h] = pos + 1;
      }
    }
  }

  putLit(s, 256);                               // end of block
  if(s.nbits) putBits(s, 0, 8 - s.nbits);       // pad to a byte
  for(int i = 0; i < 4; i++) putByte(s, (uint8_t)(crc >> (8 * i)));
  for(int i = 0; i < 4; i++) putByte(s, (uint8_t)(isize >> (8 * i)));
  flushOut(s);
  uint32_t total = s.err ? 0 : s.total;
  free(sp);
  return total;
}

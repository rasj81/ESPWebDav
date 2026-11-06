#pragma once
// Small gzip compressor (LZ77 with a 4 KB window + fixed Huffman codes), about 24 KB of RAM.
// Used to shrink a firmware .bin on the card so it fits the free flash for an update;
// the esp8266 boot loader unpacks gzip images. Plain C++, tested on a PC against gzip.
#include <stdint.h>
#include <stddef.h>

typedef int  (*gz_read_fn)(void* ctx, uint8_t* buf, size_t len);         // bytes read, 0 at end, <0 error
typedef bool (*gz_write_fn)(void* ctx, const uint8_t* buf, size_t len);  // false on error
typedef void (*gz_tick_fn)(void* ctx);                                  // called now and then (yield, progress)

// Returns the compressed size, or 0 on error (out of memory, read or write failure).
uint32_t gz_compress(gz_read_fn rd, gz_write_fn wr, gz_tick_fn tick, void* ctx);

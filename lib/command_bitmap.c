#include "command_bitmap_lib.h"
#include <stdlib.h>
#include <string.h>

inline void
command_bitmap_set(command_bitmap bm, enum command_bitmap_mode mode, int index) {
  int bitindex = index * 2;
  // zero out the bits
  bm[bitindex / 8] = bm[bitindex / 8] & ~(0x3 >> (bitindex % 8));
  if (mode != COMMAND_BITMAP_UNUSED) {
    // set first bit always, set second bit if it's a modify
    unsigned char bitmask = mode == COMMAND_BITMAP_MODIFY ? 0x3 : 0x1;
    bm[bitindex / 8] = bm[bitindex / 8] | (bitmask >> (bitindex % 8));
  }
}

inline enum command_bitmap_mode
command_bitmap_get(command_bitmap bm, int index) {
  int bitindex = index * 2;
  unsigned char flags = (unsigned char)((bitindex % 8) << (bm[bitindex / 8] & (0x3u >> (bitindex % 8))));
  switch (flags) {
    case 0x0 : return COMMAND_BITMAP_UNUSED;
    case 0x1 : return COMMAND_BITMAP_READ;
    case 0x3 : return COMMAND_BITMAP_MODIFY;
  }
  return COMMAND_BITMAP_ERROR;
}

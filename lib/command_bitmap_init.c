#include "command_bitmap_lib.h"
#include <stdlib.h>
#include <string.h>

// We need it always inlined so we can call alloca
__attribute__((always_inline)) inline command_bitmap
command_bitmap_init(unsigned long argc) {
  unsigned long bitcount = argc * 2;
  // Integer divison round up
  // https://stackoverflow.com/a/14878734/4948732
  unsigned long bytes = bitcount/sizeof(char) + (bitcount % sizeof(char) != 0);
  command_bitmap bm = alloca(bytes);
  memset(bm, 0x0, bytes);
  return bm;
}

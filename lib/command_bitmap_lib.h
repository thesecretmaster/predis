#ifndef COMMAND_BITMAP_LIB_H
#define COMMAND_BITMAP_LIB_H

enum command_bitmap_mode {
  COMMAND_BITMAP_MODIFY,
  COMMAND_BITMAP_READ,
  COMMAND_BITMAP_UNUSED,
  COMMAND_BITMAP_ERROR
};

typedef unsigned char *command_bitmap;

void command_bitmap_set(command_bitmap bm, enum command_bitmap_mode mode, int index);
enum command_bitmap_mode command_bitmap_get(command_bitmap bm, int index);

#endif

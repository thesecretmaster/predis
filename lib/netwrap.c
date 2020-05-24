#include "netwrap.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"
// Can't be packed better
struct data_wrap {
  char *end;
  char *ptr;
  char *data;
  size_t length;
  int fd;
};

#pragma GCC diagnostic pop

struct data_wrap *dw_init(int fd, size_t allocation_size) {
  struct data_wrap *dw = malloc(sizeof(struct data_wrap));
  dw->fd = fd;
  dw->length = allocation_size;
  dw->data = malloc(allocation_size + 1);
  ((char*)dw->data)[allocation_size] = '\0';
  dw->ptr = dw->data;
  dw->end = dw->data;
  return dw;
}
#include <stdio.h>
long dw_read(struct data_wrap *dw, void *buf, long size_raw, const char *end_char, char **nocopy_start, bool *nocopy_again) {
  char *buf_ptr = buf;
  size_t cpy_len;
  ssize_t recv_len;
  char *targ_ptr;
  bool early_end = false;
  long total_copied = 0;
  size_t dist_to_end;
  bool nostop = (size_raw == -1);
  size_t size = (size_t)size_raw;
  if (nocopy_start != NULL)
    *nocopy_start = dw->ptr;
  while (nostop || size > 0) {
    // First, decide how much we want to copy. We choose the smallest of:
    //   - `size`
    //   - The remaining space left in our dw->data buffer (dw->end - dw->ptr)
    //   - The distance to `end_char` in our dw->data buffer
    // If it's the last one, we also set the early_end flag because we
    // know we don't need to refill the buffer if it already has our end
    // char.
    if (dw->end < dw->ptr)
      return -5;
    dist_to_end = (size_t)(dw->end - dw->ptr);
    cpy_len = (nostop || size > dist_to_end) ? dist_to_end : size;
    // The size_t casts are acceptable here because memchr garentees that targ_ptr >= memchr
    if (end_char != NULL && (targ_ptr = memchr(dw->ptr, *end_char, dist_to_end + 1)) != NULL && cpy_len >= (size_t)((targ_ptr += 1) - dw->ptr)) {
      cpy_len = (size_t)(targ_ptr - dw->ptr);
      early_end = true;
    }
    // Move the right quanitify of data from the dw buffer to the main buffer
    if (buf != NULL && cpy_len > 0)
      memcpy(buf_ptr, dw->ptr, cpy_len);
    total_copied += cpy_len;
    dw->ptr += cpy_len;
    if (early_end) {
      if (nocopy_again != NULL)
        *nocopy_again = false;
      return total_copied;
    }
    buf_ptr += cpy_len;
    if (!nostop)
      size -= cpy_len;

    // Fetch more data into our buffer if we're out of data.
    if (dw->ptr == dw->end && (nostop || size > 0)) {
      if (dw->end == dw->data + dw->length) {
        if (nocopy_start != NULL) {
          if (nocopy_again != NULL)
            *nocopy_again = size > 0;
          return dw->end - *nocopy_start;
        }
        recv_len = recv(dw->fd, dw->data, dw->length, 0x0);
        if (recv_len > 0) {
          dw->ptr = dw->data;
          dw->end = dw->data + recv_len;
        }
      } else {
        if (dw->data + dw->length < dw->end)
          return -4;
        recv_len = recv(dw->fd, dw->end, (size_t)((dw->data + dw->length) - dw->end), 0x0);
        if (recv_len > 0)
          dw->end += recv_len;
      }
      if (recv_len == 0) {
        return -2;
      } else if (recv_len < 0) {
        return -3;
      }
    }
  }
  return -1;
}

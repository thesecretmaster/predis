#include "netwrap.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>

struct data_wrap {
  int fd;
  int length;
  char *end;
  char *ptr;
  char *data;
};

struct data_wrap *dw_init(int fd, int allocation_size) {
  struct data_wrap *dw = malloc(sizeof(struct data_wrap));
  dw->fd = fd;
  dw->length = allocation_size;
  dw->data = malloc(allocation_size + 1);
  ((char*)dw->data)[allocation_size] = '\0';
  dw->ptr = dw->data;
  dw->end = dw->data;
  return dw;
}

int dw_read(struct data_wrap *dw, void *buf, int size, const char *end_char) {
  char *buf_ptr = buf;
  int cpy_len;
  int recv_len;
  char *targ_ptr;
  bool early_end = false;
  int total_copied = 0;
  while (size > 0) {
    // First, decide how much we want to copy. We choose the smallest of:
    //   - `size`
    //   - The remaining space left in our dw->data buffer (dw->end - dw->ptr)
    //   - The distance to `end_char` in our dw->data buffer
    // If it's the last one, we also set the early_end flag because we
    // know we don't need to refill the buffer if it already has our end
    // char.
    cpy_len = size > (dw->end - dw->ptr) ? dw->end - dw->ptr : size;
    if (end_char != NULL && (targ_ptr = memchr(dw->ptr, *end_char, (dw->end - dw->ptr) + 2)) != NULL && cpy_len >= (targ_ptr += 1) - dw->ptr) {
      cpy_len = targ_ptr - dw->ptr;
      early_end = true;
    }
    // Move the right quanitify of data from the dw buffer to the main buffer
    if (buf != NULL && cpy_len > 0)
      memcpy(buf_ptr, dw->ptr, cpy_len);
    total_copied += cpy_len;
    dw->ptr += cpy_len;
    if (early_end)
      return total_copied;
    buf_ptr += cpy_len;
    size -= cpy_len;
    // Fetch more data into our buffer if we're out of data.
    if (dw->ptr == dw->end && size > 0) {
      if (dw->end == dw->data + dw->length) {
        dw->ptr = dw->data;
        dw->end = dw->data + (recv_len = recv(dw->fd, dw->data, dw->length, 0x0));
      } else {
        recv_len = recv(dw->fd, dw->end, (dw->data + dw->length) - dw->end, 0x0);
        dw->end += recv_len;
        if (recv_len == 0)
          return -2;
      }
    }
  }
  return -1;
}

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

long dw_eat(struct data_wrap *dw, long size) {
  return dw_read_copy(dw, NULL, size, NULL);
}

long dw_read_nocopy(struct data_wrap *dw, long size_raw, const char *end_char, char **start_ptr, bool *nocopy_again) {
  char *end_ptr = NULL;
  bool nostop = size_raw == -1;
  unsigned long size = size_raw > 0 ? (unsigned long)size_raw : 0;
  unsigned long remaining_bytes;
  unsigned long copy_length;
  long bytes_copied = 0;
  ssize_t recv_len;
  *nocopy_again = false;
  *start_ptr = dw->ptr;
  while (nostop || size > 0) {
    if (0 > dw->end - dw->ptr)
      return -3;
    remaining_bytes = (unsigned long)(dw->end - dw->ptr);
    copy_length = (remaining_bytes < size) || nostop ? remaining_bytes : size;
    if (end_char != NULL && (end_ptr = memchr(dw->ptr, *end_char, remaining_bytes)) != NULL)
      copy_length = (size_t)(end_ptr - dw->ptr);
    dw->ptr += copy_length;
    if (!nostop)
      size -= copy_length;
    bytes_copied += copy_length;
    if (end_ptr != NULL)
      return bytes_copied;
    if (dw->ptr == dw->end && (size != 0 || nostop)) {
      if (dw->end == dw->data + dw->length) {
        *nocopy_again = true;
        recv_len = recv(dw->fd, dw->data, dw->length, 0x0);
        if (recv_len > 0) {
          dw->ptr = dw->data;
          dw->end = dw->ptr + recv_len;
        } else {
          return recv_len;
        }
        break;
      } else {
        recv_len = recv(dw->fd, dw->end, (size_t)((dw->data + dw->length) - dw->end), 0x0);
        if (recv_len < 1) {
          return recv_len - 1;
        } else {
          dw->end += recv_len;
        }
      }
    }
  }
  return bytes_copied;
}

long dw_read_copy(struct data_wrap *dw, char *buf, long size_raw, const char *end_char) {
  char *end_ptr = NULL;
  ssize_t recv_len;
  bool nostop = size_raw == -1;
  unsigned long size = size_raw < 0 ? 0 : (unsigned long)size_raw;
  unsigned long remaining_bytes;
  unsigned long copy_length;
  long bytes_copied = 0;
  while (nostop || size > 0) {
    if (0 > dw->end - dw->ptr) {
      return -3;
    }
    remaining_bytes = (unsigned long)(dw->end - dw->ptr);
    copy_length = (remaining_bytes < size) || nostop ? remaining_bytes : size;
    if (end_char != NULL && (end_ptr = memchr(dw->ptr, *end_char, remaining_bytes)) != NULL)
      copy_length = (size_t)(end_ptr - dw->ptr);
    if (buf != NULL) {
      memcpy(buf, dw->ptr, copy_length);
      buf = buf + copy_length;
    }
    dw->ptr += copy_length;
    if (!nostop)
      size -= copy_length;
    bytes_copied += copy_length;
    if (end_ptr != NULL)
      return bytes_copied;
    if (dw->ptr == dw->end && size > 0) {
      if (dw->end == dw->data + dw->length) {
        recv_len = recv(dw->fd, dw->data, dw->length, 0x0);
        if (recv_len > 0) {
          dw->ptr = dw->data;
          dw->end = dw->ptr + recv_len;
        } else {
          return recv_len;
        }
      } else {
        recv_len = recv(dw->fd, dw->end, (size_t)((dw->data + dw->length) - dw->end), 0x0);
        if (recv_len > 0) {
          dw->end += recv_len;
        } else {
          return recv_len - 1;
        }
      }
    }
  }
  return bytes_copied;
}

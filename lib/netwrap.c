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

static void sp(char *s) {
  while (*s != '\0') {
    if (*s == '\n') {
      printf("\\n");
    } else if (*s == '\r') {
      printf("\\r");
    } else {
      printf("%c", *s);
    }
    s++;
  }
  printf("\n");
}

static void cp(char s) {
  if (s == '\n') {
    printf("\\n");
  } else if (s == '\r') {
    printf("\\r");
  } else {
    printf("%c", s);
  }
  printf("\n");
}

long dw_eat(struct data_wrap *dw, long size) {
  return dw_read_copy(dw, NULL, size, NULL);
}

long dw_read_nocopy(struct data_wrap *dw, long size_raw, const char *end_char, char **start_ptr, bool *nocopy_again) {
  *start_ptr = dw->ptr;
  char *end_ptr = NULL;
  bool nostop = size_raw == -1;
  unsigned long size = size_raw > 0 ? (unsigned long)size_raw : 0;
  unsigned long remaining_bytes;
  unsigned long copy_length;
  long bytes_copied = 0;
  ssize_t recv_len;
  *nocopy_again = false;
  while (nostop || size > 0) {
    if (0 > dw->end - dw->ptr)
      return -2;
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
          return recv_len;
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
      return -2;
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
          return recv_len;
        }
      }
    }
  }
  return bytes_copied;
}

long dw_read_legacy(struct data_wrap *dw, char *buf, long size_raw, const char *end_char, char **nocopy_start, bool *nocopy_again) {
  // Just a way of communicating with the outside, default to false
  if (nocopy_again != NULL)
    *nocopy_again = false;
  // If we're doing nocopy, we'll always start at the start.
  //   there's the exception, when we loop back around, but that's
  //   handled later.
  if (nocopy_start != NULL)
    *nocopy_start = dw->ptr;
  // If size == -1, we're in NoStop mode, meaning we simply read until we hit
  // the string in end_char
  bool nostop = size_raw == -1;
  size_t size = (size_t)size_raw;
  // This loop will circle around the buffer, UNLESS we're in nocopy mode,
  // in which case it'll need to be called again.
  // Relatedly, if nocopy_start == dw->data + dw->length, we reset nocopy_start
  // to point to the start of the buffer (dw->data) again.
  size_t dist_to_end;
  size_t cpy_len;
  size_t end_char_length = 0;
  if (end_char != NULL)
    end_char_length = strlen(end_char);
  ssize_t recv_len;
  long total_copied = 0;
  char *targ_ptr = NULL;
  // printf("DW START (nocopy = %s)\n", nocopy_start == NULL ? "false" : "true");
  while (nostop || size > 0) {
    if (dw->end < dw->ptr)
      return -5;
    // First, decide how much we want to copy. We choose the smallest of:
    //   - `size`
    //   - The remaining space left in our dw->data buffer (dw->end - dw->ptr)
    //   - The distance to `end_char` in our dw->data buffer
    dist_to_end = (size_t)(dw->end - dw->ptr);
    cpy_len = (nostop || size > dist_to_end) ? dist_to_end : size;
    // The size_t casts are acceptable here because memchr garentees that targ_ptr >= memchr
    if (end_char != NULL && (targ_ptr = memchr(dw->ptr, *end_char, cpy_len)) != NULL)
      cpy_len = (size_t)(targ_ptr - dw->ptr) - 1;

    // Second, we actually copy the data over (if there's data to copy and we got passed a real buffer)
    if (buf != NULL && cpy_len > 0) {
      // printf("We're going to copy %zu bytes\n", cpy_len);
      memcpy(buf, dw->ptr, cpy_len);
      buf = buf + cpy_len;
    }
    // printf("Moved dw->ptr by %zu\n", cpy_len);
    total_copied += cpy_len;
    dw->ptr += cpy_len;
    // If we hit the end_char, we can just stop here
    if (targ_ptr != NULL) {
      dw->ptr += 1;
      // printf("RETURN (end char)\n");
      return total_copied;
    }

    // Now, there are two possibilities:
    //   1. We undershot how much data we needed because the buffer didn't have
    //      all of the data we needed.
    //   2. We don't have a size cap so we just keep going until the end_char
    //      case returns
    //   3. We're done!

    // Now we prep for the next round. This is valid because cpy_len <= size.
    if (!nostop)
      size -= cpy_len;

    // First, no matter what mode we're in, if the buffer has more space before
    // the end, we can just use it.
    // In case 1 or 2, cpy_len = dist_to_end = dw->end - dw->ptr, and since
    // we set dw->ptr += cpy_len, then this'll be true. If we hit end_char or
    // are size capped, we would not do this because a) early return and b) size
    // > 0.
    if (dw->ptr == dw->end && (nostop || size > 0)) {
      // So, we've decided we want more data. If we're not at the end of the
      // buffer yet, it's easy, if we are we need to loop back around and make
      // an exception for nocopy.
      // printf("Fetching more data\n");

      if (dw->end == dw->data + dw->length) {
        // We're at the end! Time to start over.
        dw->ptr = dw->data;
        recv_len = recv(dw->fd, dw->data, dw->length, 0x0);
        if (recv_len > 0)
          dw->end = dw->ptr + recv_len;
        if (nocopy_start != NULL) {
          *nocopy_again = true;
          // printf("RETURN (nocopy)\n");
          return total_copied;
        }
      } else {
        if (dw->data + dw->length < dw->end)
          return -4;
        // Fill the rest of the buffer.
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
  printf("RETURN (normal)\n");
  return -1;
}

// long dw_read(struct data_wrap *dw, void *buf, long size_raw, const char *end_char, char **nocopy_start, bool *nocopy_again) {
//   char *buf_ptr = buf;
//   size_t cpy_len;
//   ssize_t recv_len;
//   char *targ_ptr;
//   bool early_end = false;
//   long total_copied = 0;
//   size_t dist_to_end;
//   bool do_return;
//   bool nostop = (size_raw == -1);
//   size_t size = (size_t)size_raw;
//   if (nocopy_start != NULL)
//     *nocopy_start = dw->ptr;
//   while (nostop || size > 0) {
//     // First, decide how much we want to copy. We choose the smallest of:
//     //   - `size`
//     //   - The remaining space left in our dw->data buffer (dw->end - dw->ptr)
//     //   - The distance to `end_char` in our dw->data buffer
//     // If it's the last one, we also set the early_end flag because we
//     // know we don't need to refill the buffer if it already has our end
//     // char.
//     if (dw->end < dw->ptr)
//       return -5;
//     dist_to_end = (size_t)(dw->end - dw->ptr);
//     cpy_len = (nostop || size > dist_to_end) ? dist_to_end : size;
//     // The size_t casts are acceptable here because memchr garentees that targ_ptr >= memchr
//     if (end_char != NULL && (targ_ptr = memchr(dw->ptr, *end_char, dist_to_end + 1)) != NULL && cpy_len >= (size_t)((targ_ptr += 1) - dw->ptr)) {
//       cpy_len = (size_t)(targ_ptr - dw->ptr);
//       early_end = true;
//     }
//     // Move the right quanitify of data from the dw buffer to the main buffer
//     if (buf != NULL && cpy_len > 0)
//       memcpy(buf_ptr, dw->ptr, cpy_len);
//     total_copied += cpy_len;
//     dw->ptr += cpy_len;
//     if (early_end) {
//       if (nocopy_again != NULL)
//         *nocopy_again = false;
//       return total_copied;
//     }
//     buf_ptr += cpy_len;
//     if (!nostop)
//       size -= cpy_len;
//
//     // Fetch more data into our buffer if we're out of data.
//     if (dw->ptr == dw->end && (nostop || size > 0)) {
//       if (dw->end == dw->data + dw->length) {
//         printf("Looping back around to the start, I think?\n");
//         do_return = false;
//         if (nocopy_start != NULL) {
//           printf("Nocopy start branch?\n");
//           if (nocopy_again != NULL)
//             *nocopy_again = size > 0;
//           if (size > 0) {
//             **nocopy_start = '\0';
//             *nocopy_start = dw->data;
//             do_return = true;
//             printf("Nocopy restart attempt\n");
//           }
//           if (!do_return)
//             return 0;
//         }
//         recv_len = recv(dw->fd, dw->data, dw->length, 0x0);
//         printf("Actual recv branch, got %zd bytes\n", recv_len);
//         if (recv_len > 0) {
//           dw->ptr = dw->data;
//           dw->end = dw->data + recv_len;
//         }
//         if (do_return) {
//           printf("RET ZERO\n");
//           return 0;
//         }
//       } else {
//         if (dw->data + dw->length < dw->end)
//           return -4;
//         printf("Time for a fresh recv! Keep filling the buffer, I think? Size %zu\n", (size_t)((dw->data + dw->length) - dw->end));
//         recv_len = recv(dw->fd, dw->end, (size_t)((dw->data + dw->length) - dw->end), 0x0);
//         printf("Just recieved %zd bytes\n", recv_len);
//         if (recv_len > 0)
//           dw->end += recv_len;
//       }
//       if (recv_len == 0) {
//         return -2;
//       } else if (recv_len < 0) {
//         return -3;
//       }
//     }
//   }
//   return -1;
// }

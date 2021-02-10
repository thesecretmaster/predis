#include "resp_parser.h"
#include "resp_parser_types.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <poll.h>

#ifdef RESP_PARSER_USE_DW

#include "resp_parser_dw.c"

#endif // RESP_PARSER_USE_DW


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct resp_spare_page {
  char *page;
  unsigned int size;
};

#pragma GCC diagnostic pop

static void print_raw(char *s) {
  while (*s != '\0') {
    if (*s == '\r')
    printf("\\r");
    else if (*s == '\n')
    printf("\\n");
    else
    printf("%c", *s);
    s++;
  }
  printf("\n");
}

struct resp_reciever_data {
  struct resp_spare_page spare_page;
  int fd;
  unsigned int pollfds_length;
  struct pollfd pollfds[];
};

int resp_reciever_label(struct resp_reciever_data *rrd) {
  return rrd->fd;
}

static inline int rp_recieve(struct resp_reciever_data *fd_data, void *buff, size_t len, int flags) {
  return recv(fd_data->fd, buff, len, flags);
}

struct resp_reciever_data *resp_initialize_reciever(int fd) {
  unsigned int pollfds_length = 1;
  struct resp_reciever_data *d = malloc(sizeof(struct resp_reciever_data) + sizeof(struct pollfd) * pollfds_length);
  d->fd = fd;
  d->spare_page.size = 0;
  d->spare_page.page = NULL;
  d->pollfds_length = pollfds_length;
  d->pollfds[0].fd = fd;
  d->pollfds[0].events = POLLIN;
  return d;
}

#define BUFSIZE 256
static const char bs_end[] = "\r\n";
// static const char *cmd_proc_net_err = "cmd_proc network error";

struct resp_allocations {
  char **allocations;
  char **argv;
  bulkstring_size_t *argv_lengths;
  long argc;
  int allocation_count;
  unsigned int tag;
};

struct resp_allocations *resp_cmd_init(unsigned int tag) {
  struct resp_allocations *allocs = malloc(sizeof(struct resp_allocations));
  allocs->allocation_count = 0;
  allocs->allocations = NULL;
  allocs->argv = NULL;
  allocs->argc = 0;
  allocs->tag = tag;
  return allocs;
}

unsigned int resp_get_tag(struct resp_allocations *allocs) {
  return allocs->tag;
}

static int process_length_internal(struct resp_reciever_data *fd_data, char *buf_ptr_orig, char *current_buffer, unsigned int current_buffer_length, char **new_buf_ptr, char **new_page, unsigned int *new_page_length, bulkstring_size_t *argc) {
  char *buf_ptr;
  ssize_t recv_val;
  unsigned int copy_length;
  *new_page = NULL;
  *new_buf_ptr = NULL;
  // printf("LENGHT PROC\n");
  do {
    buf_ptr = buf_ptr_orig;
    *argc = strtol(buf_ptr, &buf_ptr, 10);
    // printf("Got len %d\n", *argc);
    // We should also handle the error case in which the length number is
    // 123a12 (or some other invalid number)
    if (
      // the buffer couldn't hold an additional /r/n
         buf_ptr + 2 > current_buffer + current_buffer_length
      // there isn't a /r/n at the end
      || memcmp(buf_ptr, bs_end, 2) != 0
    ) {
      // printf("loopi\n");
      if (current_buffer_length < BUFSIZE) {
        // printf("moar recv in loopi (cbl: %d)\n", current_buffer_length);
        recv_val = rp_recieve(fd_data, current_buffer + current_buffer_length, (size_t)(BUFSIZE - current_buffer_length), 0x0);
        if (recv_val <= 0)
          return -1; // Network error
        // printf("recv %d bytes\n", recv_val);
        current_buffer_length += recv_val;
      } else {
        if (*new_page != NULL) {
          // Array length number is too long to fit and we've already
          // allocated a new page
          return -2;
        }
        // printf("Fresh page in loopi\n");
        // allocate a fresh page
        assert((current_buffer + current_buffer_length) - buf_ptr_orig >= 0);
        copy_length = (unsigned int)((current_buffer + current_buffer_length) - buf_ptr_orig);
        *new_page = malloc(BUFSIZE + 1);
        (*new_page)[BUFSIZE] = '\0';
        memcpy(*new_page, buf_ptr_orig, copy_length);
        recv_val = rp_recieve(fd_data, (*new_page) + copy_length, BUFSIZE - copy_length, 0x0);
        if (recv_val <= 0)
          return -2;
        *new_page_length = (unsigned int)recv_val + copy_length;
        buf_ptr = (*new_page);
        buf_ptr_orig = buf_ptr;
        current_buffer = *new_page;
        current_buffer_length = *new_page_length;
      }
    } else {
      break;
    }
  } while (true);
  // if (*new_page != NULL)
  *new_page_length = current_buffer_length;
  *new_buf_ptr = buf_ptr;
  return 0;
}

int resp_cmd_process(struct resp_reciever_data *fd_data, struct resp_allocations * const allocs) {
  unsigned int copy_length;
  // Setup first buffer or use spare page buffer
  // Read "*<length>\r\n"
  char *current_buffer;
  unsigned int current_buffer_length;
  ssize_t recv_val;
  struct resp_spare_page *spare_page = &fd_data->spare_page;
  if (spare_page->page == NULL) {
    current_buffer = malloc(BUFSIZE + 1);
    current_buffer[BUFSIZE] = '\0';
    recv_val = rp_recieve(fd_data, current_buffer, BUFSIZE, 0x0);
    if (recv_val <= 0)
      return -1; // Network error
    current_buffer_length = (unsigned int)recv_val;
  } else {
    // printf("READIN SPARE PAGE\n");
    current_buffer = spare_page->page;
    current_buffer_length = spare_page->size;
  }
  assert(current_buffer_length >= 1);
  if (current_buffer[0] != '*') {
    printf("'''not a array'''\n");
    print_raw(current_buffer);
    return -2; // Not a BS_ARRAY
  }

  long argc_raw;
  unsigned int argc;
  char *buf_ptr = current_buffer + 1;

  // Get BS array length
  // Note: Process length internal ensures that there's 2 bytes at the end
  char *fresh_page;
  char *fresh_page_ptr;
  unsigned int fresh_page_length;
  if (process_length_internal(fd_data, buf_ptr, current_buffer, current_buffer_length, &fresh_page_ptr, &fresh_page, &fresh_page_length, &argc_raw) != 0)
    return -3;
  if (argc_raw <= 0) // TODO: We can't return 0 early, we need to handle all the spare page cleanup
    return 0;
  argc = (unsigned int)argc_raw;
  // Save a malloc by allocating double the size for argv and using the second
  // half for allocation_count
  char **argv_allocations_allocation = malloc(
    (sizeof(char*) * (argc + 1)) +
    (sizeof(char*) * (argc + 1)) +
    (sizeof(bulkstring_size_t) * (argc + 1))
  );
  allocs->argv = argv_allocations_allocation;
  allocs->allocations = (argv_allocations_allocation + argc + 1);
  allocs->argv_lengths = (bulkstring_size_t*)(argv_allocations_allocation + ((argc + 1) * 2));
  allocs->allocations[0] = current_buffer;
  allocs->allocation_count = 1;
  allocs->argc = argc_raw;
  // Handle the loose allocation from process_length_internal on argc
  buf_ptr = fresh_page_ptr;
  current_buffer_length = fresh_page_length;
  if (fresh_page != NULL) {
    allocs->allocations[allocs->allocation_count] = fresh_page;
    allocs->allocation_count += 1;
    current_buffer = fresh_page;
  }
  buf_ptr += 2; // We know that the buffer can hold an additional /r/n so
                // we can just eat them. It is guaranteed by process_length_internal
  bulkstring_size_t bs_length;
  // printf("Parsing command with %d args\n", argc);
  for (unsigned int i = 0; i < argc; i++) {
    // printf("STarting loooop\n");
    if (buf_ptr >= current_buffer + current_buffer_length) {
      if (current_buffer_length == BUFSIZE) {
        // printf("C1\n");
        // Since we know that buf_ptr is pointed at or past the end, we can
        // assume that there's no data to copy -- all the data has been read.
        current_buffer = malloc(BUFSIZE + 1);
        current_buffer[BUFSIZE] = '\0';
        allocs->allocations[allocs->allocation_count] = current_buffer;
        allocs->allocation_count += 1;
        buf_ptr = current_buffer;
        recv_val = rp_recieve(fd_data, current_buffer, BUFSIZE, 0x0);
        if (recv_val <= 0)
          return -4;
        current_buffer_length = (unsigned int)recv_val;
      } else {
        // printf("C2\n");
        recv_val = rp_recieve(fd_data, current_buffer + current_buffer_length, BUFSIZE - current_buffer_length, 0x0);
        if (recv_val <= 0)
          return -5;
        current_buffer_length += recv_val;
      }
    }
    // We know that we have at least one character in the buffer
    // printf("Cureent buffer state: ");
    // print_raw(buf_ptr);
    if (buf_ptr[0] != '$')
      return -6; // Expected a bulkstring and didn't get one
    // Length reading loop
    buf_ptr += 1; // Eat the '$'
    if (process_length_internal(fd_data, buf_ptr, current_buffer, current_buffer_length, &fresh_page_ptr, &fresh_page, &fresh_page_length, &bs_length) != 0)
      return -7;
    allocs->argv_lengths[i] = bs_length;
    buf_ptr = fresh_page_ptr;
    current_buffer_length = fresh_page_length;
    if (fresh_page != NULL) {
      allocs->allocations[allocs->allocation_count] = fresh_page;
      allocs->allocation_count += 1;
      current_buffer = fresh_page;
    }

    // printf("Cureeeent buffer state: ");
    // print_raw(buf_ptr);

    // printf("Processing argument %d: Length %d\n", i, bs_length);

    buf_ptr += 2; // Handle '\r\n'. This is garenteed by process_length_internal

    // Special case for a null BS which is "$-1\r\n"
    if (bs_length < 0) {
      allocs->argv[i] = NULL;
      continue;
      // Maybe we should also be handling fetching more content? But I think
      // the if at the top of the loop does that?
    }

    // Read "<str>\r\n"
    if (buf_ptr + bs_length + 2 <= current_buffer + current_buffer_length/* there's space for (bs_length + 2) bytes after buf_ptr */) {
      // printf("D1\n");
      // The data fits in the buffer, and all that we need has already been recv'd
      allocs->argv[i] = buf_ptr;
      *(buf_ptr + bs_length) = '\0';
      buf_ptr += bs_length + 2;
    } else if (buf_ptr + bs_length + 2 < current_buffer + BUFSIZE) {
      // printf("D2\n");
      // The data fits in the buffer, but we need to recv the rest of it
      // printf("D2 start val: ");
      // print_raw(buf_ptr);
      // printf("cbuflen: %d\n", current_buffer_length);
      allocs->argv[i] = buf_ptr;
      assert((buf_ptr + bs_length + 2) - (current_buffer + current_buffer_length) >= 0);
      recv_val = rp_recieve(fd_data, current_buffer + current_buffer_length, (size_t)((buf_ptr + bs_length + 2) - (current_buffer + current_buffer_length)), MSG_WAITALL);
      if (recv_val <= 0)
        return -8;
      current_buffer_length += recv_val;
      *(buf_ptr + bs_length) = '\0';
      buf_ptr += bs_length + 2;
    } else if (bs_length + 2 < BUFSIZE){
      // printf("D3\n");
      // The data would fit into a fresh buffer, but doesn't fit into this one
      assert((current_buffer + current_buffer_length) - buf_ptr >= 0);
      copy_length = (unsigned int)((current_buffer + current_buffer_length) - buf_ptr);
      current_buffer = malloc(BUFSIZE + 1);
      current_buffer[BUFSIZE] = '\0';
      allocs->allocations[allocs->allocation_count] = current_buffer;
      allocs->allocation_count += 1;
      memcpy(current_buffer, buf_ptr, copy_length);
      assert((bs_length + 2) >= copy_length);
      recv_val = rp_recieve(fd_data, current_buffer + copy_length, (size_t)((bs_length + 2) - copy_length), MSG_WAITALL);
      if (recv_val <= 0)
        return -9;
      allocs->argv[i] = current_buffer;
      current_buffer_length = copy_length + (unsigned int)recv_val;
      *(current_buffer + bs_length) = '\0';
      buf_ptr = current_buffer + bs_length + 2;
    } else {
      // printf("D4\n");
      // The data does not fit into any sane buffer, use a bigbuf
      assert((current_buffer + current_buffer_length) - buf_ptr >= 0);
      copy_length = (unsigned int)((current_buffer + current_buffer_length) - buf_ptr);
      current_buffer = malloc((unsigned long)bs_length + 2);
      allocs->allocations[allocs->allocation_count] = current_buffer;
      allocs->allocation_count += 1;
      memcpy(current_buffer, buf_ptr, copy_length);
      recv_val = rp_recieve(fd_data, current_buffer + copy_length, (size_t)((bs_length + 2) - copy_length), MSG_WAITALL);
      if (recv_val <= 0)
        return -9;
      *(current_buffer + bs_length) = '\0';
      allocs->argv[i] = current_buffer;
      // Prep for the next iteration
      current_buffer = malloc(BUFSIZE + 1);
      current_buffer[BUFSIZE] = '\0';
      allocs->allocations[allocs->allocation_count] = current_buffer;
      allocs->allocation_count += 1;
      buf_ptr = current_buffer;
      recv_val = rp_recieve(fd_data, current_buffer, BUFSIZE, 0x0);
      if (recv_val <= 0)
        return -4;
      current_buffer_length = (unsigned int)recv_val;
    }
    // printf("Got argument ");
    // print_raw(allocs->argv[i]);
  }
  if (buf_ptr < current_buffer + current_buffer_length) {
    spare_page->page = malloc(BUFSIZE + 1);
    spare_page->page[BUFSIZE] = '\0';
    assert((current_buffer + current_buffer_length) - buf_ptr > 0);
    spare_page->size = (unsigned int)((current_buffer + current_buffer_length) - buf_ptr);
    // printf("CREATING SPARE PAGE size = %d\n", spare_page->size);
    memcpy(spare_page->page, buf_ptr, spare_page->size);
    spare_page->page[spare_page->size] = '\0';
    // print_raw(spare_page->page);
  } else {
    spare_page->page = NULL;
    spare_page->size = 0;
  }
  // printf("Done!\n");
  return 0;
}

void resp_cmd_free(struct resp_allocations * const allocs) {
  for (int i = 0; i < allocs->allocation_count - 1; i++)
    free(allocs->allocations[i]);
}

void resp_cmd_args(struct resp_allocations * const allocs, long *argc, char ***argv, bulkstring_size_t **argv_lengths) {
  *argc = allocs->argc;
  *argv = allocs->argv;
  *argv_lengths = allocs->argv_lengths;
}

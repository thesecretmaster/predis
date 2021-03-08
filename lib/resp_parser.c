#include "resp_parser.h"
#include "resp_parser_types.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/epoll.h>

#ifdef RESP_PARSER_USE_DW

#include "resp_parser_dw.c"

#endif // RESP_PARSER_USE_DW

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct resp_spare_page {
  char *page;
  unsigned int size;
};

struct resp_conn_data {
  void *data;
  struct resp_spare_page spare_page;
  int fd;
};

#pragma GCC diagnostic pop
struct resp_conn_data *resp_conn_data_init(int fd, void *data) {
  struct resp_conn_data *cd = malloc(sizeof(struct resp_conn_data));
  cd->spare_page.size = 0;
  cd->spare_page.page = NULL;
  cd->data = data;
  cd->fd = fd;
  return cd;
}

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

void resp_conn_data_prime(struct resp_conn_data *cdata, int epoll_fd) {
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLONESHOT;
  ev.data.ptr = cdata;
  epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cdata->fd, &ev);
}

#define BUFSIZE 256

static inline ssize_t rp_fill_remaining(int fd, char *buf, size_t used_length, size_t total_size) {
  assert(used_length < total_size);
  return recv(fd, buf + used_length, (size_t)(total_size - used_length), 0x0);
}

static inline ssize_t rp_fill(int fd, char *buf, unsigned int used_length, size_t fill_up_to) {
  assert(used_length <= fill_up_to);
  return recv(fd, buf + used_length, fill_up_to - used_length, MSG_WAITALL);
}

static inline char *rp_create_buffer(size_t size) {
  char *buf = malloc(size + 1);
  buf[size] = '\0';
  return buf;
}

static inline ssize_t rp_setup_buffer(int fd, char **buf) {
  *buf = rp_create_buffer(BUFSIZE);
  return recv(fd, *buf, BUFSIZE, 0x0);
}


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

static int process_length_internal(int fd_data, char *buf_ptr_orig, char *current_buffer, unsigned int current_buffer_length, char **new_buf_ptr, char **new_page, unsigned int *new_page_length, bulkstring_size_t *argc) {
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
        if ((recv_val = rp_fill_remaining(fd_data, current_buffer, current_buffer_length, BUFSIZE)) <= 0)
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
        if ((recv_val = rp_fill_remaining(fd_data, *new_page, copy_length, BUFSIZE)) <= 0)
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

enum resp_sm_state {
  RESP_SM_BS_BODY_FIT = 2,
  RESP_SM_BS_BODY_BIGBUF = 3,
  RESP_SM_BS_START_PRE = 1,
  RESP_SM_CLEAN = 0,
  RESP_SM_ARY_START = '*',
  RESP_SM_BS_START = '$',
  RESP_SM_SS_START = '+',
  RESP_SM_ERR_START = '-'
};

struct resp_sm_buf {
  char *buf;
  size_t start_ptr;
  size_t end_ptr;
  size_t length;
  long bs_length;
  size_t argc_countdown;
};

struct resp_sm {
  int fd;
  void *user_data;
  enum resp_sm_state state;
  struct resp_sm_buf data;
  struct resp_allocations *result;
};

void *resp_sm_data(struct resp_sm *sm) {
  return sm->user_data;
}

int resp_sm_fd(struct resp_sm *sm) {
  return sm->fd;
}

struct resp_sm *resp_sm_init(int fd, void *data) {
  struct resp_sm *sm = malloc(sizeof(struct resp_sm));
  sm->state = RESP_SM_CLEAN;
  sm->data.buf = NULL;
  sm->data.length = 0;
  sm->data.start_ptr = 0;
  sm->data.end_ptr = 0;
  sm->fd = fd;
  sm->result = NULL;
  sm->user_data = data;
  return sm;
}

#include <errno.h>
#define RESP_SM_MAX_CHARS_IN_SIZE 32
#define resp_sm_handle_bad_recv(recv_length) \
          if (recv_length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { \
            return RESP_SM_STATUS_EMPTY; \
          } else { \
            return RESP_SM_STATUS_CLOSED; \
          }

struct resp_allocations *resp_cmd_sm_allocs(struct resp_sm *sm) {
  return sm->result;
}

#include <stdio.h>
// #define printf_dbg(...) printf( __VA_ARGS__ );
#define printf_dbg(...)

enum resp_sm_status resp_cmd_process_sm(struct resp_sm *sm) {
  ssize_t recv_length;
  char *ptr;
  long bs_length_internal;
  printf_dbg("STARTING IN %d / %c (clean = %d)\n", sm->state, sm->state, RESP_SM_CLEAN);
  switch (sm->state) {
    case (RESP_SM_CLEAN): {
      printf_dbg("Enter CLEAN\n");
      // Two possibilites:
      // 1. No buffer
      // 2. Buffer from previous call, but in that case we are garunteed a fresh
      //    page.
      assert(sm->data.buf == NULL || sm->data.start_ptr == 0);
      // Setup the result
      sm->result = resp_cmd_init(0x0);
      // Setup a buffer unless one has already been set up
      if (sm->data.buf == NULL) {
        printf_dbg("  Allocated a new buffer\n");
        sm->data.buf = rp_create_buffer(BUFSIZE);
        sm->data.start_ptr = 0;
        sm->data.end_ptr = 0;
        sm->data.length = BUFSIZE;
      }
      // If there's no data in the buffer, fetch data
      if (sm->data.start_ptr == sm->data.end_ptr) {
        printf_dbg("  Fetch data\n");
        recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
        if (recv_length > 0) {
          sm->data.end_ptr += recv_length;
        } else resp_sm_handle_bad_recv(recv_length)
        printf_dbg("  Fetch done\n");
      }
      // If there's data in the buffer now, move stages
      printf_dbg("  SP: %d EP: %d\n", sm->data.start_ptr, sm->data.end_ptr);
      if (sm->data.buf[sm->data.start_ptr] == RESP_SM_ARY_START) {
        printf_dbg("  Checking where to go %c (incr start_ptr and move on)\n", sm->data.buf[sm->data.start_ptr]);
        sm->state = RESP_SM_ARY_START;
        printf_dbg("  Ok so this is RESP_SM_ARY_START %d\n", sm->state);
        sm->data.start_ptr += 1;
        goto case_RESP_SM_ARY_START;
      } else {
        printf_dbg("  Expected ARY_START but got %c/%d\n", sm->data.buf[sm->data.start_ptr], sm->data.buf[sm->data.start_ptr]);
        return RESP_SM_STATUS_ERROR;
      }
    }
    case (RESP_SM_ARY_START):
    case_RESP_SM_ARY_START: {
      printf_dbg("Enter ARY_START\n");
      // WARNING: This is dependent on the assumption that we're just parsing
      //          BS arrays.
      // We know that the ONLY way we get here is from RESP_SM_CLEAN so we
      // can assume we don't need to alloc a buffer.
      assert(sm->data.length - sm->data.start_ptr > RESP_SM_MAX_CHARS_IN_SIZE);
      // See if we already have the ending in memory
      printf_dbg("  Lookin for thing %lu %lu\n", sm->data.start_ptr, sm->data.end_ptr);
      if (sm->data.end_ptr - sm->data.start_ptr > 2 && memchr(sm->data.buf + sm->data.start_ptr, '\r', sm->data.end_ptr - 2 - sm->data.start_ptr) != NULL) {
        // We do already have the whole number in memory. Read it.
        sm->data.argc_countdown = strtol(sm->data.buf + sm->data.start_ptr, &ptr, 10);
        printf_dbg("  Read num from mem! %lu\n", sm->data.argc_countdown);
        // Confirm that syntax is correct
        if (!(*ptr == '\r' && *(ptr + 1) == '\n')) {
          printf_dbg("  Oof no rn :'(\n");
          return RESP_SM_STATUS_ERROR;
        }
        printf_dbg("  STart moving SP up %lu\n", sm->data.start_ptr);
        sm->data.start_ptr = (ptr + 2) - sm->data.buf;
        printf_dbg("  Finish moving SP up %lu (%c)\n", sm->data.start_ptr, sm->data.buf[sm->data.start_ptr]);
        // Check the leading character to see where go next
        if (sm->data.buf[sm->data.start_ptr] == RESP_SM_BS_START) {
          sm->result->argc = sm->data.argc_countdown;
          sm->result->allocations = malloc(sizeof(char*) * (sm->result->argc + 1));
          sm->result->argv = malloc(sizeof(char*) * sm->result->argc);
          sm->result->argv_lengths = malloc(sizeof(bulkstring_size_t) * sm->result->argc);
          sm->result->allocations[sm->result->allocation_count] = sm->data.buf;
          sm->result->allocation_count += 1;
          printf_dbg("Exit ARY_START (argc = %lu)\n", sm->result->argc);
          goto case_RESP_SM_BS_INIT;
        } else {
          return RESP_SM_STATUS_ERROR;
        }
      } else {
        printf_dbg("  Bad case. state = %d\n", sm->state);
        if (sm->data.end_ptr - sm->data.start_ptr >= RESP_SM_MAX_CHARS_IN_SIZE) {
          // If we already have MAX_CHARS_IN_SIZE in memory, give up
          return RESP_SM_STATUS_ERROR;
        } else {
          recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
          if (recv_length > 0) {
            sm->data.end_ptr += recv_length;
          } else resp_sm_handle_bad_recv(recv_length)
          goto case_RESP_SM_ARY_START;
        }
      }
    }
    case_RESP_SM_BS_INIT: {
      printf_dbg("Do BS_INIT\n");
      sm->data.bs_length = 0;
      sm->state = RESP_SM_BS_START_PRE;
      goto case_RESP_SM_BS_START_PRE;
    }
    case (RESP_SM_BS_START_PRE):
    case_RESP_SM_BS_START_PRE: {
      if (sm->data.start_ptr < sm->data.end_ptr) {
        if (sm->data.buf[sm->data.start_ptr] != '$') {
          printf_dbg("No $\n");
          return RESP_SM_STATUS_ERROR;
        }
        sm->data.start_ptr += 1;
        sm->state = RESP_SM_BS_START;
        goto case_RESP_SM_BS_START;
      } else if (sm->data.end_ptr < sm->data.length) {
        recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
        if (recv_length > 0) {
          sm->data.end_ptr += recv_length;
        } else resp_sm_handle_bad_recv(recv_length)
        goto case_RESP_SM_BS_START_PRE;
      } else {
        sm->data.buf = rp_create_buffer(BUFSIZE);
        sm->result->allocations[sm->result->allocation_count] = sm->data.buf;
        sm->result->allocation_count += 1;
        sm->data.start_ptr = 0;
        sm->data.end_ptr = 0;
        sm->data.length = BUFSIZE;
        recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
        if (recv_length > 0) {
          sm->data.end_ptr += recv_length;
        } else resp_sm_handle_bad_recv(recv_length)
        goto case_RESP_SM_BS_START_PRE;
      }
    }
    case (RESP_SM_BS_START):
    case_RESP_SM_BS_START: {
      printf_dbg("Enter BS_START\n");
      // First, parse the number (the prefix has been taken off by ARY_START)
      // See if we already have the ending in memory
      if (sm->data.end_ptr - sm->data.start_ptr > 1 && memchr(sm->data.buf + sm->data.start_ptr, '\r', sm->data.end_ptr - 1 - sm->data.start_ptr) != NULL) {
        printf_dbg("  The bs length is already in memory\n");
        // We do already have the whole number in memory. Read it.
        bs_length_internal = strtol(sm->data.buf + sm->data.start_ptr, &ptr, 10);
        printf_dbg("  Int BS len was %lu\n", sm->data.bs_length);
        sm->data.bs_length = (sm->data.bs_length * (10 * (ptr - (sm->data.buf + sm->data.start_ptr)))) + bs_length_internal;
        printf_dbg("  Final BS len was %lu\n", sm->data.bs_length);
        // Confirm that syntax is correct
        if (!(*ptr == '\r' && *(ptr + 1) == '\n')) {
          printf_dbg("  No rn after number\n");
          return RESP_SM_STATUS_ERROR;
        }
        sm->data.start_ptr = (ptr + 2) - sm->data.buf;
        printf_dbg("Exit BS_START 1\n");
        goto case_RESP_SM_BS_BODY;
      } else {
        printf_dbg("  The bs length is not in memory\n");
        if (sm->data.end_ptr - sm->data.start_ptr >= RESP_SM_MAX_CHARS_IN_SIZE) {
          // If we already have MAX_CHARS_IN_SIZE in memory, give up
          return RESP_SM_STATUS_ERROR;
        } else if (sm->data.end_ptr != sm->data.length) {
          recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
          if (recv_length > 0) {
            sm->data.end_ptr += recv_length;
          } else resp_sm_handle_bad_recv(recv_length)
          printf_dbg("Exit BS_START 2\n");
          goto case_RESP_SM_BS_START;
        } else {
          sm->data.bs_length = strtol(sm->data.buf + sm->data.start_ptr, &ptr, 10);
          printf_dbg("  Got a length of %lu. Now fetch a fresh buffer and try again\n", sm->data.bs_length);
          sm->data.buf = rp_create_buffer(BUFSIZE);
          sm->result->allocations[sm->result->allocation_count] = sm->data.buf;
          sm->result->allocation_count += 1;
          sm->data.start_ptr = 0;
          sm->data.end_ptr = 0;
          sm->data.length = BUFSIZE;
          recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
          if (recv_length > 0) {
            sm->data.end_ptr += recv_length;
          } else resp_sm_handle_bad_recv(recv_length)
          printf_dbg("Exit BS_START 3\n");
          goto case_RESP_SM_BS_START;
        }
      }
    }
    case_RESP_SM_BS_BODY: {
      printf_dbg("Enter BS_BODY\n");
      // Our only task is to read sm->data.bs_length + 2 bytes (the BS and \r\n)
      if (sm->data.length - sm->data.start_ptr >= sm->data.bs_length + 2) {
        // Yay, we can fit all the data in the current buffer!
        sm->state = RESP_SM_BS_BODY_FIT;
        printf_dbg("Go to BS_BODY_FIT\n");
        goto case_RESP_SM_BS_BODY_FIT;
      } else {
        // We can't fit it into the current buffer. Allocate a bigbuf and put it there.
        sm->state = RESP_SM_BS_BODY_BIGBUF;
        sm->data.buf = rp_create_buffer(sm->data.bs_length + 2);
        sm->result->allocations[sm->result->allocation_count] = sm->data.buf;
        sm->result->allocation_count += 1;
        sm->data.start_ptr = 0;
        sm->data.end_ptr = 0;
        sm->data.length = sm->data.bs_length + 2;
        recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
        if (recv_length > 0) {
          sm->data.end_ptr += recv_length;
        } else resp_sm_handle_bad_recv(recv_length)
        printf_dbg("Go to BS_BODY_BIGBUF\n");
        goto case_RESP_SM_BS_BODY_BIGBUF;
      }
    }
    case (RESP_SM_BS_BODY_FIT):
    case_RESP_SM_BS_BODY_FIT: {
      printf_dbg("Enter BODY_FIT\n");
      if (sm->data.end_ptr - sm->data.start_ptr >= sm->data.bs_length + 2) {
        printf_dbg("  It fits!\n");
        // Double yay, it's already here!
        // Confirm the data is valid
        if (sm->data.buf[sm->data.start_ptr + sm->data.bs_length] != '\r' || sm->data.buf[sm->data.start_ptr + sm->data.bs_length + 1] != '\n') {
          printf_dbg("  No rn\n");
          return RESP_SM_STATUS_ERROR;
        }
        sm->result->argv[sm->result->argc - sm->data.argc_countdown] = sm->data.buf + sm->data.start_ptr;
        sm->result->argv_lengths[sm->result->argc - sm->data.argc_countdown] = sm->data.bs_length;
        sm->data.start_ptr += sm->data.bs_length + 2;
        // printf_dbg("  BF debug %c\n", sm->data.buf[sm->data.start_ptr]);
        printf_dbg("Exit BODY_FIT (read: %.*s)\n", sm->result->argv_lengths[sm->result->argc - sm->data.argc_countdown], sm->result->argv[sm->result->argc - sm->data.argc_countdown]);
        goto RESP_SM_BS_BODY_FINISH;
      } else {
        // We don't have it yet, so let's get it!
        recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
        if (recv_length > 0) {
          sm->data.end_ptr += recv_length;
        } else resp_sm_handle_bad_recv(recv_length)
        goto case_RESP_SM_BS_BODY_FIT;
      }
    }
    case (RESP_SM_BS_BODY_BIGBUF):
    case_RESP_SM_BS_BODY_BIGBUF: {
      printf_dbg("Enter BODY_BIGBUF\n");
      if (sm->data.end_ptr - sm->data.start_ptr < sm->data.bs_length + 2) {
        recv_length = rp_fill_remaining(sm->fd, sm->data.buf, sm->data.end_ptr, sm->data.length);
        if (recv_length > 0) {
          sm->data.end_ptr += recv_length;
        } else resp_sm_handle_bad_recv(recv_length)
        goto case_RESP_SM_BS_BODY_BIGBUF;
      } else {
        // Allocate a fresh buffer for the next req to use
        sm->result->argv[sm->result->argc - sm->data.argc_countdown] = sm->data.buf;
        sm->result->argv_lengths[sm->result->argc - sm->data.argc_countdown] = sm->data.bs_length;
        sm->data.buf = rp_create_buffer(BUFSIZE);
        sm->result->allocations[sm->result->allocation_count] = sm->data.buf;
        sm->result->allocation_count += 1;
        sm->data.start_ptr = 0;
        sm->data.end_ptr = 0;
        sm->data.length = BUFSIZE;
        printf_dbg("Exit BODY_BIGBUF (read: %.*s)\n", sm->result->argv_lengths[sm->result->argc - sm->data.argc_countdown], sm->result->argv[sm->result->argc - sm->data.argc_countdown]);
        goto RESP_SM_BS_BODY_FINISH;
      }
    }
    default: {
      printf_dbg("THIS IS VERY BAD WE SHOULD NOT BE HERE\n");
      return RESP_SM_STATUS_ERROR;
    }
  }
  RESP_SM_BS_BODY_FINISH: {
    char *old_buf;
    printf_dbg("Enter BODY_FINISH\n");
    sm->data.argc_countdown -= 1;
    if (sm->data.argc_countdown > 0) {
      printf_dbg("More to read, go back!\n");
      goto case_RESP_SM_BS_INIT;
    } else {
      if (sm->data.start_ptr == sm->data.end_ptr) {
        sm->state = RESP_SM_CLEAN;
        sm->data.buf = NULL;
        sm->data.start_ptr = 0;
        sm->data.end_ptr = 0;
        return RESP_SM_STATUS_DONE;
      } else {
        if (sm->data.start_ptr == sm->data.end_ptr) {
          printf_dbg("Do STATUS_MORE A\n");
          sm->data.buf = NULL;
          sm->data.start_ptr = 0;
          sm->data.end_ptr = 0;
        } else {
          printf_dbg("Do STATUS_MORE B\n");
          old_buf = sm->data.buf;
          sm->data.buf = rp_create_buffer(BUFSIZE);
          memcpy(sm->data.buf, old_buf + sm->data.start_ptr, sm->data.end_ptr - sm->data.start_ptr);
          sm->data.end_ptr = sm->data.end_ptr - sm->data.start_ptr;
          sm->data.start_ptr = 0;
          sm->data.length = BUFSIZE;
        }
        sm->state = RESP_SM_CLEAN;
        return RESP_SM_STATUS_MORE;
      }
    }
  }
}

int resp_cmd_process(int epoll_fd, struct resp_allocations * const allocs, struct resp_conn_data **cdata, void **udata, int *ret_fd) {
  unsigned int copy_length;
  // Setup first buffer or use spare page buffer
  // Read "*<length>\r\n"
  char *current_buffer;
  unsigned int current_buffer_length;
  ssize_t recv_val;
  struct epoll_event ep_data;
  if (epoll_wait(epoll_fd, &ep_data, 1, -1) != 1)
    return -10;
  struct resp_conn_data *resp_cdata = ep_data.data.ptr;
  int fd_data = resp_cdata->fd;
  *ret_fd = fd_data;
  *udata = resp_cdata->data;
  *cdata = resp_cdata;
  struct resp_spare_page *spare_page = &resp_cdata->spare_page;
  if (spare_page->page == NULL) {
    if ((recv_val = rp_setup_buffer(fd_data, &current_buffer)) <= 0)
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
        recv_val = rp_setup_buffer(fd_data, &current_buffer);
        allocs->allocations[allocs->allocation_count] = current_buffer;
        allocs->allocation_count += 1;
        buf_ptr = current_buffer;
        if (recv_val <= 0)
          return -4;
        current_buffer_length = (unsigned int)recv_val;
      } else {
        // printf("C2\n");
        if ((recv_val = rp_fill_remaining(fd_data, current_buffer, current_buffer_length, BUFSIZE)) <= 0)
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
      assert(buf_ptr + bs_length + 2 >= current_buffer);
      recv_val = rp_fill(fd_data, current_buffer, current_buffer_length, (size_t)((buf_ptr + bs_length + 2) - current_buffer));
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
      recv_val = rp_fill(fd_data, current_buffer, copy_length, (size_t)bs_length + 2);
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
      recv_val = rp_fill(fd_data, current_buffer, copy_length, (size_t)bs_length + 2);
      if (recv_val <= 0)
        return -9;
      *(current_buffer + bs_length) = '\0';
      allocs->argv[i] = current_buffer;
      // Prep for the next iteration
      recv_val = rp_setup_buffer(fd_data, &current_buffer);
      allocs->allocations[allocs->allocation_count] = current_buffer;
      allocs->allocation_count += 1;
      buf_ptr = current_buffer;
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

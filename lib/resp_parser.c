#include "resp_parser.h"
#include "resp_parser_types.h"
#include <stdlib.h>
#include <stdio.h>

const char *resp_error(struct resp_response *r) {
  return r->type == PROCESSING_ERROR ? r->data.processing_error : NULL;
}

struct resp_response *resp_alloc() {
  return malloc(sizeof(struct resp_response));
}

struct resp_response_allocations {
  int allocation_count;
  void **allocations;
};

#include <sys/socket.h>
#include <string.h>
#include <assert.h>

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

#define BUFSIZE 48
static const char bs_end[] = "\r\n";
// static const char *cmd_proc_net_err = "cmd_proc network error";

struct resp_spare_page {
  char *page;
  unsigned int size;
};

struct resp_allocations {
  int allocation_count;
  int allocation_array_size; /* DEPRECATED. DO NOT USE */
  char **allocations;
  char **argv;
  int argc;
};

struct resp_allocations *resp_cmd_init() {
  struct resp_allocations *allocs = malloc(sizeof(struct resp_allocations));
  allocs->allocation_count = 0;
  allocs->allocations = NULL;
  allocs->argv = NULL;
  allocs->argc = 0;
  return allocs;
}

struct resp_spare_page *resp_cmd_init_spare_page() {
  struct resp_spare_page *page = malloc(sizeof(struct resp_spare_page));
  page->size = 0;
  page->page = NULL;
  return page;
}
// #include <pthread.h>

// static pthread_mutex_t print_lock;

static int process_length_internal(int fd, char *buf_ptr_orig, char *current_buffer, unsigned int current_buffer_length, char **new_buf_ptr, char **new_page, unsigned int *new_page_length, long *argc) {
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
        recv_val = recv(fd, current_buffer + current_buffer_length, (size_t)(BUFSIZE - current_buffer_length), 0x0);
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
        recv_val = recv(fd, (*new_page) + copy_length, BUFSIZE - copy_length, 0x0);
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

int resp_cmd_process(int fd, struct resp_allocations * const allocs, struct resp_spare_page * const spare_page) {
  unsigned int copy_length;
  // Setup first buffer or use spare page buffer
  // Read "*<length>\r\n"
  char *current_buffer;
  unsigned int current_buffer_length;
  ssize_t recv_val;
  if (spare_page->page == NULL) {
    current_buffer = malloc(BUFSIZE + 1);
    current_buffer[BUFSIZE] = '\0';
    recv_val = recv(fd, current_buffer, BUFSIZE, 0x0);
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
  if (process_length_internal(fd, buf_ptr, current_buffer, current_buffer_length, &fresh_page_ptr, &fresh_page, &fresh_page_length, &argc_raw) != 0)
    return -3;
  if (argc_raw <= 0) // TODO: We can't return 0 early, we need to handle all the spare page cleanup
    return 0;
  argc = (unsigned int)argc_raw;
  // Save a malloc by allocating double the size for argv and using the second
  // half for allocation_count
  char **argv_allocations_allocation = malloc(sizeof(char*) * (argc * 2) + 2);
  allocs->argv = argv_allocations_allocation;
  allocs->allocations = (argv_allocations_allocation + argc);
  allocs->allocations[0] = current_buffer;
  allocs->allocation_count = 1;
  allocs->argc = (int)argc_raw;
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
  long bs_length_raw;
  unsigned long bs_length;
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
        recv_val = recv(fd, current_buffer, BUFSIZE, 0x0);
        if (recv_val <= 0)
          return -4;
        current_buffer_length = (unsigned int)recv_val;
      } else {
        // printf("C2\n");
        recv_val = recv(fd, current_buffer + current_buffer_length, BUFSIZE - current_buffer_length, 0x0);
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
    if (process_length_internal(fd, buf_ptr, current_buffer, current_buffer_length, &fresh_page_ptr, &fresh_page, &fresh_page_length, &bs_length_raw) != 0)
      return -7;
    assert(bs_length_raw >= 0); // Yeah, this is putting a problem off for later. I'll deal with it later.
    bs_length = (unsigned long)bs_length_raw;
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
      recv_val = recv(fd, current_buffer + current_buffer_length, (size_t)((buf_ptr + bs_length + 2) - (current_buffer + current_buffer_length)), MSG_WAITALL);
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
      recv_val = recv(fd, current_buffer + copy_length, (size_t)((bs_length + 2) - copy_length), MSG_WAITALL);
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
      current_buffer = malloc(bs_length + 2);
      allocs->allocations[allocs->allocation_count] = current_buffer;
      allocs->allocation_count += 1;
      memcpy(current_buffer, buf_ptr, copy_length);
      recv_val = recv(fd, current_buffer + copy_length, (bs_length + 2) - copy_length, MSG_WAITALL);
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
      recv_val = recv(fd, current_buffer, BUFSIZE, 0x0);
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

#if 0
int resp_cmd_process_depr(int fd, struct resp_allocations * const allocs, struct resp_spare_page * const spare_page) {
  char *original_buffer;
  char *buf_ptr;
  char *spare_page_buf;
  char *nbuf_ptr;
  bool bigbuf;
  size_t size;
  ssize_t recv_val;
  printf("============= Starting Processing ==============\n");
  if (spare_page->page == NULL) {
    printf("Allocating initial buffer\n");
    original_buffer = malloc(BUFSIZE + 2);
    if (original_buffer == NULL)
      return -1;
    buf_ptr = original_buffer;
    recv_val = recv(fd, buf_ptr, BUFSIZE, 0x0);
    if (recv_val <= 0)
      return -2; // Connection closed
    size = (unsigned)recv_val;
    printf("Original recv: ");
    buf_ptr[size] = '\0';
    print_raw(buf_ptr);
  } else {
    original_buffer = spare_page->page;
    buf_ptr = original_buffer;
    size = spare_page->size;
    buf_ptr[size] = '\0';
    printf("Using spare page (size %d)\n", size);
  }
  if (buf_ptr[0] != '*') {
    pthread_mutex_lock(&print_lock);
    printf("Not an array??? %s %d\n", spare_page->page == NULL ? "null" : "not null", size);
    print_raw(buf_ptr);
    print_raw(original_buffer);
    pthread_mutex_unlock(&print_lock);
    return -6; // Not a bulkstring array
  }
  spare_page->page = NULL;
  spare_page->size = 0;
  buf_ptr += 1;
  allocs->argc = (int)strtol(buf_ptr, &buf_ptr, 10);
  if (allocs->argc < 1)
    return -3; // Command has no length
  printf("There are going to be %d args\n", allocs->argc);
  buf_ptr += 2; // Eat \r\n
  allocs->argv = malloc(sizeof(struct resp_response) * (unsigned)allocs->argc);
  if (allocs->argv == NULL)
    return -1;
  long bs_length;
  char *big_bs_allocation;
  allocs->allocation_array_size = allocs->argc + 1;
  allocs->allocations = malloc(sizeof(char*) * (unsigned)allocs->allocation_array_size);
  if (allocs->allocations == NULL)
    return -1;
  int allocation_idx = 0;
  allocs->allocations[allocation_idx] = original_buffer;
  allocation_idx++;
  unsigned long remaining_buffer_size;
  for (long i = 0; i < allocs->argc; i++) {
    printf("Starting parse of arg %d\n", i);
    if (i != 0 && buf_ptr >= original_buffer + BUFSIZE) {
      printf("Allocating a new buffer because the pointer is at / past the end\n");
      original_buffer = malloc(BUFSIZE + 10);
      allocs->allocations[allocation_idx] = original_buffer;
      allocation_idx++;
      buf_ptr = original_buffer;
      recv_val = recv(fd, original_buffer, BUFSIZE, 0x0);
      if (recv_val <= 0)
        return -2;
      size = (unsigned)recv_val;
    }
    if (*buf_ptr != '$')
      return -5;
    buf_ptr++;
  retry:
    bigbuf = false;
    remaining_buffer_size = BUFSIZE - (buf_ptr - original_buffer);
    // buf_ptr points to the start of the length
    bs_length = strtol(buf_ptr, &nbuf_ptr, 10);
    printf("%d: BS length %d\n", i, bs_length);
    // nbuf_ptr points to the \r\n after the length before the content
    if (memcmp(nbuf_ptr, bs_end, sizeof(bs_end) - 1) != 0) {
      printf("%d: Length overlaps buffers\n", i);
      // The length overlaps past the end of the recv'd data.
      if (size < BUFSIZE) {
        printf("%d: More data\n", i);
        // Recv more content
        recv_val = recv(fd, original_buffer + size, BUFSIZE - (unsigned)size, 0x0);
        if (recv_val <= 0)
          return -2;
        size += (unsigned)recv_val;
      } else {
        printf("%d: New buffer\n", i);
        // Allocate a new buffer
        nbuf_ptr = malloc(BUFSIZE + 1);
        if (nbuf_ptr == NULL)
          return -1;
        allocs->allocations[allocation_idx] = nbuf_ptr;
        allocation_idx++;
        // Copy the data from the old buffer to the new buffer
        memcpy(nbuf_ptr, buf_ptr, remaining_buffer_size);
        original_buffer = nbuf_ptr;
        buf_ptr = nbuf_ptr;
        recv_val = recv(fd, nbuf_ptr + remaining_buffer_size, BUFSIZE - remaining_buffer_size, 0x0);
        if (recv_val <= 0)
          return -2;
        size = remaining_buffer_size + (unsigned)recv_val;
      }
      printf("%d: Retrying\n", i);
      // Restart the loop
      goto retry;
    }
    // Eat the /r/n after the bulkstring length;
    nbuf_ptr += 2;
    remaining_buffer_size = size - (nbuf_ptr - original_buffer);
    // We have the bulkstring length, check if the bulkstring extends past the
    // available allocation
    if (original_buffer + size < nbuf_ptr + bs_length + 2) {
      printf("%d: BS isn't going to fit in current buffer\n", i);
      if (bs_length > BUFSIZE - 4) {
        printf("%d: Allocate special bigbuf\n", i);
        bigbuf = true;
        // We're gonna need a big allocation, this won't fit on a normal page
        // The content extends past the end of the buffer.
        // Allocate bs_length of space and memcpy -> recv WAIT_ALL into it
        big_bs_allocation = malloc(sizeof(char) * ((unsigned)bs_length + 2));
        if (big_bs_allocation == NULL)
          return -1;
        allocs->allocations[allocation_idx] = big_bs_allocation;
        allocation_idx++;
        memcpy(big_bs_allocation, nbuf_ptr, remaining_buffer_size);
        big_bs_allocation[bs_length] = '\0';
        recv_val = recv(fd, big_bs_allocation + remaining_buffer_size, ((unsigned)bs_length + 2) - remaining_buffer_size, MSG_WAITALL);
        if (recv_val <= 0)
          return -2;
        allocs->argv[i] = big_bs_allocation;
        // Allocate a fresh buffer for future allocations
        original_buffer = malloc(BUFSIZE + 2);
        if (original_buffer == NULL)
          return -1;
        allocs->allocations[allocation_idx] = original_buffer;
        allocation_idx++;
        buf_ptr = original_buffer;
        recv_val = recv(fd, buf_ptr, BUFSIZE, 0x0);
        if (recv_val <= 0)
          return -2;
        size = (unsigned)recv_val;
        printf("EC1\n");
      } else {
        printf("%d: Allocate new page\n", i);
        printf("%d: Old page: ", i);
        print_raw(nbuf_ptr);
        // We can just create a fresh page and use it
        original_buffer = malloc(BUFSIZE + 10);
        if (original_buffer == NULL)
          return -1;
        allocs->allocations[allocation_idx] = original_buffer;
        allocation_idx++;
        printf("%d: Copying %d bytes from nbuf to new buf: ", i, remaining_buffer_size);
        print_raw(nbuf_ptr);
        memcpy(original_buffer, nbuf_ptr, remaining_buffer_size);
        allocs->argv[i] = original_buffer;
        buf_ptr = original_buffer + remaining_buffer_size;
        size = 0;
        do {
          recv_val = recv(fd, buf_ptr + size, BUFSIZE - remaining_buffer_size, 0x0);
          if (recv_val <= 0)
            return -2;
          printf("%d: Got %d bytes, might need more?\n", i, recv_val);
          size += recv_val;
        } while (size < (bs_length + 2) - remaining_buffer_size);
        *(original_buffer + bs_length) = '\0';
        size = (unsigned)recv_val + remaining_buffer_size;
        if (i == allocs->argc - 1 && (recv_val + buf_ptr) - original_buffer > bs_length + 2 /* /r/n */) {
          printf("%d: Use spb\n", i);
          printf("%d: Current buffer contents: ", i);
          print_raw(buf_ptr);
          spare_page_buf = malloc(BUFSIZE + 1);
          if (spare_page_buf == NULL)
            return -1;
          memmove(spare_page_buf, (recv_val + buf_ptr), ((recv_val + buf_ptr) - original_buffer) - (bs_length + 2));
          spare_page->page = spare_page_buf;
          spare_page->size = remaining_buffer_size;
        }
        buf_ptr = original_buffer + bs_length + 2;
        printf("EC2\n");
      }
    } else {
      printf("%d: Do the normal thing\n", i);
      allocs->argv[i] = nbuf_ptr;
      buf_ptr = nbuf_ptr + bs_length + 2;
      *(nbuf_ptr + bs_length) = '\0';
      printf("EC3 %p\n", spare_page->page);
      if (i == allocs->argc - 1 && buf_ptr - original_buffer < size) {
        printf("SP time %d\n", size);
        spare_page_buf = malloc(BUFSIZE + 1);
        if (spare_page_buf == NULL)
          return -1;
        memmove(spare_page_buf, buf_ptr, size - (buf_ptr - original_buffer));
        spare_page->page = spare_page_buf;
        spare_page->size = size - (buf_ptr - original_buffer);
      }
    }
  }
  printf("Done! Got:");
  for (int i = 0; i < allocs->argc; i++) {
    printf(" %s", allocs->argv[i]);
  }
  printf("\n");
  allocs->allocation_count = allocation_idx;
  return 0;
}
#endif

void resp_cmd_free(struct resp_allocations * const allocs) {
  for (int i = 0; i < allocs->allocation_count - 1; i++)
    free(allocs->allocations[i]);
}

void resp_cmd_args(struct resp_allocations * const allocs, int *argc, char ***argv) {
  *argc = allocs->argc;
  *argv = allocs->argv;
}

static const char *process_string_net_err = "process string network error";
static const char *process_string_malloc_err = "process string malloc error";
static const char *process_string_dw_err = "process string dw error";
static const char *process_string_length_err = "process string length error";
static const int max_string_size = 512;
static const char resp_line_terminator = '\r';

static void process_string(struct data_wrap *dw, struct resp_response * const response) {
  size_t size = 0;
  size_t buf_size_incr;
  char *buf = NULL;
  char *nbuf;
  long retval;
  do {
    buf_size_incr = size < max_string_size ? size : max_string_size;
    if (buf_size_incr < 8)
      buf_size_incr = 8;
    nbuf = realloc(buf, sizeof(char) * (size + buf_size_incr));
    if (nbuf == NULL) {
      free(buf);
      response->type = PROCESSING_ERROR;
      response->data.processing_error = process_string_malloc_err;
      return;
    } else {
      buf = nbuf;
    }
    retval = dw_read_copy(dw, buf + size, (long)buf_size_incr, &resp_line_terminator);
    size += buf_size_incr;
  } while (retval == -1);
  dw_eat(dw, 2);
  if (retval <= -2) {
    free(buf);
    response->type = PROCESSING_ERROR;
    response->data.processing_error = process_string_net_err;
    if (retval <= -4)
      response->data.processing_error = process_string_dw_err;
  } else if ((long)(size - buf_size_incr) + retval - 2 < 0) {
    response->type = PROCESSING_ERROR;
    response->data.processing_error = process_string_length_err;
  } else {
    buf[(long)(size - buf_size_incr) + retval] = '\0';
    // Note: If it's an error string, we fix it outside this method
    response->type = SIMPLE_STRING;
    response->data.simple_string = buf;
  }
}

static const char* process_int_net_err = "process int network error";

static int process_int_raw(struct data_wrap *dw, long long *i) {
  *i = 0;
  char *nocopy_ptr;
  bool nocopy_again;
  long recv_len;
  do {
    recv_len = dw_read_nocopy(dw, -1, &resp_line_terminator, &nocopy_ptr, &nocopy_again);
    if (recv_len < 0)
      return (int)recv_len;

    // Funny special case: if dw_read_nocopy hits the end, it doesn't know if
    // there are more bytes of the number that are about to be fetched, so it
    // says to do it again. But, if there are no numbers at the start of the
    // buffer, it'll return a length of 0. Then when we multiply, that'd erase
    // i and mess everything up.
    if (recv_len != 0)
      *i = *i * 10 * recv_len + strtoll(nocopy_ptr, NULL, 10);
  } while (nocopy_again);
  dw_eat(dw, 2);
  return 0;
}

static void process_int(struct data_wrap *dw, struct resp_response * const response) {
  long long i;
  if (process_int_raw(dw, &i) != 0) {
    response->type = PROCESSING_ERROR;
    response->data.processing_error = process_int_net_err;
    return;
  }
  response->type = INTEGER;
  response->data.integer = i;
}

static const char *process_bulk_string_net_err = "error in getting bulk string from network";
static const char *process_bulk_string_malloc_err = "error in allocating memory for bulk string";
static const char *process_bulk_string_length_err = "length of string too negative";

static int process_bulk_string_raw(struct data_wrap *dw, char **str, int maxlen) {
  int err;
  long long i;
  long main_recv;
  long cleanup_recv;
  if ((err = process_int_raw(dw, &i)) != 0)
    return err;
  if (i <= 0)
    return (int)i;

  if (i >= maxlen) {
    *str = malloc(sizeof(char) * (unsigned long)(i + 1));
    if (*str == NULL)
      return -7;
  }

  main_recv = dw_read_copy(dw, *str, (long)i, NULL);
  cleanup_recv = dw_eat(dw, 2);
  if (main_recv < 0 || cleanup_recv < 0) {
    return -2;
  }
  (*str)[i] = '\0';
  return (int)i;
}

static void process_bulk_string(struct data_wrap *dw, struct resp_response * const response) {
  process_int(dw, response);
  char *bs_buf;
  long main_recv;
  long cleanup_recv;
  size_t bs_length;
  if (response->type != PROCESSING_ERROR) {
    // Handle the case of a bulk string length -1 being a NULL
    if (response->data.integer == -1) {
      bs_buf = NULL;
    } else if (response->data.integer < -1) {
      response->type = PROCESSING_ERROR;
      response->data.processing_error = process_bulk_string_length_err;
      return;
    } else {
      bs_length = (size_t)response->data.integer;
      bs_buf = malloc(sizeof(char) * (bs_length + 1));
      if (bs_buf == NULL) {
        response->type = PROCESSING_ERROR;
        response->data.processing_error = process_bulk_string_malloc_err;
        return;
      }
      // The second read is to handle the trailing \r\n
      main_recv = dw_read_copy(dw, bs_buf, (long)bs_length, NULL);
      cleanup_recv = dw_eat(dw, 2);
      if (main_recv <= 0 && cleanup_recv <= 0) {
        response->type = PROCESSING_ERROR;
        response->data.processing_error = process_bulk_string_net_err;
        return;
      }
      bs_buf[bs_length] = '\0';
    }
    response->type = BULK_STRING;
    response->data.bulk_string = bs_buf;
  }
}

static const char *process_array_malloc_err = "error allocating memory for array";
static const char *process_array_length_err = "array length less than -1";

int resp_process_command(struct data_wrap *dw, char ***array, int array_length, int substr_length) {
  long long len;
  char *raw_type;
  bool nocopy_again;
  long rval;
  int retlen;
  bool cts_safe = true;
  raw_type = NULL;
  do {
    rval = dw_read_nocopy(dw, 1, NULL, &raw_type, &nocopy_again);
  } while (nocopy_again);
  if (rval < 0 || raw_type == NULL || *raw_type != '*') {
    return -4;
  }
  int err = process_int_raw(dw, &len);
  char **array_elems;
  if (err != 0)
    return -3;
  if (len > 0) {
    if (len <= array_length) {
      array_elems = *array;
    } else {
      cts_safe = false;
      array_elems = malloc(sizeof(char*) * (unsigned long long)len);
      if (array_elems == NULL)
        return -2;
      *array = array_elems;
    }
    for (int i = 0; i < len; i++) {
      raw_type = NULL;
      do {
        rval = dw_read_nocopy(dw, 1, NULL, &raw_type, &nocopy_again);
      } while (nocopy_again);
      if (rval < 0 || raw_type == NULL || *raw_type != '$') {
        return -4;
      }
      retlen = process_bulk_string_raw(dw, &array_elems[i], cts_safe ? substr_length : 0);
      if (retlen < -1)
        return -5;
    }
  }
  return (int)len;
}

static void process_array(struct data_wrap *dw, struct resp_response * const response) {
  process_int(dw, response);
  long length;
  struct resp_response *ary_elems;
  if (response->type != PROCESSING_ERROR) {
    length = response->data.integer;
    response->data.array.length = length;
    // Handle the case of a bulk string length -1 being a NULL
    if (length == -1) {
      response->data.array.elements = NULL;
    } else if (length < -1) {
      response->type = PROCESSING_ERROR;
      response->data.processing_error = process_array_length_err;
      return;
    } else {
      ary_elems = malloc(sizeof(struct resp_response) * (size_t)length);
      if (ary_elems == NULL) {
        response->type = PROCESSING_ERROR;
        response->data.processing_error = process_array_malloc_err;
        return;
      }
      response->data.array.elements = ary_elems;
      for (int i = 0; i < length; i++) {
        resp_process_packet(dw, &response->data.array.elements[i]);
      }
    }
    response->type = ARRAY;
  }
}

static void print_response_inner(struct resp_response *r, int depth) {
  if (r == NULL) {
    printf("Resp is null\n");
    return;
  }
  switch (r->type) {
    case PROCESSING_ERROR : {
      printf("A processing error occured");
      break;
    }
    case SIMPLE_STRING : {
      printf("\"%s\"", r->data.simple_string);
      break;
    }
    case ERROR_STRING : {
      printf("ERR \"%s\"", r->data.error_string);
      break;
    }
    case INTEGER : {
      printf("%lld", r->data.integer);
      break;
    }
    case BULK_STRING : {
      printf("(BS) \"%s\"", r->data.bulk_string == NULL ? "(null bulk string)" : r->data.bulk_string);
      break;
    }
    case ARRAY : {
      printf("[\n");
      for (int i = 0; i < r->data.array.length; i++) {
        for (int j = 0; j < depth; j++) {
          printf("  ");
        }
        // printf("%d. ", i + 1);
        print_response_inner(&r->data.array.elements[i], depth + 1);
        if (i < r->data.array.length - 1)
          printf(",");
        printf("\n");
      }
      for (int j = 0; j < depth - 1; j++) {
        printf("  ");
      }
      printf("]");
      break;
    }
  }
  if (depth == 1)
    printf("\n");
}

void resp_print(struct resp_response *r) {
  print_response_inner(r, 1);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

long dw_read_nocopy(struct data_wrap *dw, long size, const char *end_char, char **start_ptr, bool *nocopy_again);

int resp_process_packet(struct data_wrap *dw, struct resp_response * const response) {
  char *raw_type = NULL;
  bool nocopy_again;
  long rval;
  do {
    rval = dw_read_nocopy(dw, 1, NULL, &raw_type, &nocopy_again);
  } while (nocopy_again);
  if (rval < 0 || raw_type == NULL) {
    // Connection is closed, can't read type
    return -1;
  }
  switch((enum RESP_TYPE)*raw_type) {
    case SIMPLE_STRING : {
      process_string(dw, response);
      break;
    }
    case ERROR_STRING : {
      process_string(dw, response);
      if (response->type == SIMPLE_STRING)
        response->type = ERROR_STRING;
      break;
    }
    case INTEGER : {
      process_int(dw, response);
      break;
    }
    case BULK_STRING : {
      process_bulk_string(dw, response);
      break;
    }
    case ARRAY : {
      process_array(dw, response);
      break;
    }
    default : {
      printf("Invalid type %c\n", *raw_type);
      return -2;
    }
  }
  return 0;
}

#pragma GCC diagnostic pop

char *resp_bulkstring_array_fetch(struct resp_response *r, unsigned long idx) {
  if (r->type != ARRAY || (long)idx >= r->data.array.length)
    return NULL;
  struct resp_response *resp = &(r->data.array.elements[idx]);
  if (resp->type != BULK_STRING)
    return NULL;
  return resp->data.bulk_string;
}

long resp_array_length(struct resp_response *r) {
  if (r->type != ARRAY)
    return -1;
  return r->data.array.length;
}

enum RESP_TYPE resp_type(struct resp_response *r) {
  return r->type;
}

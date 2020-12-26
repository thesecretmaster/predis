#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sched.h>
#include <fcntl.h>
#include "lib/resp_parser.h"
#include "lib/command_ht.h"
#include "lib/type_ht.h"
#include "commands.h"
#include "predis_ctx.h"
#include "lib/hashtable.h"
#include "lib/1r1w_queue.h"
#include <assert.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"
// Don't care abotu padding right now
struct conn_data {
  struct queue *processing_queue;
  struct queue *sending_queue;
  struct command_ht *command_ht;
  struct type_ht *type_ht;
  struct ht_table *global_ht;
  int fd;
  int nonblock_fd;
};

#pragma GCC diagnostic pop

static int load_structures(struct predis_ctx *ctx, __attribute__((unused)) void **_data, char **argv, __attribute__((unused)) argv_length_t *argv_lengths, int argc) {
  printf("Starting load\n");
  if (argc != 1)
    return WRONG_ARG_COUNT;

  printf("Right args\n");
  char *file_name = argv[0];
  void *dl_handle;
  if ((dl_handle = dlopen(file_name, RTLD_NOW | RTLD_LOCAL)) == NULL) {
    printf("Bad dlopen %s %p %s\n", file_name, dl_handle, dlerror());
    return DLOPEN_FAILED;
  }

  predis_init_func ifunc = (predis_init_func)dlsym(dl_handle, "predis_init");
  struct predis_ctx *sctx = malloc(sizeof(struct predis_ctx));
  sctx->command_ht = ctx->command_ht;
  sctx->type_ht = ctx->type_ht;
  sctx->reply_fd = -1;
  sctx->needs_reply = false;

  ifunc(sctx);

  free(sctx);

  return PREDIS_SUCCESS;
}

// static const char ok_msg[] = "+OK\r\n";

// #define argv_stack_length 10
// #define argv_stack_string_length 50

static void *packet_processor(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct queue *queue = cdata->processing_queue;
  struct resp_allocations *resp_allocs;
  struct resp_spare_page *resp_sp = resp_cmd_init_spare_page();
  int cmd_status;
  long argc;
  char **argv;
  bulkstring_size_t *argv_lengths;
  do {
    resp_allocs = resp_cmd_init();
    cmd_status = resp_cmd_process(cdata->fd, resp_allocs, resp_sp);
    if (cmd_status == -2) {
      printf("Connection %d closed\n", cdata->fd);
      break;
    } else if (cmd_status != 0) {
      printf("Protocol error: %d\n", cmd_status);
      break;
    }
    resp_cmd_args(resp_allocs, &argc, &argv, &argv_lengths);
    if (argc < 1) {
      printf("Command array too short (%ld)\n", argc);
      continue;
    }
    while (queue_push(queue, &resp_allocs) != 0) {}
  } while (true);
  queue_close(queue);
  printf("Recver exiting\n");
  return NULL;
}

static void *runner(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct queue *queue = cdata->processing_queue;
  struct ht_table *table = cdata->global_ht;
  char **argv;
  bulkstring_size_t *argv_lengths;
  char **ptrs;
  bulkstring_size_t *ptrs_lengths;
  long argc_raw = 0;
  unsigned long argc;
  command_func cmd;
  struct format_string *fstring;
  unsigned long other_i;
  bool error_happened;
  bool error_resolved;
  int command_ht_rval;
  struct predis_ctx ctx;
  void **data;
  void *data_stack[10];
  struct resp_allocations *resp_allocs;
  ctx.command_ht = cdata->command_ht;
  ctx.reply_fd = cdata->nonblock_fd;
  ctx.sending_queue = cdata->sending_queue;
  ctx.reply_buf = malloc(sizeof(char) * PREDIS_CTX_CHAR_BUF_SIZE);
  do {
    if (queue_pop(queue, (void*)&resp_allocs) != 0) {
      if (queue_closed(queue)) {
        break;
      } else {
        continue;
      }
    }
    // printf("Qpop\n");
    resp_cmd_args(resp_allocs, &argc_raw, &argv, &argv_lengths);
    argc = (unsigned long)argc_raw;
    ctx.needs_reply = true;
    error_happened = false;
    error_resolved = false;
    if (argv[0] == NULL) {
      printf("Argument error\n");
    } else if ((command_ht_rval = command_ht_fetch(cdata->command_ht, argv[0], (unsigned int)argv_lengths[0], &fstring, &cmd)) == 0) {
      if (fstring->length + 1 != argc) {
        printf("Length wrong! Fstring: %u / Argc: %lu\n", fstring->length, argc);
        error_happened = true;
      } else {
        // printf("Running command %s\n", argv[0]);
        ptrs = argv + 1;
        ptrs_lengths = argv_lengths + 1;
        argc -= 1;
        if (argc < sizeof(data_stack)) {
          data = data_stack;
        } else {
          data = malloc(sizeof(void*) * argc);
        }
        other_i = 0;
        for (unsigned long i = 0; i < argc; i++) {
          other_i = i;
          // printf("Command %s -- Arg[%d] = %c, %s\n", cmd_name, i, preload.format_string[i], ptrs[i]);
          switch(fstring->contents[i].access_type) {
            case FSTRING_MODIFY_CREATE: {
              // printf("Small c in %d %s\n", i, ptrs[i]);
              if (fstring->contents[i].type->init(&data[i]) == 0) {
                switch (ht_store(table, ptrs[i], (unsigned int)ptrs_lengths[i], data[i], fstring->contents[i].type)) {
                  case HT_DUPLICATE_KEY: {
                    // free(data[i]);
                    ht_find(table, ptrs[i], (unsigned int)ptrs_lengths[i], (void**)&data[i], fstring->contents[i].type);
                    break;
                  }
                  case HT_WRONGTYPE: {
                    printf("Wrong type in MODIFY_CREATE op\n");
                    error_happened = true;
                    break;
                  }
                  case HT_OOM:
                  case HT_NOT_FOUND: {
                    printf("Weird error in MODIFY_CREATE\n");
                    error_happened = true;
                    break;
                  }
                  case HT_GOOD: {
                    break;
                  }
                }
              } else {
                printf("Initialization failed for type in MODIFY_CREATE op\n");
                error_happened = true;
              }
              break;
            }
            case FSTRING_READONLY: {
              // printf("Big R in %d %s\n", i, ptrs[i]);
              switch (ht_find(table, ptrs[i], (unsigned int)ptrs_lengths[i], &data[i], fstring->contents[i].type)) {
                case HT_WRONGTYPE: {
                  printf("Wrong type in READONLY op\n");
                  error_happened = true;
                  break;
                }
                case HT_NOT_FOUND: {
                  // printf("Not found in READONLY\n");
                  error_happened = true;
                  break;
                }
                case HT_DUPLICATE_KEY:
                case HT_OOM: {
                  printf("Weird error in READONLY\n");
                  error_happened = true;
                  break;
                }
                case HT_GOOD: {
                  break;
                }
              }
              if (error_happened) {
                error_resolved = true;
                replyBulkString(&ctx, NULL, -1);
              }
              break;
            }
            case FSTRING_STRING: {
              // printf("Small s in %d\n", i);
              data[i] = NULL;
              break;
            }
            case FSTRING_CREATE:
            case FSTRING_MODIFY_NOCREATE: {
              printf("Can't handle this command yet\n");
              error_happened = true;
              data[i] = NULL;
              break;
            }
          }
          if (error_happened)
            break;
        }
        // Fetch data, run preload, schedule, enqueue <- wow so out of date
        if (error_happened) {
          if (!error_resolved) {
            printf("ERR OTHER THINGIE\n");
            replySimpleString(&ctx, "ERR other thingie");
          }
        } else {
          switch(cmd(&ctx, data, ptrs, ptrs_lengths, (int)argc)) {
            case WRONG_ARG_COUNT: {
              replySimpleString(&ctx, "ERR wrong arg count");
              break;
            }
          }
        }
        if (data != data_stack)
          free(data);
      }
    } else {
      printf("Command %.*s not found (1 = notfound, %d)\n", (int)argv_lengths[0], argv[0], command_ht_rval);
    }
    if (ctx.needs_reply)
      replySimpleString(&ctx, "OK");
    resp_cmd_free(resp_allocs);
  } while (argc_raw >= 0);
  queue_close(cdata->sending_queue);
  printf("Runner exiting\n");
  return NULL;
}

static void *sender(void *_obj) {
  struct conn_data *obj = _obj;
  struct queue *q = obj->sending_queue;
  struct pre_send_data data;
  while (!queue_closed(q)) {
    if (queue_pop(q, (void*)&data) == 0) {
      send(obj->fd, data.msg, data.length, MSG_NOSIGNAL);
    }
  }
  printf("Sender exiting\n");
  return NULL;
}

// #include <sys/ioctl.h>
// static void *qchecker(void *_q) {
//   struct conn_data *q = _q;
//   struct timespec sleeplen;
//   sleeplen.tv_sec = 0;
//   sleeplen.tv_nsec = 10000000;
//   int count;
//   while (true) {
//     nanosleep(&sleeplen, NULL);
//     ioctl(q->fd, FIONREAD, &count);
//     if (queue_closed(q->processing_queue))
//       break;
//     printf("Queue size: %u\nData on fd: %d\n", queue_size(q->processing_queue), count);
//   }
//   return NULL;
// }
#include <signal.h>
static void sigint_handler(int i) __attribute__((noreturn));
static void sigint_handler(__attribute__((unused)) int i) {
  printf("Exiting!\n");
  exit(0);
}

static const char load_cmd_name[] = "load";

int main() {
  signal(SIGINT, &sigint_handler);
  printf("Starting server\n");
  struct addrinfo *addrinfo;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  if (getaddrinfo("0.0.0.0", "8000", &hints, &addrinfo) != 0) {
    printf("bad\n");
    return 1;
  }
  // AF_INET/AF_UNIX/AF_INET6 all work
  int socket_fd = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
  if (socket_fd < 0) {
    printf("pad3\n");
    return 3;
  }
  int tru = 1;
  setsockopt(socket_fd, SOL_SOCKET ,SO_REUSEADDR, &tru, sizeof(int));
  if (bind(socket_fd, addrinfo->ai_addr, addrinfo->ai_addrlen) != 0) {
    printf("bad2\n");
    return 2;
  }
  if (listen(socket_fd, 10) != 0) {
    printf("bad4\n");
    return 4;
  }
  int client_sock;
  struct sockaddr_storage their_addr;
  socklen_t addr_size = sizeof(their_addr);
  pthread_t pid;
  struct ht_table *global_ht = ht_init();
  struct type_ht *type_ht = type_ht_init(256);
  struct command_ht *command_ht = command_ht_init(256, type_ht);
  const char load_cmd_fstring[] = "S";
  command_ht_store(command_ht, load_cmd_name, sizeof(load_cmd_name), &load_structures, load_cmd_fstring, sizeof(load_cmd_fstring) - 1);
  struct predis_ctx ctx;
  ctx.command_ht = command_ht;
  ctx.type_ht = type_ht;
  load_structures(&ctx, NULL, &((char*){"types/string.so"}), NULL, 1);
  load_structures(&ctx, NULL, &((char*){"commands/string.so"}), NULL, 1);
  load_structures(&ctx, NULL, &((char*){"commands/config.so"}), NULL, 1);
  // load_structures(&ctx, NULL, &((char*){"commands/hash.so"}), NULL, 1);
  struct conn_data *obj;
  while (1) {
    client_sock = accept(socket_fd, (struct sockaddr *)&their_addr, &addr_size);
    printf("Accepted a conn %d!\n", client_sock);
    obj = malloc(sizeof(struct conn_data));
    obj->fd = client_sock;
    obj->nonblock_fd = client_sock;
    // obj->nonblock_fd = fcntl(client_sock, F_DUPFD);
    // fcntl(obj->nonblock_fd, F_SETFL, O_NONBLOCK);
    obj->global_ht = global_ht;
    obj->processing_queue = queue_init(50, sizeof(struct resp_allocations*));
    obj->sending_queue = queue_init(50, sizeof(struct pre_send_data));
    assert(obj->sending_queue != NULL);
    obj->command_ht = command_ht;
    obj->type_ht = type_ht;
    pthread_create(&pid, NULL, packet_processor, obj);
    pthread_create(&pid, NULL, runner, obj);
    pthread_create(&pid, NULL, sender, obj);
    // pthread_create(&pid, NULL, qchecker, obj);
  }
}

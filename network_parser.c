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
  struct ht_table *global_ht;
  int fd;
  int nonblock_fd;
};

#pragma GCC diagnostic pop

static int load_command(struct predis_ctx *ctx, __attribute__((unused)) struct predis_data **_data, char **argv, __attribute__((unused)) unsigned long *argv_lengths, int argc) {
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
  int argc;
  char **argv;
  unsigned long *argv_lengths;
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
      printf("Command array too short (%d)\n", argc);
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
  unsigned long *argv_lengths;
  char **ptrs;
  unsigned long *ptrs_lengths;
  int argc_raw = 0;
  unsigned long argc;
  command_func cmd;
  union command_preload_strategies preload;
  char *cmd_name;
  bool preload_is_func;
  int rval;
  unsigned long other_i;
  bool error_happened;
  bool error_resolved;
  int ht_find_val;
  struct predis_ctx ctx;
  struct predis_data **data;
  struct predis_data *data_stack[10];
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
    cmd_name = argv[0];
    if (cmd_name == NULL) {
      printf("Argument error\n");
    } else if ((cmd = command_ht_fetch_command(cdata->command_ht, cmd_name)) == NULL || (preload = command_ht_fetch_preload(cdata->command_ht, cmd_name, &preload_is_func)).ptr == NULL) {
      printf("Command %s not found\n", cmd_name);
    } else {
      ptrs = argv + 1;
      ptrs_lengths = argv_lengths + 1;
      argc -= 1;
      if (preload_is_func) {
        // bm = command_bitmap_init((unsigned long)argc);
        printf("Uhhh can't handle a preload func\n");
      } else {
        if (argc < sizeof(data_stack)) {
          data = data_stack;
        } else {
          data = malloc(sizeof(struct predis_data*) * argc);
        }
        other_i = 0;
        for (unsigned long i = 0; i < argc; i++) {
          other_i = i;
          // printf("Command %s -- Arg[%d] = %c, %s\n", cmd_name, i, preload.format_string[i], ptrs[i]);
          if (preload.format_string[i] == '\0') {
            printf("Bad fstring\n");
            replySimpleString(&ctx, "ERR wrong number of args");
            error_happened = true;
            break;
          }
          switch(preload.format_string[i]) {
            case 'c': {
              // printf("Small c in %d %s\n", i, ptrs[i]);
              data[i] = malloc(sizeof(struct predis_data));
              rval = ht_store(table, ptrs[i], (unsigned int)ptrs_lengths[i], data[i]);
              if (rval == HT_DUPLICATE_KEY) {
                // free(data[i]);
                ht_find(table, ptrs[i], (unsigned int)ptrs_lengths[i], &data[i]);
              } else if (rval != HT_GOOD) {
                printf("SmolC\n");
                error_happened = true;
              }
              break;
            }
            case 'R': {
              // printf("Big R in %d %s\n", i, ptrs[i]);
              if ((ht_find_val = ht_find(table, ptrs[i], (unsigned int)ptrs_lengths[i], &data[i])) != HT_GOOD) {
                // printf("dum %d\n", ht_find_val);
                error_happened = true;
                error_resolved = true;
                replyBulkString(&ctx, NULL, -1);
              }
              break;
            }
            case 's': {
              // printf("Small s in %d\n", i);
              data[i] = NULL;
              break;
            }
            default : {
              printf("Invalid format string");
              error_happened = true;
            }
          }
          if (error_happened)
            break;
        }
        // Fetch data, run preload, schedule, enqueue
        if (error_happened) {
          if (!error_resolved) {
            printf("ERR OTHER THINGIE %c\n", preload.format_string[other_i]);
            replySimpleString(&ctx, "ERR other thingie");
          }
        } else {
          cmd(&ctx, data, ptrs, ptrs_lengths, (int)argc);
        }
        if (data != data_stack)
          free(data);
      }
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
#include <signal.h>
static void sigint_handler(int i) __attribute__((noreturn));
static void sigint_handler(__attribute__((unused)) int i) {
  printf("Exiting!\n");
  exit(0);
}

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
  struct command_ht *command_ht = command_ht_init(256);
  command_ht_store(command_ht, "load", &load_command, (const char *)"s", false);
  struct predis_ctx ctx;
  ctx.command_ht = command_ht;
  load_command(&ctx, NULL, &((char*){"commands/string.so"}), NULL, 1);
  load_command(&ctx, NULL, &((char*){"commands/config.so"}), NULL, 1);
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
    pthread_create(&pid, NULL, packet_processor, obj);
    pthread_create(&pid, NULL, runner, obj);
    pthread_create(&pid, NULL, sender, obj);
  }
}

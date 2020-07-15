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
#include "lib/resp_parser.h"
#include "lib/command_ht.h"
#include "commands.h"
#include "predis_ctx.h"
#include "lib/hashtable.h"
#include "lib/1r1w_queue.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"
// Don't care abotu padding right now
struct conn_data {
  struct queue *queue;
  struct command_ht *command_ht;
  struct ht_table *global_ht;
  int fd;
};

#pragma GCC diagnostic pop

static int load_command(struct predis_ctx *ctx, __attribute__((unused)) struct predis_data **_data, char **argv, int argc) {
  printf("Starting load\n");
  if (argc != 1)
    return WRONG_ARG_COUNT;

  printf("Right args\n");
  char *file_name = argv[0];
  void *dl_handle;
  if ((dl_handle = dlopen(file_name, RTLD_NOW | RTLD_LOCAL)) == NULL)
    return DLOPEN_FAILED;

  predis_init_func ifunc = (predis_init_func)dlsym(dl_handle, "predis_init");
  struct predis_ctx *sctx = malloc(sizeof(struct predis_ctx));
  sctx->command_ht = ctx->command_ht;
  sctx->reply_fd = -1;
  sctx->needs_reply = false;

  ifunc(sctx);

  free(sctx);

  return PREDIS_SUCCESS;
}

static const char ok_msg[] = "+OK\r\n";

#define argv_stack_length 10
#define argv_stack_string_length 50

static void *packet_processor(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct data_wrap *dw = dw_init(cdata->fd, 1024);
  struct queue *queue = cdata->queue;
  int argc_raw;
  char **argv;
  do {
    argc_raw = resp_process_command(dw, &argv, 0, 0);
    while (queue_push(queue, argv, argc_raw) != 0) {}
    if (argc_raw == -4) {
      printf("Connection %d closed\n", cdata->fd);
      break;
    } else if (argc_raw < -1) {
      printf("Protocol error: %d\n", argc_raw);
      break;
    } else if (argc_raw < 1) {
      printf("Command array too short (%d)\n", argc_raw);
    }
  } while (true);
  printf("Recver exiting\n");
  return NULL;
}

static void *runner(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct queue *queue = cdata->queue;
  struct ht_table *table = cdata->global_ht;
  char **argv;
  char **ptrs;
  int argc_raw;
  unsigned long argc;
  command_func cmd;
  union command_preload_strategies preload;
  char *cmd_name;
  bool preload_is_func;
  int rval;
  unsigned long other_i;
  bool error_happened;
  struct predis_ctx ctx;
  struct predis_data **data;
  struct predis_data *data_stack[10];
  ctx.command_ht = cdata->command_ht;
  ctx.reply_fd = cdata->fd;
  ctx.reply_buf = malloc(sizeof(char) * PREDIS_CTX_CHAR_BUF_SIZE);
  do {
    while (queue_pop(queue, &argv, &argc_raw) != 0) {}
    ctx.needs_reply = true;
    error_happened = false;
    cmd_name = argv[0];
    if (cmd_name == NULL || argc_raw < 1) {
      printf("Argument error\n");
    } else if ((cmd = command_ht_fetch_command(cdata->command_ht, cmd_name)) == NULL || (preload = command_ht_fetch_preload(cdata->command_ht, cmd_name, &preload_is_func)).ptr == NULL) {
      printf("Command %s not found\n", cmd_name);
    } else {
      argc = (unsigned long)(argc_raw - 1);
      ptrs = argv + 1;
      if (preload_is_func) {
        // bm = command_bitmap_init((unsigned long)argc);
        printf("Uhhh can't handle a preload func\n");
      } else {
        if (argc < sizeof(data_stack)) {
          data = data_stack;
        } else {
          data = malloc(sizeof(struct predis_data*) * argc);
        }
        for (unsigned long i = 0; i < argc; i++) {
          other_i = i;
          if (preload.format_string[i] == '\0') {
            replySimpleString(&ctx, "ERR wrong number of args");
            error_happened = true;
            break;
          }
          switch(preload.format_string[i]) {
             case 'c':
                data[i] = malloc(sizeof(struct predis_data));
                rval = ht_store(table, ptrs[i], data[i]);
                if (rval == HT_DUPLICATE_KEY) {
                  free(data[i]);
                  ht_find(table, ptrs[i], &data[i]);
                } else if (rval != HT_GOOD) {
                  error_happened = true;
                }
                break;
             case 'R':
                if (ht_find(table, ptrs[i], &data[i]) != HT_GOOD)
                  error_happened = true;
                break;
             case 's':
                data[i] = NULL;
                break;
             default :
                printf("Invalid format string");
                error_happened = true;
          }
          if (error_happened)
            break;
        }
        // Fetch data, run preload, schedule, enqueue
        if (error_happened) {
          printf("ERR OTHER THINGIE %c\n", preload.format_string[other_i]);
          replySimpleString(&ctx, "ERR other thingie");
        } else {
          cmd(&ctx, data, ptrs, (int)argc);
        }
        if (data != data_stack)
          free(data);
      }
    }
    if (ctx.needs_reply)
      send(cdata->fd, ok_msg, sizeof(ok_msg) - 1, MSG_NOSIGNAL);
  } while (argc_raw >= 0);
  printf("Runner exiting\n");
  return NULL;
}

static void *connhandler(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct data_wrap *dw = dw_init(cdata->fd, 1024);
  // struct data_wrap *dw = dw_init(cdata->fd, 14);
  struct ht_table *table = cdata->global_ht;
  int argc_raw;
  command_func cmd;
  union command_preload_strategies preload;
  char *cmd_name;
  bool preload_is_func;
  unsigned long argc;
  int rval;
  bool error_happened;
  char **ptrs;
  char *ptrs_stack[10];
  struct predis_data **data;
  struct predis_data *data_stack[10];
  char **argv;
  char *argv_stack[argv_stack_length];
  char *argv_stack_template[argv_stack_length];
  for (int i = 0; i < argv_stack_length; i++) {
    argv_stack_template[i] = malloc(sizeof(char) * argv_stack_string_length);
    if (argv_stack_template[i] == NULL) {
      printf("Ugh malloc you dumb butt\n");
      return NULL;
    }
  }
  struct predis_ctx ctx;
  ctx.command_ht = cdata->command_ht;
  ctx.reply_fd = cdata->fd;
  ctx.reply_buf = malloc(sizeof(char) * PREDIS_CTX_CHAR_BUF_SIZE);
  do {
    error_happened = false;
    argv = argv_stack;
    for (unsigned long i = 0; i < argv_stack_length; i++) {
      argv_stack[i] = argv_stack_template[i];
    }
    argc_raw = resp_process_command(dw, &argv, sizeof(argv_stack), sizeof(argv_stack_template) / argv_stack_length);
    if (argc_raw == -4) {
      printf("Connection %d closed\n", cdata->fd);
      break;
    } else if (argc_raw < -1) {
      printf("Protocol error: %d\n", argc_raw);
      break;
    } else if (argc_raw < 1) {
      printf("Command array too short (%d)\n", argc_raw);
      break;
    }
    ctx.needs_reply = true;
    cmd_name = argv[0];
    if (cmd_name != NULL && argc_raw >= 1 && (cmd = command_ht_fetch_command(cdata->command_ht, cmd_name)) != NULL && (preload = command_ht_fetch_preload(cdata->command_ht, cmd_name, &preload_is_func)).ptr != NULL) {
      argc = (unsigned long)(argc_raw - 1);
      if (argc < sizeof(ptrs_stack)) {
        ptrs = ptrs_stack;
      } else {
        ptrs = malloc(sizeof(char*) * argc);
      }
      for (unsigned long i = 0; i < argc; i++) {
        ptrs[i] = argv[i + 1];
      }
      if (preload_is_func) {
        // bm = command_bitmap_init((unsigned long)argc);
        printf("Uhhh can't handle a preload func\n");
      } else {
        if (argc < sizeof(data_stack)) {
          data = data_stack;
        } else {
          data = malloc(sizeof(struct predis_data*) * argc);
        }
        for (unsigned long i = 0; i < argc; i++) {
          if (preload.format_string[i] == '\0') {
            replySimpleString(&ctx, "ERR wrong number of args");
            error_happened = true;
            break;
          }
          switch(preload.format_string[i]) {
             case 'c':
                data[i] = malloc(sizeof(struct predis_data));
                rval = ht_store(table, ptrs[i], data[i]);
                if (rval == HT_DUPLICATE_KEY) {
                  free(data[i]);
                  ht_find(table, ptrs[i], &data[i]);
                } else if (rval != HT_GOOD) {
                  error_happened = true;
                }
                break;
             case 'R':
                if (ht_find(table, ptrs[i], &data[i]) != HT_GOOD)
                  error_happened = true;
                break;
             case 's':
                data[i] = NULL;
                break;
             default :
                printf("Invalid format string");
                error_happened = true;
          }
          if (error_happened)
            break;
        }
        // Fetch data, run preload, schedule, enqueue
        if (!error_happened) {
          cmd(&ctx, data, ptrs, (int)argc);
        } else {
          replySimpleString(&ctx, "ERR other thingie");
        }
        if (data != data_stack)
          free(data);
      }
      if (ptrs != ptrs_stack)
        free(ptrs);
    } else {
      printf("A bad thing #2 :(\n");
    }
    if (argv != argv_stack) {
      free(argv);
      argv = argv_stack;
    }
    if (ctx.needs_reply)
      send(cdata->fd, ok_msg, sizeof(ok_msg) - 1, MSG_NOSIGNAL);
  } while (argc_raw >= -1);
  close(cdata->fd);
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
  if (getaddrinfo("0.0.0.0", "8080", &hints, &addrinfo) != 0) {
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
  load_command(&ctx, NULL, &((char*){"commands/string.so"}), 1);
  while (1) {
    client_sock = accept(socket_fd, (struct sockaddr *)&their_addr, &addr_size);
    printf("Accepted a conn %d!\n", client_sock);
    struct conn_data *obj = malloc(sizeof(struct conn_data));
    obj->fd = client_sock;
    obj->global_ht = global_ht;
    obj->queue = queue_init(10);
    obj->command_ht = command_ht;
    pthread_create(&pid, NULL, packet_processor, obj);
    pthread_create(&pid, NULL, runner, obj);
  }
}

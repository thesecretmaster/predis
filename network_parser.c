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
#include "lib/resp_parser.h"
#include "lib/command_ht.h"
#include "lib/command_bitmap_init.c"
#include "commands.h"
#include "predis_ctx.h"
#include "lib/hashtable.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"
// Don't care abotu padding right now
struct conn_data {
  int fd;
  struct command_ht *command_ht;
  struct ht_table *global_ht;
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

static void *connhandler(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct data_wrap *dw = dw_init(cdata->fd, 1024);
  // struct data_wrap *dw = dw_init(cdata->fd, 14);
  struct resp_response *resp = resp_alloc();
  struct ht_table *table = cdata->global_ht;
  int pack_err;
  char error_buf[512];
  const char *error_text;
  int error_buf_len;
  command_func cmd;
  union command_preload_strategies preload;
  char *cmd_name;
  command_bitmap bm;
  bool preload_is_func;
  int argc;
  int rval;
  bool error_happened;
  char **ptrs;
  struct predis_data **data;
  struct predis_ctx *ctx = malloc(sizeof(struct predis_ctx));
  do {
    error_happened = false;
    pack_err = resp_process_packet(dw, resp);
    ctx->needs_reply = true;
    if (pack_err == -1) {
      printf("Connection %d closed\n", cdata->fd);
      break;
    } else if (pack_err == -2) {
      printf("Protocol error: %d\n", pack_err);
      break;
    }
    error_text = resp_error(resp);
    if (error_text != NULL) {
      error_buf_len = snprintf(error_buf, sizeof(error_buf), "-%s", error_text);
      if (error_buf_len > 0) {
        send(cdata->fd, error_buf, (size_t)(error_buf_len), MSG_NOSIGNAL);
      } else {
        printf("Failed to send error\n");
      }
    } else if (resp_type(resp) != ARRAY) {
      printf("uh wrong type u fool\n");
    } else {
      cmd_name = resp_bulkstring_array_fetch(resp, 0);
      argc = (int)(resp_array_length(resp) - 1);
      ptrs = malloc(sizeof(char*) * argc);
      for (int i = 0; i < argc; i++) {
        ptrs[i] = resp_bulkstring_array_fetch(resp, i + 1);
      }
      if (cmd_name != NULL && argc >= 0 && (cmd = command_ht_fetch_command(cdata->command_ht, cmd_name)) != NULL && (preload = command_ht_fetch_preload(cdata->command_ht, cmd_name, &preload_is_func)).ptr != NULL) {
        if (preload_is_func) {
          // bm = command_bitmap_init((unsigned long)argc);
          printf("Uhhh can't handle a preload func\n");
        } else {
          data = malloc(sizeof(struct predis_data) * argc);
          ctx->command_ht = cdata->command_ht;
          ctx->reply_fd = cdata->fd;
          for (int i = 0; i < argc; i++) {
            if (preload.format_string[i] == '\0') {
              replySimpleString(ctx, "ERR wrong number of args");
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
            cmd(ctx, data, ptrs, argc);
          } else {
            replySimpleString(ctx, "ERR other thingie");
          }
          free(data);
        }
      } else {
        resp_print(resp);
      }
      if (ctx->needs_reply)
        send(cdata->fd, ok_msg, sizeof(ok_msg) - 1, MSG_NOSIGNAL);
      free(ptrs);
    }
  } while (error_text == NULL);
  close(cdata->fd);
  return NULL;
}
#include <signal.h>
static void sigint_handler(int i) {
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
    obj->command_ht = command_ht;
    pthread_create(&pid, NULL, connhandler, obj);
  }
}

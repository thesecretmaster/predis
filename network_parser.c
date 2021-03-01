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
#include <stdint.h>
#include "lib/resp_parser.h"
#include "lib/command_ht.h"
#include "lib/type_ht.h"
#include "public/commands.h"
#include "send_queue.h"
#include "predis_ctx.h"
#include "lib/hashtable.h"
#include "lib/1r1w_queue.h"
#include "lib/gc.h"
#include "lib/send_queue.h"
#include <assert.h>
#include <sys/epoll.h>

struct conn_data {
  struct queue *processing_queue;
  struct queue *sending_queue;
  struct command_ht *command_ht;
  struct type_ht *type_ht;
  struct ht_table *global_ht;
  struct resp_conn_data *resp_cdata;
  int fd;
  int epoll_fd;
};

static int load_structures(struct predis_ctx *ctx, __attribute__((unused)) struct predis_arg *_data, char **argv, __attribute__((unused)) argv_length_t *argv_lengths, int argc) {
  printf("Starting load\n");
  if (argc != 1)
    return WRONG_ARG_COUNT;

  printf("Right args\n");
  char *file_name = argv[0];
  void *dl_handle;
  if ((dl_handle = dlopen(file_name, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE)) == NULL) {
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
  dlclose(dl_handle);

  return PREDIS_SUCCESS;
}

// static const char ok_msg[] = "+OK\r\n";

// #define argv_stack_length 10
// #define argv_stack_string_length 50

static int packet_reciever(int epoll_fd, struct resp_allocations **resp_allocs, struct resp_conn_data **cdata, void **proc_q, int *par_fd) {
  int cmd_status;
  long argc;
  char **argv;
  bulkstring_size_t *argv_lengths;
  *resp_allocs = resp_cmd_init(0x0);
  cmd_status = resp_cmd_process(epoll_fd, *resp_allocs, cdata, proc_q, par_fd);
  if (cmd_status == -2 || cmd_status == -1) {
    printf("Connection %d other end closed\n", *par_fd);
    return -1;
  } else if (cmd_status != 0) {
    printf("Protocol error: %d\n", cmd_status);
    return -2;
  }
  resp_cmd_args(*resp_allocs, &argc, &argv, &argv_lengths);
  if (argc < 1) {
    printf("Command array too short (%ld)\n", argc);
    return 1;
  }
  return 0;
}

struct packet_reciever_data {
  int epoll_fd;
};

 __attribute__ ((unused))
static inline void *packet_reciever_queue(void *_pr_data) {
  struct packet_reciever_data *pr_data = _pr_data;
  struct resp_allocations *resp_allocs;
  int ret_fd;
  struct queue *proc_q;
  struct resp_conn_data *cdata;
  int rval;
  do {
    rval = packet_reciever(pr_data->epoll_fd, &resp_allocs, &cdata, (void**)&proc_q, &ret_fd);
    if (rval < 0) {
      queue_close(proc_q);
    } else if (rval > 0) {
      continue;
    } else {
      resp_conn_data_prime(cdata, pr_data->epoll_fd);
      while (queue_push(proc_q, &resp_allocs) != 0) {}
    }
  } while (true);
  return NULL;
}

#include "predis_arg_impl.c"

static void runner(struct predis_ctx *ctx, struct resp_allocations *resp_allocs, struct command_ht *command_ht, struct ht_table *table, struct gc_user *gcg) {
  char **argv;
  bulkstring_size_t *argv_lengths;
  char **ptrs;
  bulkstring_size_t *ptrs_lengths;
  long argc_raw = 0;
  unsigned long argc;
  union command_ht_command_funcs cmd;
  struct format_string *fstring;
  bool error_happened;
  bool error_resolved;
  int command_ht_rval;
  struct predis_arg *data;
  struct predis_arg data_stack[10];
  bool command_is_meta;
  long fstring_index;
  struct predis_typed_data *typed_data;
  struct gc_working_set *working_set;
  void *gc_value;
  resp_cmd_args(resp_allocs, &argc_raw, &argv, &argv_lengths);
  argc = (unsigned long)argc_raw;
  ctx->needs_reply = true;
  error_happened = false;
  error_resolved = false;
  if (argv[0] == NULL) {
    printf("Argument error\n");
  } else if ((command_ht_rval = command_ht_fetch(command_ht, argv[0], (unsigned int)argv_lengths[0], &fstring, &cmd, &command_is_meta)) == 0) {
    if (command_is_meta) {
      cmd.meta(ctx, table, argv + 1, argv_lengths + 1, ((int)argc) - 1, gcg);
    } else {
      if (fstring->length + fstring->optional_argument_count + 1 > argc) {
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
          data = malloc(sizeof(struct predis_arg) * argc);
        }
        fstring_index = 0;
        gc_lock(gcg);
        working_set = malloc(sizeof(struct gc_working_set) + sizeof(void*)*argc*2);
        working_set->length = argc*2;
        memset(&working_set->members, (uintptr_t)NULL, sizeof(void*)*working_set->length);
        for (unsigned long i = 0; i < argc; i++) {
          data[i].ht_value = NULL;
          data[i].data = NULL;
          // printf("Pargins arg[%lu] = fstring[%ld], %s\n", i, fstring_index, ptrs[i]);
          switch(i > fstring->length ? FSTRING_STRING : fstring->contents[fstring_index].access_type) {
            case FSTRING_MODIFY_CREATE: {
              // printf("Small c in %d %s\n", i, ptrs[i]);
              switch (ht_store(table, ptrs[i], (unsigned int)ptrs_lengths[i], &(data[i].ht_value), &gc_value)) {
                case HT_GOOD: {
                  typed_data = malloc(sizeof(struct predis_typed_data));
                  working_set->members[i*2] = typed_data;
                  working_set->members[i*2 + 1] = gc_value;
                  typed_data->type = fstring->contents[fstring_index].details.type;
                  typed_data->data = NULL;
                  data[i].data = typed_data;
                  data[i].needs_commit = true;
                  data[i].needs_initialization = true;
                  break;
                }
                case HT_DUPLICATE_KEY: {
                  // HANDLE WRONGTYPE HERE
                  data[i].data = *(struct predis_typed_data**)data[i].ht_value;
                  if (data[i].data->type != fstring->contents[fstring_index].details.type) {
                    replyError(ctx, "ERR WRONGTYPE");
                    error_happened = true;
                    error_resolved = true;
                    break;
                  }
                  working_set->members[i*2] = data[i].data;
                  working_set->members[i*2 + 1] = gc_value;
                  data[i].needs_initialization = false;
                  data[i].needs_commit = false;
                  // The duplicate key case is the same as the good case
                  // because in both we're just making sure there's a reseved
                  // spot for the modify or create to happen later.
                  break;
                }
                case HT_BADARGS:
                case HT_OOM:
                case HT_NOT_FOUND: {
                  printf("Weird error in MODIFY_CREATE\n");
                  error_happened = true;
                  break;
                }
              }
              break;
            }
            case FSTRING_READONLY: {
              // printf("Big R in %d %s\n", i, ptrs[i]);
              data[i].needs_initialization = false;
              switch (ht_find(table, ptrs[i], (unsigned int)ptrs_lengths[i], &(data[i].ht_value), &gc_value)) {
                case HT_NOT_FOUND: {
                  // printf("Not found in READONLY\n");
                  error_happened = true;
                  break;
                }
                case HT_BADARGS:
                case HT_DUPLICATE_KEY:
                case HT_OOM: {
                  printf("Weird error in READONLY\n");
                  error_happened = true;
                  break;
                }
                case HT_GOOD: {
                  data[i].data = *(struct predis_typed_data**)data[i].ht_value;
                  if (data[i].data->type != fstring->contents[fstring_index].details.type) {
                    replyError(ctx, "ERR WRONGTYPE");
                    error_happened = true;
                    error_resolved = true;
                    break;
                  }
                  working_set->members[i*2] = data[i].data;
                  working_set->members[i*2 + 1] = gc_value;
                  break;
                }
              }
              if (error_happened) {
                error_resolved = true;
                replyBulkString(ctx, NULL, -1);
              }
              break;
            }
            case FSTRING_READONLY_OPTIONAL: {
              data[i].needs_initialization = false;
              switch (ht_find(table, ptrs[i], (unsigned int)ptrs_lengths[i], &(data[i].ht_value), &gc_value)) {
                case HT_NOT_FOUND: {
                  data[i].data = NULL;
                  data[i].ht_value = NULL;
                  break;
                }
                case HT_BADARGS:
                case HT_DUPLICATE_KEY:
                case HT_OOM: {
                  printf("Weird error in READONLY\n");
                  error_happened = true;
                  break;
                }
                case HT_GOOD: {
                  if (data[i].data->type != fstring->contents[fstring_index].details.type) {
                    printf("Wrong type in READONLY op\n");
                    error_happened = true;
                    break;
                  }
                  working_set->members[i*2] = data[i].data;
                  working_set->members[i*2 + 1] = gc_value;
                  break;
                }
              }
              if (error_happened) {
                error_resolved = true;
                replyBulkString(ctx, NULL, -1);
              }
              break;
            }
            case FSTRING_STRING: {
              data[i].data = NULL;
              data[i].ht_value = NULL;
              break;
            }
            case FSTRING_JUMP : {
              // printf("ORIG idx: %lu fstring %ld (fstring len %d) (argc %lu)\n", i, fstring_index, fstring->length, argc);
              // printf("Jump time! %d args remaining, %d slots left in fstring\n", argc - i, fstring->length - fstring_index);
              if ((long)(argc - i) > fstring->length - 1 /* subtract one because fstring->length includes the 1 jump node */ - fstring_index) {
                // printf("Following jump %u\n", fstring->contents[fstring_index].details.jump_target);
                fstring_index = (long)fstring->contents[fstring_index].details.jump_target - 1;
              }
              // printf("Dropping i by one so we'll revisit this arg\n");
              i -= 1;
              // printf("FINL idx: %lu fstring %ld\n", i, fstring_index);
              break;
            }
            case FSTRING_CREATE:
            case FSTRING_MODIFY_NOCREATE: {
              printf("Can't handle this command type yet\n");
              error_happened = true;
              data[i].data = NULL;
              data[i].ht_value = NULL;
              break;
            }
          }
          if (error_happened)
            break;
          fstring_index += 1;
        }
        gc_commit(gcg, working_set);
        // Fetch data, run preload, schedule, enqueue <- wow so out of date
        if (!error_happened) {
          switch(cmd.normal(ctx, data, ptrs, ptrs_lengths, (int)argc)) {
            case WRONG_ARG_COUNT: {
              replySimpleString(ctx, "ERR wrong arg count");
              break;
            }
          }
        }
        gc_clear(gcg);
        if (data != data_stack)
          free(data);
      }
      if (error_happened) {
        if (!error_resolved) {
          printf("ERR OTHER THINGIE\n");
          replySimpleString(ctx, "ERR other thingie");
        }
      }
    }
  } else {
    printf("Command %.*s not found (1 = notfound, %d)\n", (int)argv_lengths[0], argv[0], command_ht_rval);
    replyError(ctx, "ERR unknown command");
  }
  if (ctx->needs_reply)
    replySimpleString(ctx, "OK");
  resp_cmd_free(resp_allocs);
}

__attribute__ ((unused))
static inline void *runner_queue(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct queue *queue = cdata->processing_queue;
  struct resp_allocations *resp_allocs;
  struct predis_ctx ctx;
  struct gc_user *gc = gc_register_user();
  ctx.command_ht = cdata->command_ht;
  ctx.reply_fd = cdata->fd;
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
    runner(&ctx, resp_allocs, cdata->command_ht, cdata->global_ht, gc);
  } while (!queue_closed(queue));
  queue_close(cdata->sending_queue);
  printf("Runner exiting\n");
  return NULL;
}

static void send_pre_data(int fd, struct pre_send *pre_send) {
  unsigned long ss_len;
  int len;
  char *buf;
  char nil_bs[] = "$-1\r\n";
  switch (pre_send->type) {
    case PRE_SEND_SS : {
      ss_len = strlen(pre_send->data.ss);
      buf = malloc(sizeof(char) * (1 + ss_len + 2));
      buf[0] = '+';
      buf[1 + ss_len] = '\r';
      buf[1 + ss_len + 1] = '\n';
      memcpy(buf + 1, pre_send->data.ss, ss_len);
      len = 1 + (int)ss_len + 2;
      break;
    }
    case PRE_SEND_ERR : {
      ss_len = strlen(pre_send->data.err);
      buf = malloc(sizeof(char) * (1 + ss_len + 2));
      buf[0] = '-';
      buf[1 + ss_len] = '\r';
      buf[1 + ss_len + 1] = '\n';
      memcpy(buf + 1, pre_send->data.err, ss_len);
      len = 1 + (int)ss_len + 2;
      break;
    }
    case PRE_SEND_NUM : {
      len = snprintf( NULL, 0, ":%ld\r\n", pre_send->data.num);
      buf = malloc((unsigned long)len);
      snprintf(buf, (unsigned long)len, ":%ld\r\n", pre_send->data.num);
      break;
    }
    case PRE_SEND_BS : {
      if (pre_send->data.bs.length == -1 && pre_send->data.bs.contents == NULL) {
        len = sizeof(nil_bs) - 1;
        buf = nil_bs;
      } else {
        len = snprintf(NULL, 0, "$%ld\r\n%.*s\r\n", pre_send->data.bs.length, (int)pre_send->data.bs.length, pre_send->data.bs.contents);
        buf = malloc((unsigned long)len);
        snprintf(buf, (unsigned long)len, "$%ld\r\n%.*s\r\n", pre_send->data.bs.length, (int)pre_send->data.bs.length, pre_send->data.bs.contents);
      }
      break;
    }
    case PRE_SEND_ARY : {
      len = snprintf( NULL, 0, "*%ld\r\n", pre_send->data.array.length);
      buf = malloc((unsigned long)len);
      snprintf(buf, (unsigned long)len, "*%ld\r\n", pre_send->data.array.length);
      send(fd, buf, (size_t)len, MSG_NOSIGNAL);
      for (unsigned i = 0; i < pre_send->data.array.length; i++) {
        send_pre_data(fd, &(pre_send->data.array.contents[i]));
      }
      return;
    }
  }
  send(fd, buf, (size_t)len, MSG_NOSIGNAL);
  if (buf != nil_bs)
    free(buf);
}

__attribute__ ((unused))
static inline void *sender(void *_obj) {
  struct conn_data *obj = _obj;
  struct queue *q = obj->sending_queue;
  struct pre_send pre_send;
  while (!queue_closed(q)) {
    if (queue_pop(q, &pre_send) == 0) {
      send_pre_data(obj->fd, &pre_send);
    }
  }
  queue_free(obj->sending_queue);
  queue_free(obj->processing_queue);
  printf("Sender exiting\n");
  return NULL;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct onestep_data {
  struct command_ht *command_ht;
  struct ht_table *global_ht;
  int epoll_fd;
};

#pragma GCC diagnostic pop

static void *onestep_thread(void *_onestep_data) {
  struct onestep_data *os_data = _onestep_data;
  int fd;
  struct queue *sending_queue = queue_init(50, sizeof(struct pre_send));
  struct send_queue *sq;
  int sq_ptr;
  struct pre_send sq_data;
  int sq_rval;

  struct resp_conn_data *rcdata;
  struct resp_allocations *resp_allocs;
  int rval;

  struct gc_user *gc = gc_register_user();
  struct predis_ctx ctx;
  ctx.command_ht = os_data->command_ht;
  ctx.sending_queue = sending_queue;
  ctx.reply_buf = malloc(sizeof(char) * PREDIS_CTX_CHAR_BUF_SIZE);

  do {
    rval = packet_reciever(os_data->epoll_fd, &resp_allocs, &rcdata, (void**)&sq, &fd);
    if (rval < 0) {
      if (rval == -1) {
        shutdown(fd, 0);
        close(fd);
      }
      continue;
    } else if (rval > 0) {
      continue;
    }

    do {
      sq_ptr = send_queue_register(sq);
    } while (sq_ptr < 0);
    resp_conn_data_prime(rcdata, os_data->epoll_fd);

    ctx.reply_fd = fd;
    ctx.send_queue = sq;
    ctx.send_queue_ptr = (unsigned int)sq_ptr;

    runner(&ctx, resp_allocs, os_data->command_ht, os_data->global_ht, gc);

    do {
      sq_rval = send_queue_pop_start(sq, (void*)&sq_data);
    } while (sq_rval == -3);
    if (sq_rval == 0) {
      send_pre_data(fd, &sq_data);
      while (send_queue_pop_continue(sq, (void*)&sq_data) == 0) {
        send_pre_data(fd, &sq_data);
      }
    }

    // while (queue_pop(sending_queue, &pre_send) == 0) {
    //   send_pre_data(fd, &pre_send);
    // }
  } while (true);
}

struct gc_data {
  volatile bool stop;
};

static void *gc_thread(void *_gc_data) {
  struct gc_data *data = _gc_data;
  printf("Launched GC\n");
  while (!data->stop) {
    sched_yield();
    gc_run();
  }
  free(data);
  printf("Exited GC!\n");
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

static struct command_ht *global_command_ht = NULL;
static struct type_ht *global_type_ht = NULL;
static struct ht_table *global_ht = NULL;
static struct gc_data *gc_data = NULL;
static pthread_t gc_pid;

#include <signal.h>
static void sigint_handler(int i) __attribute__((noreturn));
static void sigint_handler(__attribute__((unused)) int i) {
  if (global_command_ht != NULL)
    command_ht_free(global_command_ht);
  if (global_type_ht != NULL)
    type_ht_free(global_type_ht);
  if (global_ht != NULL)
    ht_free(global_ht, NULL);
  if (gc_data != NULL) {
    free(gc_data);
  }
  gc_cleanup();
  exit(0);
}

static const char load_cmd_name[] = "load";

static void free_typed_data(void *_data) {
  struct predis_typed_data *data = _data;
  data->type->free(data->data);
}

static int command_del(__attribute__((unused)) struct predis_ctx *ctx, void *_global_ht_table, char **argv, argv_length_t *argv_lengths, int argc, struct gc_user *gc_usr) {
  struct ht_table *table = _global_ht_table;
  gc_lock(gc_usr);
  for (int i = 0; i < argc; i++) {
    if (argv_lengths[i] > 0) {
      void *cts;
      ht_del(table, argv[i], (unsigned int)argv_lengths[i], &cts);
      gc_free(cts, free_typed_data);
    }
  }
  gc_commit(gc_usr, NULL);
  return 0;
}
#include <getopt.h>
#include <sys/sysinfo.h>
int main(int argc, char *argv[]) {
  signal(SIGINT, &sigint_handler);

  int thread_count = (int)get_nprocs();
  unsigned int port = 8000;

  const struct option prog_opts[] = {
    {"port", optional_argument, NULL, 'p'},
    {"threads", optional_argument, NULL, 't'},
  };
  int c;
  while (1) {
    c = getopt_long(argc, argv, "p:t:", prog_opts, NULL);
    if (c == -1) { break; }
    switch (c) {
      case 't':
        if (optarg != NULL) {
          thread_count = (int)strtol(optarg, NULL, 10);
        } else {
          printf("-t requires an argument\n");
        }
        break;
      case 'p':
        if (optarg != NULL) {
          port = (unsigned int)strtol(optarg, NULL, 10);
        } else {
          printf("-p requires an argument\n");
        }
        break;
    }
  }

  printf("Starting server on port %u with %u threads\n", port, thread_count);
  struct addrinfo *addrinfo;
  struct addrinfo hints;
  int port_string_length = snprintf(NULL, 0, "%u", port);
  char port_string[port_string_length + 1];
  snprintf(port_string, (unsigned long)port_string_length + 1, "%u", port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  if (getaddrinfo("0.0.0.0", port_string, &hints, &addrinfo) != 0) {
    printf("bad\n");
    return 1;
  }
  // AF_INET/AF_UNIX/AF_INET6 all work
  int socket_fd = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
  if (socket_fd < 0) {
    freeaddrinfo(addrinfo);
    printf("pad3\n");
    return 3;
  }
  int tru = 1;
  setsockopt(socket_fd, SOL_SOCKET ,SO_REUSEADDR, &tru, sizeof(int));
  if (bind(socket_fd, addrinfo->ai_addr, addrinfo->ai_addrlen) != 0) {
    printf("bad2\n");
    freeaddrinfo(addrinfo);
    return 2;
  }
  freeaddrinfo(addrinfo);
  if (listen(socket_fd, 48) != 0) {
    printf("bad4\n");
    return 4;
  }
  int client_sock;
  struct sockaddr_storage their_addr;
  socklen_t addr_size = sizeof(their_addr);
  pthread_t pid;
  gc_initialize();
  gc_data = malloc(sizeof(struct gc_data));
  gc_data->stop = false;
  pthread_create(&gc_pid, NULL, gc_thread, gc_data);
  global_ht = ht_init(true);
  global_type_ht = type_ht_init(256);
  global_command_ht = command_ht_init(256, global_type_ht);
  const char load_cmd_fstring[] = "S";
  command_ht_store(global_command_ht, load_cmd_name, sizeof(load_cmd_name), &load_structures, load_cmd_fstring, sizeof(load_cmd_fstring) - 1);
  struct predis_ctx ctx;
  command_ht_store_meta(global_command_ht, "del", sizeof("del") - 1, &command_del);
  ctx.command_ht = global_command_ht;
  ctx.type_ht = global_type_ht;
  load_structures(&ctx, NULL, &((char*){"types/string.so"}), NULL, 1);
  load_structures(&ctx, NULL, &((char*){"types/hash.so"}), NULL, 1);
  load_structures(&ctx, NULL, &((char*){"commands/string.so"}), NULL, 1);
  load_structures(&ctx, NULL, &((char*){"commands/config.so"}), NULL, 1);
  load_structures(&ctx, NULL, &((char*){"commands/hash.so"}), NULL, 1);
  struct conn_data *obj;
  int epoll_fd = epoll_create(1024); // Just a hint; redis uses this so I guess we will
  struct epoll_event ev;
  // struct packet_reciever_data *pr_data = malloc(sizeof(struct packet_reciever_data));
  // pr_data->epoll_fd = epoll_fd;
  // for (int i = 0; i < 4; i++) {
  //   pthread_create(&pid, NULL, packet_reciever_queue, pr_data);
  // }
  struct onestep_data *os_data = malloc(sizeof(struct onestep_data));
  os_data->global_ht = global_ht;
  os_data->command_ht = global_command_ht;
  os_data->epoll_fd = epoll_fd;
  for (int i = 0; i < thread_count; i++) {
    pthread_create(&pid, NULL, onestep_thread, os_data);
  }
  while (1) {
    client_sock = accept(socket_fd, (struct sockaddr *)&their_addr, &addr_size);
    printf("Accepted a conn %d!\n", client_sock);
    obj = malloc(sizeof(struct conn_data));
    obj->fd = client_sock;
    obj->epoll_fd = epoll_fd;
    // obj->nonblock_fd = fcntl(client_sock, F_DUPFD);
    // fcntl(obj->nonblock_fd, F_SETFL, O_NONBLOCK);
    obj->global_ht = global_ht;
    obj->processing_queue = queue_init(50, sizeof(struct resp_allocations*));
    obj->sending_queue = queue_init(50, sizeof(struct pre_send));
    assert(obj->sending_queue != NULL);
    obj->command_ht = global_command_ht;
    obj->type_ht = global_type_ht;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = resp_conn_data_init(client_sock, send_queue_init(10, sizeof(struct pre_send)));
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev);
    // pthread_create(&pid, NULL, runner_queue, obj);
    // pthread_create(&pid, NULL, sender, obj);
    // // pthread_create(&pid, NULL, qchecker, obj);
  }
}

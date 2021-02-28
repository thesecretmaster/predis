#include <getopt.h>
#include <sys/sysinfo.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "lib/resp_parser.c"

/* msleep(): Sleep for the requested number of milliseconds. */
int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

struct ep_data {
  int epoll_fd;
};

void *onestep_thread(void *_data) {
  struct ep_data *data = _data;
  char buff[256];
  char send_str[] = "+OK\r\n";
  struct epoll_event event;
  int fd;
  struct timespec ts;
  void *f;
  struct resp_conn_data *cdat;
  int rval;
  while (true) {
    rval = resp_cmd_process(data->epoll_fd, resp_cmd_init(0x0), &cdat, &f, &fd, &ts);
    if (rval < 0) {
      if (rval == -1) {
        shutdown(fd, 0);
        close(fd);
        printf("Cleaned up %d\n", fd);
      }
      continue;
    } else if (rval > 0) {
      continue;
    }

    resp_conn_data_prime(cdat, data->epoll_fd);
    // msleep(1);
    send(fd, send_str, sizeof(send_str) - 1, MSG_NOSIGNAL);
    // printf("REPLI %.*s\n", sizeof(send_str) - 1, send_str);
  }
}

int main(int argc, char *argv[]) {
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
  int epoll_fd = epoll_create(1024); // Just a hint; redis uses this so I guess we will
  struct epoll_event ev;
  struct ep_data *d;
  for (int i = 0; i < thread_count; i++) {
    d = malloc(sizeof(struct ep_data));
    d->epoll_fd = epoll_fd;
    pthread_create(&pid, NULL, onestep_thread, d);
  }
  while (1) {
    client_sock = accept(socket_fd, (struct sockaddr *)&their_addr, &addr_size);
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = resp_conn_data_init(client_sock, NULL);
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev);
    printf("Accepted a conn %d! %p\n", client_sock, ev.data.ptr);
  }
}

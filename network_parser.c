#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include "lib/resp_parser.h"

struct conn_data {
  int fd;
};

static const char ok_msg[] = "+OK\r\n";

static void *connhandler(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct data_wrap *dw = dw_init(cdata->fd, 256);
  enum RESP_TYPE type;
  struct resp_response resp;
  int pack_err;
  char error_buf[512];
  int error_buf_len;
  do {
    pack_err = process_packet(dw, &resp);
    if (pack_err == -1) {
      printf("Connection %d closed\n", cdata->fd);
      break;
    } else if (pack_err == -2) {
      printf("Protocol error: %d\n", pack_err);
      break;
    }
    print_response(&resp);
    type = resp.type;
    if (type == PROCESSING_ERROR) {
      error_buf_len = snprintf(error_buf, sizeof(error_buf), "%s", resp.data.processing_error);
      if (error_buf_len > 0) {
        send(cdata->fd, error_buf, (size_t)(error_buf_len), MSG_NOSIGNAL);
      } else {
        printf("Failed to send error\n");
      }
    } else {
      send(cdata->fd, ok_msg, sizeof(ok_msg) - 1, MSG_NOSIGNAL);
    }
  } while (type != PROCESSING_ERROR);
  close(cdata->fd);
  return NULL;
}

int main() {
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
  while (1) {
    client_sock = accept(socket_fd, (struct sockaddr *)&their_addr, &addr_size);
    printf("Accepted a conn %d!\n", client_sock);
    struct conn_data *obj = malloc(sizeof(struct conn_data));
    obj->fd = client_sock;
    pthread_create(&pid, NULL, connhandler, obj);
  }
}

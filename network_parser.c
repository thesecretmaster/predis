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

const char ok_msg[] = "+OK\r\n";

void *connhandler(void *_cdata) {
  struct conn_data *cdata = _cdata;
  struct resp_response *resp;
  struct data_wrap *dw = dw_init(cdata->fd, 256);
  enum RESP_TYPE type;
  do {
    resp = process_packet(dw);
    if (resp == NULL) {
      printf("Connection %d closed\n", cdata->fd);
      close(cdata->fd);
      return NULL;
    }
    print_response(resp);
    send(cdata->fd, ok_msg, sizeof(ok_msg) - 1, MSG_NOSIGNAL);
    type = resp->type;
    free(resp);
  } while (type != PROCESSING_ERROR);
  printf("Error!!!! Bad!!!!! Connhandler!!!!\n");
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
  return 0;
}

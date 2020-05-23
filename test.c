#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char cmd1[] = "*1\r\n$7\r\nCOMMAND\r\n";
const char cmd2[] = "*3\r\n$4\r\ntest\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
int main() {
  int sockfd, connfd;
  struct sockaddr_in servaddr, cli;

  // socket create and varification
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
      printf("socket creation failed...\n");
      exit(0);
  }
  else
      printf("Socket successfully created..\n");
  bzero(&servaddr, sizeof(servaddr));

  // assign IP, PORT
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  servaddr.sin_port = htons(8080);

  // connect the client socket to server socket
  if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
      printf("connection with the server failed...\n");
      exit(0);
  }
  else {
      printf("connected to the server..\n");
      send(sockfd, cmd1, sizeof(cmd1) - 1, MSG_EOR);
      nanosleep((const struct timespec[]){{0, 50000000L}}, NULL);
      send(sockfd, cmd2, sizeof(cmd2) - 1, MSG_EOR);
      close(sockfd);
  }
  return 0;
}

#include "lib/command_ht.h"
#include "commands.h"
#include "predis_ctx.h"
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>

/*
a|foobar|b foobar is looped
w = write, existance optional
W = write, existance mandatory
r = read, existance optional
R = read, existance mandatory
c = write, non-existance mandatory

s = string
i = int
*/
int register_command(struct predis_ctx *ctx, const char *command_name, command_func command, const char *format) {
  command_ht_store(ctx->command_ht, command_name, command, format, false);
  return 0;
}

int replySimpleString(struct predis_ctx *ctx, const char *ss) {
  if (!ctx->needs_reply)
    return 1;

  unsigned long ss_len = strlen(ss);
  char *buf;
  if (1 + ss_len + 2 > PREDIS_CTX_CHAR_BUF_SIZE)
    buf = malloc(sizeof(char) * (1 + ss_len + 2));
  else
    buf = ctx->reply_buf;
  buf[0] = '+';
  buf[1 + ss_len] = '\r';
  buf[1 + ss_len + 1] = '\n';
  memcpy(buf + 1, ss, ss_len);
  ctx->needs_reply = false;
  send(ctx->reply_fd, buf, 1 + ss_len + 2, 0x0);
  return 0;
}

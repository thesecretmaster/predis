#include "lib/command_ht.h"
#include "commands.h"
#include "predis_ctx.h"
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib/1r1w_queue.h"


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
int register_command(struct predis_ctx *ctx, const char *command_name, const unsigned int command_name_length, command_func command, const char *format) {
  command_ht_store(ctx->command_ht, command_name, command_name_length, command, format, false);
  printf("Stored command %s\n", command_name);
  return 0;
}

int replySimpleString(struct predis_ctx *ctx, const char *ss) {
  if (!ctx->needs_reply)
    return 1;

  // printf("REPLYSS\n");
  unsigned long ss_len = strlen(ss);
  char *buf = malloc(sizeof(char) * (1 + ss_len + 2));
  buf[0] = '+';
  buf[1 + ss_len] = '\r';
  buf[1 + ss_len + 1] = '\n';
  memcpy(buf + 1, ss, ss_len);
  ctx->needs_reply = false;
  struct pre_send_data *psd = malloc(sizeof(struct pre_send_data));
  psd->length = 1 + ss_len + 2;
  psd->msg = buf;
  queue_push(ctx->sending_queue, psd);
  return 0;
}

static const char nil_bs[] = "$-1\r\n";

int replyBulkString(struct predis_ctx *ctx, const char *ss, long ss_len) {
  if (!ctx->needs_reply)
    return 1;
  ctx->needs_reply = false;
  // printf("REPLYBS\n");
  struct pre_send_data *psd = malloc(sizeof(struct pre_send_data));
  if (ss == NULL) {
    psd->length = sizeof(nil_bs) - 1;
    psd->msg = nil_bs;
    queue_push(ctx->sending_queue, psd);
    return 0;
  }
  int bufsize = snprintf(NULL, 0, "$%lu\r\n%s\r\n", ss_len, ss);
  if (bufsize < 0)
    return -1;
  char *buf = malloc((unsigned)bufsize);
  snprintf(buf, (unsigned)bufsize, "$%lu\r\n%s\r\n", ss_len, ss);
  psd->length = (unsigned)bufsize;
  psd->msg = buf;
  queue_push(ctx->sending_queue, psd);
  return 0;
}

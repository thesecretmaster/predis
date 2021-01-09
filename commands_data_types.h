enum pre_send_type {
  PRE_SEND_SS = 1,
  PRE_SEND_NUM = 2,
  PRE_SEND_BS = 3,
  PRE_SEND_ARY = 4,
  PRE_SEND_ERR = 5
};

struct pre_send_array {
  long length;
  struct pre_send *contents;
};

struct pre_send_bs {
  long length;
  const char *contents;
};

union pre_send_data {
  const char *ss;
  const char *err;
  long num;
  struct pre_send_array array;
  struct pre_send_bs bs;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct pre_send {
  enum pre_send_type type;
  union pre_send_data data;
};

#pragma GCC diagnostic pop

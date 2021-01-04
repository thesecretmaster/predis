enum pre_send_type {
  PRE_SEND_SS,
  PRE_SEND_NUM,
  PRE_SEND_BS,
  PRE_SEND_ARY,
  PRE_SEND_ERR
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

struct pre_send {
  enum pre_send_type type;
  union pre_send_data data;
};

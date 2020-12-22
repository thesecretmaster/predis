#include "tokenizer.h"

enum ARGUMENT_TYPES {
  STRING,
  INT
};

union ARGUMENT_VALUES {
  char *string;
  int int;
};

struct argument {
  enum ARGUMENT_TYPES type;
  union ARGUMENT_VALUES value;
};

struct operation {
  int op;
  struct argument *args;
};

struct operation *tok_tokenize()

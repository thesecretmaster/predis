#include "netwrap.h"

enum RESP_TYPE {
  SIMPLE_STRING = '+',
  ERROR_STRING = '-',
  BULK_STRING = '$',
  INTEGER = ':',
  ARRAY = '*',
  PROCESSING_ERROR = '!'
};

struct resp_array {
  int length;
  struct resp_response **elements;
};

union RESP_DATA {
  char*     simple_string;
  char*     error_string;
  long long integer;
  char*     processing_error;
  char*     bulk_string;
  struct resp_array *array;
};

struct resp_response {
  enum RESP_TYPE type;
  union RESP_DATA data;
};

struct resp_response *process_packet(struct data_wrap *dw);
void print_response(struct resp_response *r);

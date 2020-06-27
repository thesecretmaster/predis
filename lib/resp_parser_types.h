struct resp_array {
  long length;
  struct resp_response *elements;
};

union RESP_DATA {
  char*     simple_string;
  char*     error_string;
  long long integer;
  const char*     processing_error;
  char*     bulk_string;
  struct resp_array array;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"
// Can't be packed better
struct resp_response {
  enum RESP_TYPE type;
  union RESP_DATA data;
};

#pragma GCC diagnostic pop

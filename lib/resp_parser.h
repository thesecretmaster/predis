enum RESP_TYPE {
  SIMPLE_STRING = '+',
  ERROR_STRING = '-',
  BULK_STRING = '$',
  INTEGER = ':',
  ARRAY = '*',
  PROCESSING_ERROR = '!'
};

typedef long bulkstring_size_t;

struct resp_allocations;
struct resp_reciever_data;

struct resp_reciever_data *resp_initialize_reciever(int fd);
struct resp_allocations *resp_cmd_init(unsigned int tag);
unsigned int resp_get_tag(struct resp_allocations *allocs);
int resp_cmd_process(struct resp_reciever_data*, struct resp_allocations * const allocs);
void resp_cmd_free(struct resp_allocations * const allocs);
void resp_cmd_args(struct resp_allocations * const allocs, long*, char***, bulkstring_size_t **);
int resp_reciever_label(struct resp_reciever_data *rrd);

#ifdef RESP_PARSER_USE_DW
// See comment at the top of resp_parser_dw.c for an explanation of why this is
// here.
#include "netwrap.h"

struct resp_response;

int resp_process_packet(struct data_wrap *dw, struct resp_response*);
int resp_process_command(struct data_wrap *dw, char ***array, int array_length, int);
void resp_print(struct resp_response *r);
const char *resp_error(struct resp_response *r);
struct resp_response *resp_alloc(void);
char *resp_bulkstring_array_fetch(struct resp_response *r, unsigned long idx);
long resp_array_length(struct resp_response *r);
enum RESP_TYPE resp_type(struct resp_response *r);

#endif // RESP_PARSER_USE_DW

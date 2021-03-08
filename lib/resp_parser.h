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
struct resp_conn_data;

enum resp_sm_status {
  RESP_SM_STATUS_ERROR,
  RESP_SM_STATUS_EMPTY,
  RESP_SM_STATUS_CLOSED,
  RESP_SM_STATUS_MORE,
  RESP_SM_STATUS_DONE
};
struct resp_sm;
int resp_sm_fd(struct resp_sm *sm);
struct resp_sm *resp_sm_init(int fd, void *data);
void *resp_sm_data(struct resp_sm *sm);
struct resp_allocations *resp_cmd_sm_allocs(struct resp_sm *sm);
enum resp_sm_status resp_cmd_process_sm(struct resp_sm *sm);

struct resp_allocations *resp_cmd_init(unsigned int tag);
unsigned int resp_get_tag(struct resp_allocations *allocs);
int resp_cmd_process(int epoll_fd, struct resp_allocations * const allocs, struct resp_conn_data **cdata, void**data, int *fd);
void resp_cmd_free(struct resp_allocations * const allocs);
void resp_cmd_args(struct resp_allocations * const allocs, long*, char***, bulkstring_size_t **);
struct resp_conn_data *resp_conn_data_init(int fd, void *data);
void resp_conn_data_prime(struct resp_conn_data *cdata, int epoll_fd);

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

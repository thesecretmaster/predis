#include "netwrap.h"

struct resp_response;

enum RESP_TYPE {
  SIMPLE_STRING = '+',
  ERROR_STRING = '-',
  BULK_STRING = '$',
  INTEGER = ':',
  ARRAY = '*',
  PROCESSING_ERROR = '!'
};


int resp_process_packet(struct data_wrap *dw, struct resp_response*);
void resp_print(struct resp_response *r);
const char *resp_error(struct resp_response *r);
struct resp_response *resp_alloc(void);
char *resp_bulkstring_array_fetch(struct resp_response *r, unsigned long idx);
long resp_array_length(struct resp_response *r);
enum RESP_TYPE resp_type(struct resp_response *r);
int resp_process_command(struct data_wrap *dw, char ***array, int array_length, int);
struct resp_allocations {
  int allocation_count;
  int allocation_array_size;
  char **allocations;
  char **argv;
  int argc;
};
struct resp_spare_page;
struct resp_spare_page *init_spare_page(void);
int cmd_proc(int fd, struct resp_allocations * const allocs, struct resp_spare_page * const spare_page);
void free_allocs(struct resp_allocations * const allocs);

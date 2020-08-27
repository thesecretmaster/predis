#ifndef RESP_PARSER_H
#define RESP_PARSER_H

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
struct resp_allocations;
struct resp_spare_page;
struct resp_spare_page *resp_cmd_init_spare_page(void);
struct resp_allocations *resp_cmd_init(void);
int resp_cmd_process(int fd, struct resp_allocations * const allocs, struct resp_spare_page * const spare_page);
void resp_cmd_free(struct resp_allocations * const allocs);
void resp_cmd_args(struct resp_allocations * const allocs, int*, char***, unsigned long **);
#endif

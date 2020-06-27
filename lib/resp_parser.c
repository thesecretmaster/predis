#include "resp_parser.h"
#include "resp_parser_types.h"
#include <stdlib.h>
#include <stdio.h>

const char *resp_error(struct resp_response *r) {
  return r->type == PROCESSING_ERROR ? r->data.processing_error : NULL;
}

struct resp_response *resp_alloc() {
  return malloc(sizeof(struct resp_response));
}

static const char *process_string_net_err = "process string network error";
static const char *process_string_malloc_err = "process string malloc error";
static const char *process_string_dw_err = "process string dw error";
static const char *process_string_length_err = "process string length error";
static const int max_string_size = 512;
static const char resp_line_terminator = '\r';

static void process_string(struct data_wrap *dw, struct resp_response * const response) {
  size_t size = 0;
  size_t buf_size_incr;
  char *buf = NULL;
  char *nbuf;
  long retval;
  do {
    buf_size_incr = size < max_string_size ? size : max_string_size;
    if (buf_size_incr < 8)
      buf_size_incr = 8;
    nbuf = realloc(buf, sizeof(char) * (size + buf_size_incr));
    if (nbuf == NULL) {
      free(buf);
      response->type = PROCESSING_ERROR;
      response->data.processing_error = process_string_malloc_err;
      return;
    } else {
      buf = nbuf;
    }
    retval = dw_read_copy(dw, buf + size, (long)buf_size_incr, &resp_line_terminator);
    size += buf_size_incr;
  } while (retval == -1);
  dw_eat(dw, 2);
  if (retval <= -2) {
    free(buf);
    response->type = PROCESSING_ERROR;
    response->data.processing_error = process_string_net_err;
    if (retval <= -4)
      response->data.processing_error = process_string_dw_err;
  } else if ((long)(size - buf_size_incr) + retval - 2 < 0) {
    response->type = PROCESSING_ERROR;
    response->data.processing_error = process_string_length_err;
  } else {
    buf[(long)(size - buf_size_incr) + retval] = '\0';
    // Note: If it's an error string, we fix it outside this method
    response->type = SIMPLE_STRING;
    response->data.simple_string = buf;
  }
}

static const char* process_int_net_err = "process int network error";

static void process_int(struct data_wrap *dw, struct resp_response * const response) {
  long long i = 0;
  char *nocopy_ptr;
  bool nocopy_again;
  long recv_len;
  do {
    recv_len = dw_read_nocopy(dw, -1, &resp_line_terminator, &nocopy_ptr, &nocopy_again);
    if (recv_len < 0) {
      response->type = PROCESSING_ERROR;
      response->data.processing_error = process_int_net_err;
      return;
    }
    // Funny special case: if dw_read_nocopy hits the end, it doesn't know if
    // there are more bytes of the number that are about to be fetched, so it
    // says to do it again. But, if there are no numbers at the start of the
    // buffer, it'll return a length of 0. Then when we multiply, that'd erase
    // i and mess everything up.
    if (recv_len != 0) {
      i = i * 10 * recv_len;
      i += strtoll(nocopy_ptr, NULL, 10);
    }
  } while (nocopy_again);
  dw_eat(dw, 2);
  response->type = INTEGER;
  response->data.integer = i;
}

static const char *process_bulk_string_net_err = "error in getting bulk string from network";
static const char *process_bulk_string_malloc_err = "error in allocating memory for bulk string";
static const char *process_bulk_string_length_err = "length of string too negative";

static void process_bulk_string(struct data_wrap *dw, struct resp_response * const response) {
  process_int(dw, response);
  char *bs_buf;
  long main_recv;
  long cleanup_recv;
  size_t bs_length;
  if (response->type != PROCESSING_ERROR) {
    // Handle the case of a bulk string length -1 being a NULL
    if (response->data.integer == -1) {
      bs_buf = NULL;
    } else if (response->data.integer < -1) {
      response->type = PROCESSING_ERROR;
      response->data.processing_error = process_bulk_string_length_err;
      return;
    } else {
      bs_length = (size_t)response->data.integer;
      bs_buf = malloc(sizeof(char) * (bs_length + 1));
      if (bs_buf == NULL) {
        response->type = PROCESSING_ERROR;
        response->data.processing_error = process_bulk_string_malloc_err;
        return;
      }
      // The second read is to handle the trailing \r\n
      main_recv = dw_read_copy(dw, bs_buf, (long)bs_length, NULL);
      cleanup_recv = dw_eat(dw, 2);
      if (main_recv <= 0 && cleanup_recv <= 0) {
        response->type = PROCESSING_ERROR;
        response->data.processing_error = process_bulk_string_net_err;
        return;
      }
      bs_buf[bs_length] = '\0';
    }
    response->type = BULK_STRING;
    response->data.bulk_string = bs_buf;
  }
}

static const char *process_array_malloc_err = "error allocating memory for array";
static const char *process_array_length_err = "array length less than -1";

static void process_array(struct data_wrap *dw, struct resp_response * const response) {
  process_int(dw, response);
  long length;
  struct resp_response *ary_elems;
  if (response->type != PROCESSING_ERROR) {
    length = response->data.integer;
    response->data.array.length = length;
    // Handle the case of a bulk string length -1 being a NULL
    if (length == -1) {
      response->data.array.elements = NULL;
    } else if (length < -1) {
      response->type = PROCESSING_ERROR;
      response->data.processing_error = process_array_length_err;
      return;
    } else {
      ary_elems = malloc(sizeof(struct resp_response) * (size_t)length);
      if (ary_elems == NULL) {
        response->type = PROCESSING_ERROR;
        response->data.processing_error = process_array_malloc_err;
        return;
      }
      response->data.array.elements = ary_elems;
      for (int i = 0; i < length; i++) {
        resp_process_packet(dw, &response->data.array.elements[i]);
      }
    }
    response->type = ARRAY;
  }
}

static void print_response_inner(struct resp_response *r, int depth) {
  if (r == NULL) {
    printf("Resp is null\n");
    return;
  }
  switch (r->type) {
    case PROCESSING_ERROR : {
      printf("A processing error occured");
      break;
    }
    case SIMPLE_STRING : {
      printf("\"%s\"", r->data.simple_string);
      break;
    }
    case ERROR_STRING : {
      printf("ERR \"%s\"", r->data.error_string);
      break;
    }
    case INTEGER : {
      printf("%lld", r->data.integer);
      break;
    }
    case BULK_STRING : {
      printf("(BS) \"%s\"", r->data.bulk_string == NULL ? "(null bulk string)" : r->data.bulk_string);
      break;
    }
    case ARRAY : {
      printf("[\n");
      for (int i = 0; i < r->data.array.length; i++) {
        for (int j = 0; j < depth; j++) {
          printf("  ");
        }
        // printf("%d. ", i + 1);
        print_response_inner(&r->data.array.elements[i], depth + 1);
        if (i < r->data.array.length - 1)
          printf(",");
        printf("\n");
      }
      for (int j = 0; j < depth - 1; j++) {
        printf("  ");
      }
      printf("]");
      break;
    }
  }
  if (depth == 1)
    printf("\n");
}

void resp_print(struct resp_response *r) {
  print_response_inner(r, 1);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

int resp_process_packet(struct data_wrap *dw, struct resp_response * const response) {
  char raw_type;
  long rval = dw_read_copy(dw, &raw_type, 1, NULL);
  if (rval == -3 || rval == -2) {
    // Connection is closed, can't read type
    return -1;
  }
  switch((enum RESP_TYPE)raw_type) {
    case SIMPLE_STRING : {
      process_string(dw, response);
      break;
    }
    case ERROR_STRING : {
      process_string(dw, response);
      if (response->type == SIMPLE_STRING)
        response->type = ERROR_STRING;
      break;
    }
    case INTEGER : {
      process_int(dw, response);
      break;
    }
    case BULK_STRING : {
      process_bulk_string(dw, response);
      break;
    }
    case ARRAY : {
      process_array(dw, response);
      break;
    }
    default : {
      printf("Invalid type %c\n", raw_type);
      return -2;
    }
  }
  return 0;
}

#pragma GCC diagnostic pop

char *resp_bulkstring_array_fetch(struct resp_response *r, int idx) {
  if (r->type != ARRAY || idx >= r->data.array.length)
    return NULL;
  struct resp_response *resp = &(r->data.array.elements[idx]);
  if (resp->type != BULK_STRING)
    return NULL;
  return resp->data.bulk_string;
}

long resp_array_length(struct resp_response *r) {
  if (r->type != ARRAY)
    return -1;
  return r->data.array.length;
}

enum RESP_TYPE resp_type(struct resp_response *r) {
  return r->type;
}

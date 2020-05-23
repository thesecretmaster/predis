#include "resp_parser.h"
#include <stdlib.h>
#include <stdio.h>

int base_string_size = 8;
char *process_string_err = "process string error";

struct resp_response *process_string(struct data_wrap *dw) {
  struct resp_response *response = malloc(sizeof(struct resp_response));
  int size = base_string_size;
  char *buf = malloc(sizeof(char) * (size));
  const char end = '\n';
  int buf_size_incr = base_string_size;
  int retval = dw_read(dw, buf, size, &end);
  while (retval == -1) {
    buf_size_incr = base_string_size;
    buf = realloc(buf, sizeof(char) * (size + buf_size_incr));
    retval = dw_read(dw, buf + size, buf_size_incr, &end);
    size += buf_size_incr;
  }
  if (retval == -2) {
    free(buf);
    response->type = PROCESSING_ERROR;
    response->data.processing_error = process_string_err;
  } else {
    buf[size - buf_size_incr + retval - 2] = '\0';
    // Note: If it's an error string, we fix it outside this method
    response->type = SIMPLE_STRING;
    response->data.simple_string = buf;
  }
  return response;
}

struct resp_response *process_int(struct data_wrap *dw) {
  struct resp_response *response = process_string(dw);
  long long i;
  if (response->type != PROCESSING_ERROR) {
    i = strtoll(response->data.simple_string, NULL, 10);
    free(response->data.simple_string);
    response->type = INTEGER;
    response->data.integer = i;
  }
  return response;
}

char *process_bulk_string_err = "error in bulk str";

struct resp_response *process_bulk_string(struct data_wrap *dw) {
  struct resp_response *response = process_int(dw);
  char *bs_buf;
  int main_recv;
  int cleanup_recv;
  if (response->type != PROCESSING_ERROR) {
    // Handle the case of a bulk string length -1 being a NULL
    if (response->data.integer == -1) {
      bs_buf = NULL;
    } else {
      bs_buf = malloc(sizeof(char) * (response->data.integer + 1));
      // The second read is to handle the trailing \r\n
      main_recv = dw_read(dw, bs_buf, response->data.integer, NULL);
      cleanup_recv = dw_read(dw, NULL, 2, NULL);
      if (main_recv != -1 && cleanup_recv != -1) {
        response->type = PROCESSING_ERROR;
        response->data.processing_error = process_bulk_string_err;
        return response;
      }
      bs_buf[response->data.integer] = '\0';
    }
    response->type = BULK_STRING;
    response->data.bulk_string = bs_buf;
  }
  return response;
}

struct resp_response *process_array(struct data_wrap *dw) {
  struct resp_response *response = process_int(dw);
  struct resp_array *ary;
  if (response->type != PROCESSING_ERROR) {
    ary = malloc(sizeof(struct resp_array));
    ary->length = response->data.integer;
    // Handle the case of a bulk string length -1 being a NULL
    if (response->data.integer == -1) {
      ary->elements = NULL;
    } else {
      ary->elements = malloc(sizeof(struct resp_response*) * response->data.integer);
      for (int i = 0; i < response->data.integer; i++) {
        ary->elements[i] = process_packet(dw);
      }
    }
    response->type = ARRAY;
    response->data.array = ary;
  }
  return response;
}

void print_response_inner(struct resp_response *r, int depth) {
  if (r == NULL) {
    printf("Resp is null\n");
    return;
  }
  switch (r->type) {
    case PROCESSING_ERROR : {
      printf("A processing error occured\n");
      break;
    }
    case SIMPLE_STRING : {
      printf("SS| \"%s\"\n", r->data.simple_string);
      break;
    }
    case ERROR_STRING : {
      printf("ES| \"%s\"\n", r->data.error_string);
      break;
    }
    case INTEGER : {
      printf("I | %lld\n", r->data.integer);
      break;
    }
    case BULK_STRING : {
      printf("BS| \"%s\"\n", r->data.bulk_string == NULL ? "(null bulk string)" : r->data.bulk_string);
      break;
    }
    case ARRAY : {
      for (int i = 0; i < r->data.array->length; i++) {
        for (int j = 0; j < depth; j++) {
          printf("   ");
        }
        printf("%d|", i + 1);
        if (r->data.array->elements[i]->type == ARRAY) {
          printf("A%d\n", r->data.array->length);
        }
        print_response_inner(r->data.array->elements[i], depth + 1);
      }
      break;
    }
  }
}

void print_response(struct resp_response *r) {
  print_response_inner(r, 1);
}

struct resp_response *process_packet(struct data_wrap *dw) {
  char raw_type;
  if (dw_read(dw, &raw_type, 1, NULL) != -1) {
    printf("Connectino closed\n");
    return NULL;
  }
  struct resp_response *response;
  switch((enum RESP_TYPE)raw_type) {
    case SIMPLE_STRING : {
      response = process_string(dw);
      break;
    }
    case ERROR_STRING : {
      response = process_string(dw);
      response->type = ERROR_STRING;
      break;
    }
    case INTEGER : {
      response = process_int(dw);
      break;
    }
    case BULK_STRING : {
      response = process_bulk_string(dw);
      break;
    }
    case ARRAY : {
      response = process_array(dw);
      break;
    }
    default : {
      printf("Invalid type %c\n", raw_type);
      return NULL;
    }
  }
  if (response == NULL) {
    printf("NULL REPOSNE OF TEYP %c\n", raw_type);
  }
  return response;
}

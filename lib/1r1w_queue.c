#include <stdlib.h>
#include "1r1w_queue.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct queue_elem {
  char **argv;
  int argc;
};


struct queue {
  struct queue_elem *elements;
  int length;
  volatile int tail;
  volatile int head;
};

#pragma GCC diagnostic pop

struct queue *queue_init(int qlen) {
  struct queue *q = malloc(sizeof(struct queue));
  if (q == NULL)
    return NULL;
  q->length = qlen;
  q->tail = 0;
  q->head = 0;
  q->elements = malloc(sizeof(struct queue_elem) * (unsigned long)qlen);
  if (q->elements == NULL) {
    free(q);
    return NULL;
  }
  for (int i = 0; i < qlen; i++) {
    q->elements[i].argv = NULL;
    q->elements[i].argc = -1;
  }
  return q;
}

int queue_push(struct queue *q, char **argv, int argc) {
  if (q->tail - q->head == q->length) {
    return -1;
  }
  q->elements[q->tail % q->length].argv = argv;
  q->elements[q->tail % q->length].argc = argc;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  q->tail += 1;
  return 0;
}

int queue_pop(struct queue *q, char ***argv, int *argc) {
  if (q->tail - q->head == 0) {
    return -1;
  }
  *argv = q->elements[q->head % q->length].argv;
  *argc = q->elements[q->head % q->length].argc;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  q->head += 1;
  return 0;
}

int queue_length(struct queue *q) {
  return q->length;
}

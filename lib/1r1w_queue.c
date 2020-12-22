#include <stdlib.h>
#include <string.h>
#include "1r1w_queue.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/time.h>

__attribute__((unused))
static long
futex(volatile int *uaddr, int futex_op, int val,
     const struct timespec *timeout, int *uaddr2, int val3)
{
   return syscall(SYS_futex, uaddr, futex_op, val,
                  timeout, uaddr2, val3);
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"


struct queue {
  char *elements;
  unsigned  length;
  unsigned long elem_length;
  __attribute__((aligned(8))) volatile unsigned int head;
  volatile unsigned int tail;
  volatile bool closed;
};

#pragma GCC diagnostic pop

struct queue *queue_init(unsigned int qlen, unsigned long elem_len) {
  struct queue *q = malloc(sizeof(struct queue));
  if (q == NULL)
    return NULL;
  q->length = qlen;
  q->tail = 0;
  q->head = 0;
  q->closed = false;
  q->elem_length = elem_len;
  q->elements = malloc(elem_len * (unsigned long)qlen);
  if (q->elements == NULL) {
    free(q);
    return NULL;
  }
  memset(q->elements, 0x0, elem_len * (unsigned long)qlen);
  return q;
}

void queue_close(struct queue *q) {
  q->closed = true;
}

bool queue_closed(struct queue *q) {
  return q->closed && (q->tail - q->head == 0);
}

int queue_push(struct queue *q, void *elem) {
  if (q->tail - q->head == q->length) {
    return -1;
  }
  memcpy(q->elements + (q->elem_length * (q->tail % q->length)), elem, q->elem_length);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  q->tail += 1;
  // futex(&q->tail, FUTEX_WAKE, 1, NULL, NULL, 0x0);
  return 0;
}
int queue_pop(struct queue *q, void *elem_targ) {
  // struct timespec ts;
  // ts.tv_sec = 1;
  // ts.tv_nsec = 0;
  // unsigned int iters = 0;
  // retry: {
    unsigned int tail = q->tail;
    if (tail - q->head == 0) {
      // futex(&q->tail, FUTEX_WAIT, q->head, NULL/* &ts */, NULL, 0x0);
      // iters++;
      // if (iters >= 100) {
      //   goto retry;
      // }
      return -1;
    }
    memcpy(elem_targ, q->elements + (q->elem_length * (q->head % q->length)), q->elem_length);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    q->head += 1;
    return 0;
  // }
}

unsigned int queue_length(struct queue *q) {
  return q->length;
}
#include <stdio.h>
#include <stdint.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
unsigned int queue_size(struct queue *q) {
  uint64_t qsize;
  uint64_t *qsize_ptr = &qsize;
  unsigned int head;
  unsigned int tail;
  if (sizeof(q->tail) * 2 == sizeof(uint64_t)) {
    __atomic_load((volatile uint64_t*)&(q->head), qsize_ptr, __ATOMIC_SEQ_CST);
    head = *((unsigned int*)qsize_ptr);
    tail = *(((unsigned int*)qsize_ptr) + 1);
    return tail - head;
  } else {
    printf("size %lu\n", sizeof(q->tail));
  }
  return q->tail - q->head;
}
#pragma GCC diagnostic pop

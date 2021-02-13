#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "send_queue.h"

#define SQE_INDEX(q, nfam, i) ((struct send_queue_elem*)(((char*)nfam) + (i * (sizeof(struct send_queue_elem) + q->element_length))))

struct send_queue_elem {
  volatile bool ready;
  char data[];
};

struct send_queue {
  unsigned int element_length;
  unsigned int length;
  unsigned int head_lock;
  unsigned int head;
  unsigned int tail;
  struct send_queue_elem contents[];
};

struct send_queue *send_queue_init(unsigned int length, unsigned int element_length) {
  struct send_queue *q = malloc(sizeof(struct send_queue) + (element_length + sizeof(struct send_queue_elem))*length);
  q->length = length;
  q->element_length = element_length;
  q->head = 0;
  q->tail = 0;
  q->head_lock = 0;
  for (unsigned int i = 0; i < length; i++) {
    SQE_INDEX(q, q->contents, i)->ready = false;
    memset(SQE_INDEX(q, q->contents, i)->data, 0x0, element_length);
  }
  return q;
}
// #include <stdio.h>
int send_queue_register(struct send_queue *q) {
  if (q->tail - q->head < q->length) {
    unsigned int rv = q->tail;
    SQE_INDEX(q, q->contents, rv % q->length)->ready = false;
    memset(SQE_INDEX(q, q->contents, rv % q->length)->data, 0x0, q->element_length);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    q->tail += 1;
    // printf("Regised to q w id %u\n", rv);
    return (int)rv;
  } else {
    return -1; // Queue is full
  }
}

void send_queue_commit(struct send_queue *q, int idx, void *data) {
  // printf("commit into %u\n", idx);
  memcpy(SQE_INDEX(q, q->contents, idx % q->length)->data, data, q->element_length);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  __atomic_store_n(&SQE_INDEX(q, q->contents, idx % q->length)->ready, true, __ATOMIC_SEQ_CST);
}

// int send_queue_pop(struct send_queue *q, void **data) {
//   if (q->head == q->tail)
//     return -1; // Queue is empty
//   unsigned int attempt_idx = q->head;
//   void *data_in_q;
//   if (!q->contents[attempt_idx % q->length].ready) {
//     return -2; // Head of queue is not ready for reading
//   } else {
//     data_in_q = q->contents[attempt_idx % q->length].data;
//     if (__atomic_compare_exchange_n(&q->head, &attempt_idx, attempt_idx + 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
//       *data = data_in_q;
//       return (int)attempt_idx;
//     } else {
//       return -3; // Well, we failed today but we'll try again tomorrow
//     }
//   }
// }


int send_queue_pop_start(struct send_queue *q, void **data) {
  unsigned int attempt_idx = q->head;
  if (attempt_idx == q->tail)
    return -1; // Queue is empty
  char data_in_q[q->element_length];
  if (!SQE_INDEX(q, q->contents, attempt_idx % q->length)->ready) {
    return -2; // Head of queue is not ready for reading
  } else {
    // printf("popping from %u\n", attempt_idx);
    memcpy(data_in_q, SQE_INDEX(q, q->contents, attempt_idx % q->length)->data, q->element_length);
    if (__atomic_compare_exchange_n(&q->head_lock, &attempt_idx, attempt_idx + 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      memcpy(data, data_in_q, q->element_length);
      return 0;
    } else {
      return -3; // Well, we failed today but we'll try again tomorrow
    }
  }
}

int send_queue_pop_continue(struct send_queue *q, void **data) {
  if (SQE_INDEX(q, q->contents, (q->head + 1) % q->length)->ready && q->head_lock != q->tail) {
    q->head_lock += 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    q->head += 1;
    memcpy(data, SQE_INDEX(q, q->contents, q->head % q->length)->data, q->element_length);
    // printf("pc good %u\n", q->head);
    return 0;
  } else {
    q->head += 1;
    // printf("pc failed\n");
    return -1;
  }
}

#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sched.h>
#include <assert.h>
#include "timer.h"

struct timing_method_interval {
  struct timespec start;
  struct timespec end;
};

struct timing_method_sum {
  struct timespec start;
  struct timespec last_start;
  struct timespec accumulator;
};

union timing_data {
  struct timing_method_interval interval;
  struct timing_method_sum sum;
};

enum timer_segment_type {
  SEGMENT_SUM,
  SEGMENT_INTERVAL
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct timer_segment {
  union timing_data data;
  unsigned int tag;
  enum timer_segment_type segment_type;
  enum interval_type type;
};

#pragma GCC diagnostic pop

typedef struct timer_segment timer_interval;
typedef struct timer_segment timer_sum;

struct time_block {
  struct time_block *prev;
  struct time_block *next;
  unsigned index;
  unsigned size;
  struct timer_segment *intervals;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct timer {
  struct time_block *time_block;
  int pool_id;
  enum thread_type type;
};

#pragma GCC diagnostic pop

struct timer_ll {
  struct timer *timer;
  struct timer_ll *next;
};

static struct timer_ll *head = NULL;

static struct time_block *tblock_init(unsigned int interval_count) {
  struct time_block *tb = malloc(sizeof(struct time_block));
  tb->prev = NULL;
  tb->next = NULL;
  tb->index = 0;
  tb->size = interval_count;
  tb->intervals = malloc(sizeof(struct timer_segment) * interval_count);
  return tb;
}

struct timer *timer_init(int pool_id, enum thread_type ttype) {
  struct timer *t = malloc(sizeof(struct timer));
  t->pool_id = pool_id;
  t->type = ttype;
  t->time_block = tblock_init(1024);
  struct timer_ll *null = NULL;
  struct timer_ll *curr;
  struct timer_ll *ll_node = malloc(sizeof(struct timer_ll));
  ll_node->timer = t;
  ll_node->next = NULL;
  if (!__atomic_compare_exchange_n(&head, &null, ll_node, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    curr = head;
    do {
      while (curr->next != NULL) {
        curr = curr->next;
      }
      null = NULL;
    } while (!__atomic_compare_exchange_n(&(curr->next), &null, ll_node, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
  }
  return t;
}

timer_interval *timer_start_interval(struct timer *t, enum interval_type itype, unsigned int tag) {
  struct time_block *old_tblock;
  if (t->time_block->index == t->time_block->size) {
    old_tblock = t->time_block;
    t->time_block = tblock_init(1024);
    t->time_block->prev = old_tblock;
    old_tblock->next = t->time_block;
  }
  struct timer_segment *tint = &(t->time_block->intervals[t->time_block->index]);
  t->time_block->index += 1;

  tint->type = itype;
  tint->tag = tag;
  tint->segment_type = SEGMENT_INTERVAL;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &(tint->data.interval.start));
  return tint;
}

timer_sum *timer_start_sum(struct timer *t, enum interval_type itype, unsigned int tag) {
  struct time_block *old_tblock;
  if (t->time_block->index == t->time_block->size) {
    old_tblock = t->time_block;
    t->time_block = tblock_init(1024);
    t->time_block->prev = old_tblock;
    old_tblock->next = t->time_block;
  }
  struct timer_segment *tint = &(t->time_block->intervals[t->time_block->index]);
  t->time_block->index += 1;

  tint->type = itype;
  tint->tag = tag;
  tint->segment_type = SEGMENT_SUM;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &(tint->data.sum.start));
  tint->data.sum.last_start = tint->data.sum.start;
  tint->data.sum.accumulator = (struct timespec){.tv_sec = 0, .tv_nsec = 0};
  return tint;
}

void timer_stop(timer_interval *tint) {
  assert(tint->segment_type == SEGMENT_INTERVAL);
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &(tint->data.interval.end));
}

#define NSECS_IN_SEC 1000000000
void timer_incr(timer_sum *tint) {
  assert(tint->segment_type == SEGMENT_SUM);
  struct timespec curr_time;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);
  struct timespec time_diff;
  time_diff.tv_nsec = curr_time.tv_nsec - tint->data.sum.last_start.tv_nsec;
  time_diff.tv_sec = curr_time.tv_sec - tint->data.sum.last_start.tv_sec;
  if (time_diff.tv_nsec < 0) {
    time_diff.tv_nsec += NSECS_IN_SEC;
    time_diff.tv_sec -= 1;
  }
  tint->data.sum.accumulator.tv_sec += time_diff.tv_sec;
  tint->data.sum.accumulator.tv_nsec += time_diff.tv_nsec;
  if (tint->data.sum.accumulator.tv_nsec > NSECS_IN_SEC) {
    tint->data.sum.accumulator.tv_nsec -= NSECS_IN_SEC;
    tint->data.sum.accumulator.tv_sec += 1;
  }
  tint->data.sum.last_start = curr_time; // This shoudln't be needed, just a failsafe
}
#undef NSECS_IN_SEC

void timer_restart(timer_sum *tint) {
  assert(tint->segment_type == SEGMENT_SUM);
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &(tint->data.sum.last_start));
}

#include <stdio.h>

// https://stackoverflow.com/q/30895970/4948732
static bool timespec_lt(struct timespec a, struct timespec b) {
    if (a.tv_sec == b.tv_sec)
        return a.tv_nsec < b.tv_nsec;
    else
        return a.tv_sec < b.tv_sec;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct timer_print {
  struct time_block *block;
  struct timer *timer;
  unsigned index;
};

#pragma GCC diagnostic pop

struct timer_active_interval {
  struct timer_segment *interval;
  struct timer_active_interval *next;
};

static inline struct timespec timer_segment_get_start(struct timer_segment seg) {
  switch (seg.segment_type) {
    case (SEGMENT_SUM) : return seg.data.sum.start;
    case (SEGMENT_INTERVAL) : return seg.data.interval.start;
  }
}

static inline struct timespec timer_segment_get_end(struct timer_segment seg) {
  switch (seg.segment_type) {
    case (SEGMENT_SUM) : return seg.data.sum.last_start;
    case (SEGMENT_INTERVAL) : return seg.data.interval.end;
  }
}

void timer_print() {
  for (int i = 0; i < 1000; i++) {
    sched_yield();
  }
  printf("Printing timer!\n");
  unsigned j = 0;
  struct timer_ll *curr = head;
  while (curr != NULL) {
    j += 1;
    curr = curr->next;
  }
  struct timer_print *print_blocks = malloc(sizeof(struct timer_print) * j);
  curr = head;
  for (unsigned int i = 0; i < j; i++) {
    print_blocks[i].timer = curr->timer;
    print_blocks[i].index = 0;
    print_blocks[i].block = curr->timer->time_block;
    while (print_blocks[i].block->prev != NULL) {
      print_blocks[i].block = print_blocks[i].block->prev;
    }
    curr = curr->next;
  }
  struct timer_print *lowest;
  unsigned int null_blocks;
  struct timer_active_interval *ta_head = NULL;
  struct timer_active_interval *ta_curr;
  struct timer_active_interval *prev;
  struct timer_active_interval *to_free;
  struct timer_active_interval *new;
  while (true) {
    lowest = NULL;
    null_blocks = 0;
    for (unsigned int i = 0; i < j; i++) {
      if (print_blocks[i].block == NULL) {
        null_blocks += 1;
      } else if (lowest == NULL || timespec_lt(timer_segment_get_start(print_blocks[i].block->intervals[print_blocks[i].index]), timer_segment_get_start(lowest->block->intervals[lowest->index]))) {
        lowest = &print_blocks[i];
      }
    }
    if (null_blocks == j) {
      break;
    }
    if (lowest->block->intervals[lowest->index].type != INTERVAL_QUEUE) {
      printf("%d | %s (%s): %.8x ",
        lowest->timer->pool_id,
        lowest->timer->type == THREAD_RUNNER ? "runner  " : lowest->timer->type == THREAD_SENDER ? "sender  " : lowest->timer->type == THREAD_RECIEVER ? "reciever" : "null",
        lowest->block->intervals[lowest->index].type == INTERVAL_QUEUE ? "queue  " : lowest->block->intervals[lowest->index].type == INTERVAL_RUNNING ? "running" : "null",
        lowest->block->intervals[lowest->index].tag
      );
      ta_curr = ta_head;
      prev = NULL;
      while (ta_curr != NULL) {
        if (timespec_lt(timer_segment_get_start(lowest->block->intervals[lowest->index]), timer_segment_get_end(*ta_curr->interval))) {
          printf("%.8x ", ta_curr->interval->tag);
          ta_curr = ta_curr->next;
        } else {
          if (prev == NULL) {
            ta_head = ta_curr->next;
            prev = ta_curr;
            ta_curr = ta_curr->next;
          } else {
            prev->next = ta_curr->next;
            to_free = ta_curr;
            ta_curr = ta_curr->next;
            free(to_free);
          }
        }
      }
      new = malloc(sizeof(struct timer_active_interval));
      new->interval = &lowest->block->intervals[lowest->index];
      new->next = NULL;
      if (prev == NULL) {
        ta_head = new;
      } else {
        prev->next = new;
      }
      printf("| %.6ld | %ld.%.9ld (-%ld.%.9ld)\n",
        timer_segment_get_end(lowest->block->intervals[lowest->index]).tv_nsec - timer_segment_get_start(lowest->block->intervals[lowest->index]).tv_nsec,
        timer_segment_get_start(lowest->block->intervals[lowest->index]).tv_sec, timer_segment_get_start(lowest->block->intervals[lowest->index]).tv_nsec,
        timer_segment_get_end(lowest->block->intervals[lowest->index]).tv_sec, timer_segment_get_end(lowest->block->intervals[lowest->index]).tv_nsec
      );
    }
    lowest->index += 1;
    if (lowest->index >= lowest->block->index) {
      lowest->index = 0;
      lowest->block = lowest->block->next;
    }
  }
}

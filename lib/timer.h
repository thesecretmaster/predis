enum thread_type {
  THREAD_RECIEVER,
  THREAD_RUNNER,
  THREAD_SENDER
};

enum interval_type {
  INTERVAL_QUEUE,
  INTERVAL_RUNNING
};

enum timing_method {
  TIMER_SUM,
  TIMER_INTERVAL
};
struct timer_segment;
typedef struct timer_segment timer_interval;
typedef struct timer_segment timer_sum;
struct timer;

struct timer *timer_init(int pool_id, enum thread_type ttype);
timer_interval *timer_start_interval(struct timer *t, enum interval_type itype, unsigned int tag);
timer_sum *timer_start_sum(struct timer *t, enum interval_type itype, unsigned int tag);
void timer_stop(timer_interval *tint);
void timer_incr(timer_sum *tint);
void timer_restart(timer_sum *tint);
void timer_print(void);

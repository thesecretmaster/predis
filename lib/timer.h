enum thread_type {
  THREAD_RECIEVER,
  THREAD_RUNNER,
  THREAD_SENDER
};

enum interval_type {
  INTERVAL_QUEUE,
  INTERVAL_RUNNING
};

struct timer_interval;
struct timer;

struct timer *timer_init(int pool_id, enum thread_type ttype);
struct timer_interval *timer_start(struct timer *t, enum interval_type itype, unsigned int tag);
void timer_stop(struct timer_interval *tint);
void timer_print(void);

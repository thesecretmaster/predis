struct send_queue;

struct send_queue *send_queue_init(unsigned int length, unsigned int element_length);
int send_queue_register(struct send_queue *q);
void send_queue_commit(struct send_queue *q, int idx, void *data);
int send_queue_pop_start(struct send_queue *q, void **data);
int send_queue_pop_continue(struct send_queue *q, void **data);
void send_queue_pop_end(struct send_queue *q);

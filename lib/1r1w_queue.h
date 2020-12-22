#include <stdbool.h>

struct queue;

struct queue *queue_init(unsigned int qlen, unsigned long elem_size);
int queue_push(struct queue *q, void*);
int queue_pop(struct queue *q, void*);
unsigned int queue_length(struct queue *q);
unsigned int queue_size(struct queue *q);
void queue_close(struct queue *q);
bool queue_closed(struct queue *q);

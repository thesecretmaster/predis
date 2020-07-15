struct queue;

struct queue *queue_init(int qlen);
int queue_push(struct queue *q, char **argv, int argc);
int queue_pop(struct queue *q, char ***argv, int *argc);
int queue_length(struct queue *q);

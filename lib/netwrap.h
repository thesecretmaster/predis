struct data_wrap;

struct data_wrap *dw_init(int fd, int allocation_size);
int dw_read(struct data_wrap *dw, void *buf, int size, const char *end_char);

#ifndef H_COMMANDS_TYPES
#define H_COMMANDS_TYPES

struct predis_ctx;
typedef int (*predis_init_func)(void*);
int predis_init(void *magic_obj);

#endif

#include "../lib/type_ht.h"
#include "commands_types.h"

int register_type(struct predis_ctx *ctx, const char *type_name, unsigned int type_name_length, type_init_func tinit, type_free_func tfree);

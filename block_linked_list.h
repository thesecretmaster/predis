struct block_linked_list;
struct block_linked_list_descriptor;

struct block_linked_list *bll_init(unsigned int data_size, unsigned int block_size);
struct block_linked_list_descriptor *bll_descriptor(struct block_linked_list *bll);
void *bll_get(struct block_linked_list_descriptor *bll, int index, int *remaining_slots);
void *bll_extend(struct block_linked_list_descriptor *bll);

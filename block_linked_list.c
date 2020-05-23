struct block_linked_list_node {
  struct block_linked_list_node *next;
  void *ary;
};

struct block_linked_list {
  int block_size;
  struct block_linked_list_node *head;
};

struct block_linked_list_descriptor {
  struct block_linked_list *bll;
  int last_seen_node_index;
  struct block_linked_list_node *last_seen_node;
};

struct block_linked_list *bll_init(unsigned int data_size, unsigned int block_size) {
  struct block_linked_list *bll = malloc(sizeof(struct block_linked_list));
  bll->block_size = block_size;
  bll->data_size = data_size;
  bll->head = malloc(sizeof(struct block_linked_list_node));
  bll->head->next = NULL;
  bll->head->ary = malloc(bll->block_size * bll->data_size);
  return bll;
}

struct block_linked_list_descriptor *bll_descriptor(struct block_linked_list *bll) {
  struct block_linked_list_descriptor *bd = malloc(sizeof(struct block_linked_list_descriptor));
  bd->bll = bll;
  bd->last_seen_node_index = 0;
  bd->last_seen_node = bll->head;
}

void *bll_get(struct block_linked_list_descriptor *bll, int index, int *remaining_slots) {

}
void *bll_extend(struct block_linked_list_descriptor *bll);

struct predis_typed_data {
  struct type_ht_raw *type;
  void *data;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct predis_arg {
  bool needs_initialization;
  struct predis_typed_data *data;
  void *ht_value; // This is a pointer to a spot in a ht_node. It will act a
                  // lot like a void** rather than a void*
};

#pragma GCC diagnostic pop

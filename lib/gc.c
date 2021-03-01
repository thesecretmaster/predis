#include "gc.h"
#define HT_ITERABLE
#include "hashtable.h"
#undef HT_ITERABLE
#include <stdbool.h>
#include <stdlib.h>
#include <sched.h>
#include <stdint.h>
#include <pthread.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct gc_user {
  struct gc_working_set * volatile working_set;
  struct gc_user * volatile next;
  volatile bool pending;
};

struct gc_free_list_elem {
  void *ptr;
  gc_free_func free_func;
  volatile int used;
  // struct gc_free_list_elem * volatile next;
};

struct gc_global_state {
  struct gc_user * volatile users;
  struct ht_table * volatile free_list;
  volatile int used_ctr;
};

#pragma GCC diagnostic pop

static pthread_mutex_t gc_run_lock;
static volatile struct gc_global_state gc_state = (struct gc_global_state){.users = NULL, .free_list = NULL, .used_ctr = 0};

static void gc_ht_free_func(void *_ptr) {
  struct gc_free_list_elem *ptr = _ptr;
  free(ptr);
}

void gc_initialize() {
  pthread_mutex_init(&gc_run_lock, NULL);
  struct ht_table *table = ht_init(false);
  struct ht_table *nul = NULL;
  if (!__atomic_compare_exchange_n(&gc_state.free_list, &nul, table, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
    ht_free(table, gc_ht_free_func);
}

void gc_cleanup() {
  ht_free(gc_state.free_list, NULL);
}

struct gc_user *gc_register_user() {
  struct gc_user *user = malloc(sizeof(struct gc_user));
  user->working_set = NULL;
  user->pending = false;
  struct gc_user *old_user;
  do {
    old_user = gc_state.users;
    user->next = old_user;
  } while (!__atomic_compare_exchange_n(&gc_state.users, &old_user, user, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
  return user;
}

void gc_lock(struct gc_user *usr) {
  __atomic_store_n(&usr->pending, true, __ATOMIC_SEQ_CST);
}

// So working set can be a pointer to a stack var
void gc_commit(struct gc_user *usr, struct gc_working_set *working_set) {
  __atomic_store_n(&usr->working_set, working_set, __ATOMIC_SEQ_CST);
  __atomic_store_n(&usr->pending, false, __ATOMIC_SEQ_CST);
}

void gc_clear(struct gc_user *usr) {
  struct gc_working_set *working_set;
  retry:
  working_set = __atomic_load_n(&usr->working_set, __ATOMIC_SEQ_CST);
  // This if statement needs to be redesigned
  if (((uintptr_t)working_set & (uintptr_t)0x1) == 1) {
    if (!__atomic_compare_exchange_n(&usr->working_set, &working_set, NULL, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)/* CAS working set to NULL */)
      goto retry;
  } else {
    if (!__atomic_compare_exchange_n(&usr->working_set, &working_set, NULL, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
      goto retry;
    free(usr->working_set);
  }
}

void gc_free(void *ptr, gc_free_func free_func) {
  struct gc_free_list_elem *fl_elem = malloc(sizeof(struct gc_free_list_elem));
  fl_elem->ptr = ptr;
  fl_elem->free_func = free_func;
  fl_elem->used = gc_state.used_ctr;
  char *cptr = (char*)&ptr;
  ht_store(gc_state.free_list, cptr, sizeof(ptr), (void**)&fl_elem, NULL);
  // struct gc_free_list_elem *head;
  // do {
  //   head = gc_state.free_list;
  //   fl_elem->next = head;
  // } while (!__atomic_compare_exchange_n(&gc_state.free_list, &head, fl_elem, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
}

void gc_run() {
  pthread_mutex_lock(&gc_run_lock);
  // struct gc_free_list_elem *free_list_tail = __atomic_exchange_n(&gc_state.free_list, NULL, __ATOMIC_SEQ_CST);
  struct gc_user *user = __atomic_load_n(&gc_state.users, __ATOMIC_SEQ_CST);
  struct gc_working_set *working_set;
  struct gc_working_set *new_working_set;
  int ctr = __atomic_fetch_add(&gc_state.used_ctr, 1, __ATOMIC_SEQ_CST);
  char *cptr;
  struct ht_iterable *n;
  struct gc_free_list_elem *elem;
  while (user != NULL) {
    while (__atomic_load_n(&user->pending, __ATOMIC_SEQ_CST)) {
      sched_yield();
    }
    working_set = __atomic_load_n(&user->working_set, __ATOMIC_SEQ_CST);
    do {
      if (working_set == NULL)
        break;
      new_working_set = (struct gc_working_set*)((uintptr_t)working_set | (uintptr_t)0x1);
    } while (!__atomic_compare_exchange_n(&user->working_set, &working_set, new_working_set, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    if (working_set != NULL) {
      // test all things in the working set against the free list
      for (unsigned i = 0; i < working_set->length; i++) {
        if (working_set->members[i] == NULL)
          continue;
        cptr = (char*)&working_set->members[i];
        if (ht_find(gc_state.free_list, cptr, sizeof(working_set->members[i]), (void**)&elem, NULL) == HT_GOOD) {
          __atomic_store_n(&elem->used, ctr, __ATOMIC_SEQ_CST);
        }
      }
      // then try to restore the working set
      if (!__atomic_compare_exchange_n(&user->working_set, &new_working_set, working_set, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        free(working_set);
      }
    }
    user = user->next;
  }
  // Now try and free all the elements in the ht where elem->used == ctr
  n = ht_next(ht_head(gc_state.free_list));
  while (n != NULL) {
    elem = ht_value(n);
    if (elem->used < ctr) {
      cptr = (char*)&elem->ptr;
      if (ht_del(gc_state.free_list, cptr, sizeof(elem->ptr), NULL) == HT_GOOD) {
        elem->free_func(elem->ptr);
        free(elem);
      }
    }
    n = ht_next(n);
  }
  pthread_mutex_unlock(&gc_run_lock);
}

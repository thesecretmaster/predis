/*

Ok, invarients:

1. Anything on the free list is unreachable from the hashtable.

So this means that the GC thread should 1) pop off the free list.
At this point no new threads can get references to anything on the free list
The only things that can still have references are a) threads running a command
that is actively using a ref (in which case it'll be in the gc list for the thread)
or b) a thread has obtained a ref but hasn't finished command phase yet (in which
case it'll have the pending flag set, then after that it'll be in the threads gc list)

So, we'll keep a list of threads, where each thread has a pending flag and a list
of ptrs + free_func ptrs.
Threads will register themselves and keep a ref to themselves
Threads will mark themselves pending when they start fetching then commit to the
gc list once they finish and unmark pending (internal)
Once they finish running a command they'll be able to clear their gc list.

The GC function will work by first popping off the free list. At this point,
any thread with a ref is either a) in a pending state and will have it in the GC
list after leaving pending state or b) has it in GC list. So, we'll go through thread
by thread and if the thread is pending, we'll spin until it's not pending anymore,
then we'll check if each element in its gc list is in the free list. Any elements in
the gc list and the free list will get returned to the main free list.
Once we've gone through all the threads we'll free everything that remains on the
free list.

So we're actually going to use a hashtable as a wrapper to the free list, so that
we can go through each working working list in O(working-list-length) time rather
than O(working-list-length * free-list-length) time. We also need to have a way
for each thread to notify the gc_runner if it's working set is cleared. After that
happens, the gc_runner can skip checking the rest of that threads working set.

*/

#ifndef H_GC
#define H_GC

typedef void (*gc_free_func)(void*);
struct gc_user;
struct gc_working_set {
  unsigned long length;
  void *members[]; // "FAM" or flexible array member, can be any length
};

struct gc_user *gc_register_user(void);
void gc_lock(struct gc_user*);
void gc_commit(struct gc_user*, struct gc_working_set *working_set);
void gc_clear(struct gc_user*);
void gc_run(void); // This probably shouldn't be run concurrently because
                   // it will spuriously fail to free things
                   // Also because ht_del now adds things to the GC list
                   // it will legit break stuff wrt reading freed memory
                   // So I put a mutex on it
void gc_free(void *ptr, gc_free_func free_func);
void gc_initialize(void); // THIS MUST BE CALLED BEFORE ANYTHING IS FREED
void gc_cleanup(void);

#endif

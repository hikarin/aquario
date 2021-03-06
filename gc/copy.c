#include "base.h"
#include <string.h>

struct _copy_header
{
  int obj_size;
  Cell forwarding;
};
typedef struct _copy_header copy_header;

static void gc_start_copy();
static inline void *gc_malloc_copy(size_t size);
static void gc_term_copy();

static void *copy_object(Cell obj);
static void copy_and_update(Cell *objp);

#define IS_ALLOCATABLE(size) (top + sizeof(copy_header) + (size) < from_space + heap_size / 2)
#define GET_OBJECT_SIZE(obj) (((copy_header *)(obj)-1)->obj_size)

#define FORWARDING(obj) (((copy_header *)(obj)-1)->forwarding)
#define IS_COPIED(obj) (FORWARDING(obj) != (obj) || !(from_space <= (char *)(obj) && (char *)(obj) < from_space + heap_size / 2))

static char *from_space = NULL;
static char *to_space = NULL;
static char *top = NULL;

static int heap_size = 0;

void *copy_object(Cell obj)
{
  Cell new_cell;
  long size;

  if (obj == NULL)
  {
    return NULL;
  }

  if (IS_COPIED(obj))
  {
    return FORWARDING(obj);
  }
  copy_header *new_header = (copy_header *)top;
  copy_header *old_header = ((copy_header *)obj) - 1;
  size = GET_OBJECT_SIZE(obj);
  memcpy(new_header, old_header, size);
  top += size;

  new_cell = (Cell)(((copy_header *)new_header) + 1);
  FORWARDING(obj) = new_cell;
  FORWARDING(new_cell) = new_cell;

  return new_cell;
}

void copy_and_update(Cell *objp)
{
  *objp = copy_object(*objp);
}

//Initialization.
void gc_init_copy(aq_gc_info *gc_info)
{
  heap_size = get_heap_size();

  from_space = aq_heap;
  to_space = aq_heap + heap_size / 2;
  top = from_space;

  gc_info->gc_malloc = gc_malloc_copy;
  gc_info->gc_start = gc_start_copy;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr = NULL;
  gc_info->gc_memcpy = NULL;
  gc_info->gc_term = gc_term_copy;
}

//Allocation.
void *gc_malloc_copy(size_t size)
{
  if (g_GC_stress || !IS_ALLOCATABLE(size))
  {
    gc_start();
    if (!IS_ALLOCATABLE(size))
    {
      heap_exhausted_error();
    }
  }
  copy_header *new_header = (copy_header *)top;
  Cell ret = (Cell)(new_header + 1);
  int allocate_size = (sizeof(copy_header) + size + 3) / 4 * 4;
  top += allocate_size;
  FORWARDING(ret) = ret;
  new_header->obj_size = allocate_size;
  return ret;
}

//Start Garbage Collection.
void gc_start_copy()
{
  top = to_space;

  //Copy all objects that are reachable from roots.
  trace_roots(copy_and_update);

  //Trace all objects that are in to space but not scanned.
  char *scanned = to_space;
  while (scanned < top)
  {
    Cell cell = (Cell)(((copy_header *)scanned) + 1);
    trace_object(cell, copy_and_update);
    scanned += GET_OBJECT_SIZE(cell);
  }

  //swap from space and to space.
  void *tmp = from_space;
  from_space = to_space;
  to_space = tmp;
}

//term.
void gc_term_copy() {}

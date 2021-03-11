#include "base.h"
#include <string.h>

struct _generational_gc_header
{
  Cell forwarding;
  int obj_size;
  int flags;
};
typedef struct _generational_gc_header generational_gc_header;

#define TENURING_THRESHOLD (15)
#define NERSARY_SIZE_RATIO (5)

#define MASK_OBJ_AGE (0x000000FF)
#define MASK_REMEMBERED_BIT (1 << 8)
#define MASK_TENURED_BIT (1 << 9)

#define OBJ_HEADER(obj) ((generational_gc_header *)(obj)-1)

#define IS_TENURED(obj) (OBJ_FLAGS(obj) & MASK_TENURED_BIT)
#define SET_TENURED(obj) (OBJ_FLAGS(obj) |= MASK_TENURED_BIT)
#define IS_NERSARY(obj) (!IS_TENURED(obj))

#define OBJ_FLAGS(obj) ((OBJ_HEADER(obj))->flags)

#define IS_REMEMBERED(obj) (OBJ_FLAGS(obj) & MASK_REMEMBERED_BIT)
#define SET_REMEMBERED(obj) (OBJ_FLAGS(obj) |= MASK_REMEMBERED_BIT)
#define CLEAR_REMEMBERED(obj) (OBJ_FLAGS(obj) &= ~MASK_REMEMBERED_BIT)

#define AGE(obj) (OBJ_FLAGS(obj) & MASK_OBJ_AGE)
#define IS_OLD(obj) (AGE(obj) >= TENURING_THRESHOLD)
#define INC_AGE(obj) (OBJ_FLAGS(obj)++)

//mark table: a bit per WORD
#define BIT_WIDTH (32)
static int *nersary_mark_tbl = NULL;
static int *tenured_mark_tbl = NULL;

#define IS_MARKED_TENURED(obj) (tenured_mark_tbl[(((char *)(obj)-tenured_space) / BIT_WIDTH)] & (1 << (((char *)(obj)-tenured_space) % BIT_WIDTH)))
#define IS_MARKED_NERSARY(obj) (nersary_mark_tbl[(((char *)(obj)-from_space) / BIT_WIDTH)] & (1 << (((char *)(obj)-from_space) % BIT_WIDTH)))
#define IS_MARKED(obj) (IS_TENURED(obj) ? IS_MARKED_TENURED(obj) : IS_MARKED_NERSARY(obj))

#define SET_MARK_TENURED(obj) (tenured_mark_tbl[(((char *)(obj)-tenured_space) / BIT_WIDTH)] |= (1 << (((char *)(obj)-tenured_space) % BIT_WIDTH)))
#define SET_MARK_NERSARY(obj) (nersary_mark_tbl[(((char *)(obj)-from_space) / BIT_WIDTH)] |= (1 << (((char *)(obj)-from_space) % BIT_WIDTH)))
#define SET_MARK(obj) (IS_TENURED(obj) ? SET_MARK_TENURED(obj) : SET_MARK_NERSARY(obj))

static void gc_start_generational();
static void minor_gc();
static void major_gc();

static void *gc_malloc_generational(size_t size);
static void gc_term_generational();

static void *copy_object(Cell obj);
static void copy_and_update(Cell *objp);
static aq_bool is_nersary_obj(Cell *objp);

//nersary space.
static char *from_space = NULL;
static char *to_space = NULL;
static char *nersary_top = NULL;

//tenured space.
static char *tenured_space = NULL;
static char *tenured_top = NULL;

//remembered set.
#define REMEMBERED_SET_SIZE 100
static Cell *remembered_set = NULL;
static int remembered_set_top = 0;
static void add_remembered_set(Cell obj);
static void clean_remembered_set();
static void gc_write_barrier_generational(Cell obj, Cell *cellp, Cell newcell);

//size of each heap.
static int nersary_size = 0;
static int nersary_heap_size = 0;
static int nersary_tbl_size = 0;
static int tenured_size = 0;
static int tenured_heap_size = 0;
static int tenured_tbl_size = 0;

#define IS_ALLOCATABLE_NERSARY(size) (nersary_top + sizeof(generational_gc_header) + (size) < from_space + nersary_heap_size)
#define GET_OBJECT_SIZE(obj) (((generational_gc_header *)(obj)-1)->obj_size)

#define FORWARDING(obj) (((generational_gc_header *)(obj)-1)->forwarding)

#define IS_COPIED(obj) (FORWARDING(obj) != (obj))
#define IS_ALLOCATABLE_TENURED() (tenured_top + nersary_heap_size < tenured_space + tenured_heap_size)

#define MARK_STACK_SIZE 1000
static int mark_stack_top;
static Cell mark_stack[MARK_STACK_SIZE];

static void mark_object(Cell *objp);
static void move_object(Cell obj);
static void update_forwarding(Cell *objp);
static void calc_new_address();
static void update_pointer();
static void slide();
static void mark();
static void compact();

//Initialization.
void gc_init_generational(aq_gc_info *gc_info)
{
  int size_int = sizeof(int);
  int rest_size = get_heap_size();
  int byte_count = BIT_WIDTH / size_int;

  //remembered set.
  int remembered_set_size = sizeof(Cell) * REMEMBERED_SET_SIZE;
  remembered_set = (Cell *)aq_heap;
  rest_size -= remembered_set_size;

  //nersary space.
  nersary_size = rest_size / NERSARY_SIZE_RATIO;
  nersary_tbl_size = (nersary_size / 2) / byte_count;
  nersary_tbl_size = (((nersary_tbl_size + size_int - 1) / size_int) * size_int);
  nersary_heap_size = (nersary_size - nersary_tbl_size) / 2;
  from_space = aq_heap + remembered_set_size;
  to_space = from_space + nersary_heap_size;
  nersary_top = from_space;

  nersary_mark_tbl = (int *)(to_space + nersary_heap_size);
  rest_size -= nersary_size;

  //tenured space.
  tenured_size = rest_size;
  tenured_tbl_size = tenured_size / (byte_count + 1);
  tenured_tbl_size = (((tenured_tbl_size + size_int - 1) / size_int) * size_int);
  tenured_heap_size = tenured_size - tenured_tbl_size;
  tenured_space = (char *)nersary_mark_tbl + nersary_tbl_size;
  tenured_top = tenured_space;
  tenured_mark_tbl = (int *)(tenured_space + tenured_heap_size);

  memset(nersary_mark_tbl, 0, nersary_tbl_size);
  memset(tenured_mark_tbl, 0, tenured_tbl_size);

  gc_info->gc_malloc = gc_malloc_generational;
  gc_info->gc_start = gc_start_generational;
  gc_info->gc_term = gc_term_generational;
  gc_info->gc_write_barrier = gc_write_barrier_generational;
  gc_info->gc_init_ptr = NULL;
  gc_info->gc_memcpy = NULL;
}

//Allocation.
void *gc_malloc_generational(size_t size)
{
  if (g_GC_stress || !IS_ALLOCATABLE_NERSARY(size))
  {
    gc_start();
    if (!IS_ALLOCATABLE_NERSARY(size))
    {
      heap_exhausted_error();
    }
  }
  generational_gc_header *new_header = (generational_gc_header *)nersary_top;
  Cell ret = (Cell)(new_header + 1);
  int allocate_size = (size + sizeof(generational_gc_header) + 3) / 4 * 4;
  OBJ_FLAGS(ret) = 0;
  nersary_top += allocate_size;
  FORWARDING(ret) = ret;
  new_header->obj_size = allocate_size;
  return ret;
}

//Start Garbage Collection.
void gc_start_generational()
{
  minor_gc();
  if (!IS_ALLOCATABLE_TENURED())
  {
    major_gc();
    if (!IS_ALLOCATABLE_TENURED())
    {
      heap_exhausted_error();
    }
  }
}

/**** for Minor GC ****/
void minor_gc()
{
  nersary_top = to_space;
  char *prev_nersary_top = nersary_top;
  char *prev_tenured_top = tenured_top;

  //copy all objects that are reachable from roots.
  trace_roots(copy_and_update);

  //scan remembered set.
  int rem_index;
  for (rem_index = 0; rem_index < remembered_set_top; rem_index++)
  {
    Cell cell = remembered_set[rem_index];
    trace_object(cell, copy_and_update);
  }

  while (prev_nersary_top < nersary_top || prev_tenured_top < tenured_top)
  {
    //scan copied objects in nersary space.
    char *scan = prev_nersary_top;
    while (scan < (char *)nersary_top)
    {
      Cell obj = (Cell)((generational_gc_header *)scan + 1);
      int obj_size = GET_OBJECT_SIZE(obj);
      trace_object(obj, copy_and_update);
      scan += obj_size;
    }
    prev_nersary_top = nersary_top;

    //scan copied objects in tenured space.
    scan = prev_tenured_top;
    while (scan < (char *)tenured_top)
    {
      Cell obj = (Cell)((generational_gc_header *)scan + 1);
      int obj_size = GET_OBJECT_SIZE(obj);
      trace_object(obj, copy_and_update);
      if (trace_object_bool(obj, is_nersary_obj) && !IS_REMEMBERED(obj))
      {
        add_remembered_set(obj);
      }
      scan += obj_size;
    }
    prev_tenured_top = tenured_top;
  }

  //swap from space and to space.
  void *tmp = from_space;
  from_space = to_space;
  to_space = tmp;
}

void clean_remembered_set()
{
  int rem_index;
  int remembered_set_top_new = 0;
  for (rem_index = 0; rem_index < remembered_set_top; rem_index++)
  {
    Cell cell = remembered_set[rem_index];
    if (trace_object_bool(cell, is_nersary_obj))
    {
      remembered_set[remembered_set_top_new++] = cell;
      SET_REMEMBERED(cell);
    }
    else
    {
      CLEAR_REMEMBERED(cell);
    }
  }

  remembered_set_top = remembered_set_top_new;
}

inline void add_remembered_set(Cell obj)
{
  if (remembered_set_top >= REMEMBERED_SET_SIZE)
  {
    clean_remembered_set();
    if (remembered_set_top >= REMEMBERED_SET_SIZE)
    {
      AQ_FPRINTF(stderr, "remembered set full\n");
      exit(-1);
    }
  }

  remembered_set[remembered_set_top++] = obj;
  SET_REMEMBERED(obj);
}

void gc_write_barrier_generational(Cell obj, Cell *cellp, Cell newcell)
{
  if (IS_TENURED(obj) && IS_NERSARY(newcell) && !IS_REMEMBERED(obj))
  {
    add_remembered_set(obj);
  }
  *cellp = newcell;
}

void copy_and_update(Cell *objp)
{
  if (IS_COPIED(*objp) || IS_TENURED(*objp))
  {
    *objp = FORWARDING(*objp);
  }
  else
  {
    *objp = copy_object(*objp);
  }
}

aq_bool is_nersary_obj(Cell *objp)
{
  if (IS_NERSARY(*objp))
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

void *copy_object(Cell obj)
{
  Cell new_cell;
  long size;

  size = GET_OBJECT_SIZE(obj);
  generational_gc_header *new_header = NULL;
  INC_AGE(obj);
  if (IS_OLD(obj))
  {
    //Promotion.
    new_header = (generational_gc_header *)tenured_top;
    tenured_top += size;
    SET_TENURED(obj);
  }
  else
  {
    new_header = (generational_gc_header *)nersary_top;
    nersary_top += size;
  }
  generational_gc_header *old_header = ((generational_gc_header *)obj) - 1;
  memcpy(new_header, old_header, size);

  new_cell = (Cell)(((generational_gc_header *)new_header) + 1);
  FORWARDING(obj) = new_cell;
  FORWARDING(new_cell) = new_cell;

  return new_cell;
}

/**** for Major GC ****/
void mark_object(Cell *objp)
{
  Cell obj = *objp;
  if (obj && !IS_MARKED(obj))
  {
    SET_MARK(obj);
    if (mark_stack_top >= MARK_STACK_SIZE)
    {
      AQ_FPRINTF(stderr, "[GC] mark stack overflow\n");
      exit(-1);
    }
    mark_stack[mark_stack_top++] = obj;
  }
}

void move_object(Cell obj)
{
  long size = GET_OBJECT_SIZE(obj);
  generational_gc_header *new_header = (generational_gc_header *)FORWARDING(obj) - 1;
  generational_gc_header *old_header = (generational_gc_header *)obj - 1;

  memcpy(new_header, old_header, size);
  Cell new_cell = (Cell)(((generational_gc_header *)new_header) + 1);

  FORWARDING(new_cell) = new_cell;
  if (IS_REMEMBERED(new_cell))
  {
    add_remembered_set(new_cell);
  }
}

void update_forwarding(Cell *cellp)
{
  if (*cellp)
  {
    *cellp = FORWARDING(*cellp);
  }
}

void calc_new_address()
{
  Cell cell = NULL;
  int obj_size = 0;
  char *scanned = tenured_space;
  char *tenured_new_top = tenured_space;
  while (scanned < tenured_top)
  {
    cell = (Cell)((generational_gc_header *)scanned + 1);
    obj_size = GET_OBJECT_SIZE(cell);
    if (IS_MARKED_TENURED(cell))
    {
      FORWARDING(cell) = (Cell)((generational_gc_header *)tenured_new_top + 1);
      tenured_new_top += obj_size;
    }
    scanned += obj_size;
  }
}

void update_pointer()
{
  trace_roots(update_forwarding);

  char *scanned = NULL;
  Cell cell = NULL;
  int obj_size = 0;
  scanned = tenured_space;
  while (scanned < tenured_top)
  {
    cell = (Cell)((generational_gc_header *)scanned + 1);
    obj_size = GET_OBJECT_SIZE(cell);
    if (IS_MARKED_TENURED(cell))
    {
      trace_object(cell, update_forwarding);
      if (trace_object_bool(cell, is_nersary_obj))
      {
        if (remembered_set_top >= REMEMBERED_SET_SIZE)
        {
          AQ_FPRINTF(stderr, "remembered set full\n");
          exit(-1);
        }
        SET_REMEMBERED(cell);
      }
      else
      {
        CLEAR_REMEMBERED(cell);
      }
    }
    scanned += obj_size;
  }

  scanned = from_space;
  while (scanned < nersary_top)
  {
    cell = (Cell)((generational_gc_header *)scanned + 1);
    obj_size = GET_OBJECT_SIZE(cell);
    trace_object(cell, update_forwarding);
    scanned += obj_size;
  }
}

void slide()
{
  char *scanned = tenured_space;
  Cell cell = NULL;
  int obj_size = 0;

  //scan tenured space.
  char *tenured_new_top = tenured_space;
  while (scanned < tenured_top)
  {
    cell = (Cell)((generational_gc_header *)scanned + 1);
    obj_size = GET_OBJECT_SIZE(cell);
    if (IS_MARKED_TENURED(cell))
    {
      move_object(cell);
      tenured_new_top += obj_size;
    }
    scanned += obj_size;
  }
  tenured_top = tenured_new_top;

  //clear mark bit in young objects.
  memset(nersary_mark_tbl, 0, sizeof(int) * nersary_tbl_size);
  memset(tenured_mark_tbl, 0, sizeof(int) * tenured_tbl_size);
}

//Start Garbage Collection.
void mark()
{
  //mark root objects.
  trace_roots(mark_object);

  Cell obj = NULL;
  while (mark_stack_top > 0)
  {
    obj = mark_stack[--mark_stack_top];
    trace_object(obj, mark_object);
  }
}

void compact()
{
  calc_new_address();
  update_pointer();
  slide();
}

void major_gc()
{
  //initialization.
  mark_stack_top = 0;
  remembered_set_top = 0;

  //mark phase.
  mark();

  //compaction phase.
  compact();
}

void gc_term_generational() {}

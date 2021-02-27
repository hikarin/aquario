#include "base.h"
#include <string.h>

struct _markcompact_gc_header{
  int obj_size;
  Cell forwarding;
  aq_bool mark_bit;
};
typedef struct _markcompact_gc_header markcompact_gc_header;

static void gc_start_markcompact();
static inline void* gc_malloc_markcompact(size_t size);
static void gc_term_markcompact();

static int heap_size = 0;

#define IS_ALLOCATABLE( size ) (top + sizeof( markcompact_gc_header ) + (size) < heap + heap_size )
#define GET_OBJECT_SIZE(obj) (((markcompact_gc_header*)(obj)-1)->obj_size)

#define FORWARDING(obj) (((markcompact_gc_header*)(obj)-1)->forwarding)
#define IS_MARKED(obj) (((markcompact_gc_header*)(obj)-1)->mark_bit)
#define SET_MARK(obj) (((markcompact_gc_header*)(obj)-1)->mark_bit=TRUE)
#define CLEAR_MARK(obj) (((markcompact_gc_header*)(obj)-1)->mark_bit=FALSE)

static char* heap        = NULL;
static char* top         = NULL;
static char* new_top     = NULL;

#define MARK_STACK_SIZE 500
static int mark_stack_top = 0;
static Cell* mark_stack = NULL;

static void mark_object(Cell* objp);
static void move_object(Cell obj);
static void update(Cell* objp);
static void calc_new_address();
static void update_pointer();
static void slide();
static void mark();
static void compact();

void mark_object(Cell* objp)
{
  Cell obj = *objp;
  if( obj && !IS_MARKED(obj) ){
    SET_MARK(obj);
    if(mark_stack_top >= MARK_STACK_SIZE){
      printf("[GC] mark stack overflow.\n");
      exit(-1);
    }
    mark_stack[mark_stack_top++] = obj;
  }
}

void move_object(Cell obj)
{
  long size = GET_OBJECT_SIZE(obj);
  markcompact_gc_header* new_header = ((markcompact_gc_header*)(FORWARDING(obj)))-1;
  markcompact_gc_header* old_header = ((markcompact_gc_header*)obj)-1;
  memcpy(new_header, old_header, size);
  Cell new_cell = (Cell)(((markcompact_gc_header*)new_header)+1);

  FORWARDING(new_cell) = new_cell;
}

//Initialization.
void gc_init_markcompact(aq_gc_info* gc_info)
{
  //mark stack.
  int mark_stack_size = sizeof(Cell) * MARK_STACK_SIZE;
  mark_stack = (Cell*)aq_heap;
  heap_size = get_heap_size() - mark_stack_size;

  //heap.
  heap = aq_heap + mark_stack_size;
  top = heap;

  gc_info->gc_malloc        = gc_malloc_markcompact;
  gc_info->gc_start         = gc_start_markcompact;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_markcompact;
}

//Allocation.
void* gc_malloc_markcompact( size_t size )
{
  if( g_GC_stress || !IS_ALLOCATABLE( size ) ){
    gc_start();
    if( !IS_ALLOCATABLE( size ) ){
      heap_exhausted_error();
    }
  }
  markcompact_gc_header* new_header = (markcompact_gc_header*)top;
  Cell ret = (Cell)(new_header+1);
  int allocate_size = (sizeof(markcompact_gc_header) + size + 3) / 4 * 4;
  top += allocate_size;
  FORWARDING(ret) = ret;
  CLEAR_MARK(ret);
  new_header->obj_size = allocate_size;
  return ret;
}

void update(Cell* cellp)
{
  if( *cellp ){
    *cellp = FORWARDING(*cellp);
  }
}

void calc_new_address()
{
  char* scanned = heap;
  new_top = heap;
  Cell cell = NULL;
  int obj_size = 0;
  while( scanned < top ){
    cell = (Cell)((markcompact_gc_header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      FORWARDING(cell) = (Cell)((markcompact_gc_header*)new_top+1);
      new_top += obj_size;
    }
    scanned += obj_size;
  }
}

void update_pointer()
{
  char* scanned = heap;
  Cell cell = NULL;
  int obj_size = 0;
  trace_roots(update);
  while( scanned < top ){
    cell = (Cell)((markcompact_gc_header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      trace_object(cell, update);
    }
    scanned += obj_size;
  }
}

void slide()
{
  char* scanned = heap;
  Cell cell = NULL;
  int obj_size = 0;
  while( scanned < top ){
    cell = (Cell)((markcompact_gc_header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      CLEAR_MARK(cell);
      move_object(cell);
    }
    scanned += obj_size;
  }
  top = new_top;
}


//Start Garbage Collection.
void mark()
{
  //mark root objects.
  trace_roots(mark_object);

  Cell obj = NULL;
  while(mark_stack_top > 0){
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

void gc_start_markcompact()
{
  //initialization.
  mark_stack_top = 0;

  //mark phase.
  mark();

  //compaction phase.
  compact();
}

//term.
void gc_term_markcompact(){}

#include "gc_base.h"
#include "gc_marksweep.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#if defined( _DEBUG )
#include "aquario.h"
#endif //_DEBUG

typedef struct marksweep_gc_header{
  int obj_size;
  Boolean mark_bit;
}MarkSweep_GC_Header;

static void gc_start_marksweep();
static inline void* gc_malloc_marksweep(size_t size);
static void gc_term_marksweep();

static int get_obj_size( size_t size );
#if defined( _DEBUG )
static void marksweep_gc_stack_check(Cell* cellp);
#endif //_DEBUG

#define IS_ALLOCATABLE( size ) (top + sizeof( MarkSweep_GC_Header ) + (size) < heap + HEAP_SIZE )
#define GET_OBJECT_SIZE(obj) (((MarkSweep_GC_Header*)(obj)-1)->obj_size)

#define IS_MARKED(obj)  (((MarkSweep_GC_Header*)(obj)-1)->mark_bit)
#define SET_MARK(obj)   (((MarkSweep_GC_Header*)(obj)-1)->mark_bit=TRUE)
#define CLEAR_MARK(obj) (((MarkSweep_GC_Header*)(obj)-1)->mark_bit=FALSE)

static char* heap        = NULL;
static char* top         = NULL;

#define MARK_STACK_SIZE 1000
static int mark_stack_top;
static Cell mark_stack[MARK_STACK_SIZE];

static void mark_object(Cell* objp);
static void mark();
static void sweep();

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

//Initialization.
void gc_init_marksweep(GC_Init_Info* gc_info)
{
  heap = (char*)aq_malloc(HEAP_SIZE);
  top = heap;

  gc_info->gc_malloc        = gc_malloc_marksweep;
  gc_info->gc_start         = gc_start_marksweep;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_marksweep;
#if defined( _DEBUG )
  gc_info->gc_stack_check   = marksweep_gc_stack_check;
#endif //_DEBUG

  printf("GC: Mark Sweep\n");
}

//Allocation.
void* gc_malloc_marksweep( size_t size )
{
  if( g_GC_stress || !IS_ALLOCATABLE( size ) ){
    gc_start_marksweep();
    if( !IS_ALLOCATABLE( size ) ){
      printf("Heap Exhausted.\n");
      exit(-1);
    }
  }
  MarkSweep_GC_Header* new_header = (MarkSweep_GC_Header*)top;
  Cell ret = (Cell)(new_header+1);
  int allocate_size = (get_obj_size(size) + 3) / 4 * 4;
  top += allocate_size;
  CLEAR_MARK(ret);
  new_header->obj_size = allocate_size;
  return ret;
}

#if defined( _DEBUG )
void marksweep_gc_stack_check(Cell* cellp)
{
  if( !(*cellp) ){
    return;
  }
  if( !(heap <= (char*)(*cellp) && (char*)(*cellp) < heap + HEAP_SIZE ) ){
    printf("[WARNING] cell %p points out of heap\n", *cellp);
  }  
}
#endif //_DEBUG

int get_obj_size( size_t size ){
  return sizeof( MarkSweep_GC_Header ) + size;
}

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

void sweep()
{

}

//Start Garbage Collection.
void gc_start_marksweep()
{
  //initialization.
  mark_stack_top = 0;

  //mark phase.
  mark();

  //sweep phase.
  sweep();
}

//term.
void gc_term_marksweep()
{
  aq_free( heap );

#if defined( _DEBUG )
  printf("used memory: %ld\n", get_total_malloc_size());
#endif
}

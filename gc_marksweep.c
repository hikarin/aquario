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
}MarkSweep_GC_Header;

//mark table: a bit per WORD
#define BIT_WIDTH    32
static int mark_tbl[HEAP_SIZE/BIT_WIDTH+1];

#define IS_MARKED(obj) (mark_tbl[(((char*)(obj)-heap)/BIT_WIDTH )] & (1 << (((char*)(obj)-heap)%BIT_WIDTH)))
#define SET_MARK(obj)  (mark_tbl[(((char*)(obj)-heap)/BIT_WIDTH )] |= (1 << (((char*)(obj)-heap)%BIT_WIDTH)))

static void gc_start_marksweep();
static inline void* gc_malloc_marksweep(size_t size);
static void gc_term_marksweep();

static int get_obj_size( size_t size );

#define GET_OBJECT_SIZE(obj) (((MarkSweep_GC_Header*)(obj)-1)->obj_size)

static char* heap           = NULL;
static Free_Chunk* freelist = NULL;

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
  freelist = (Free_Chunk*)heap;
  freelist->chunk_size = HEAP_SIZE;
  freelist->next       = NULL;

  gc_info->gc_malloc        = gc_malloc_marksweep;
  gc_info->gc_start         = gc_start_marksweep;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_marksweep;

  printf("GC: Mark Sweep\n");
}

//Allocation.
void* gc_malloc_marksweep( size_t size )
{
  int allocate_size = (get_obj_size(size) + 3) / 4 * 4;
  Free_Chunk* chunk = get_free_chunk(&freelist, allocate_size);
  if( !chunk ){
    gc_start_marksweep();
    chunk = get_free_chunk(&freelist, allocate_size);
    if( !chunk ){
      printf("Heap Exhausted.\n");
      exit(-1);
    }
  }
  MarkSweep_GC_Header* new_header = (MarkSweep_GC_Header*)chunk;
  Cell ret = (Cell)(new_header+1);
  new_header->obj_size = allocate_size;

  return ret;
}

int get_obj_size( size_t size ){
  return sizeof( MarkSweep_GC_Header ) + size;
}

void mark()
{
  mark_stack_top = 0;
  memset(mark_tbl, 0, sizeof(mark_tbl));

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
  char* scan = heap;
  size_t chunk_size = 0;
  Free_Chunk* chunk_top = NULL;
  freelist = NULL;
  while(scan < heap + HEAP_SIZE){
    Cell obj = (Cell)((MarkSweep_GC_Header*)scan+1);
    if(IS_MARKED(obj)){
      if( chunk_size > 0){
#if defined( _DEBUG )
	if( !chunk_top ){
	  printf("chunk_top: NULL\n");
	  exit(-1);
	}
#endif
	put_chunk_to_freelist(&freelist, chunk_top, chunk_size);
      }
      size_t obj_size = GET_OBJECT_SIZE(obj);
#if defined( _DEBUG )
      if(obj_size <= 0){
	printf("obj_size: 0\n");
	exit(-1);
      }
#endif
      scan += obj_size;
      chunk_size = 0;
      chunk_top = NULL;
    }else{
      if(!chunk_top){
	chunk_top = (Free_Chunk*)scan;
      }
      scan++;
      chunk_size++;
    }
  }
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

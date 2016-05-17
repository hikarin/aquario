#include "base.h"
#include "marksweep.h"
#include <stdio.h>
#include <stdint.h>

typedef struct marksweep_gc_header{
  int obj_size;
  int seg_index;
}MarkSweep_GC_Header;

//mark table: a bit per WORD
#define BIT_WIDTH          (32)
#define MEMORY_ALIGNMENT   (4)

static Free_Chunk*    freelist = NULL;
static char*          heap = NULL;
static int* mark_tbl       = NULL;
static int mark_tbl_size   = 0;

#define is_marked(obj)   (mark_tbl[(((char*)(GET_HEADER(obj))-heap)/BIT_WIDTH)] & (1 << (((char*)(obj)-heap)%BIT_WIDTH)))
#define set_mark(obj)    (mark_tbl[(((char*)(GET_HEADER(obj))-heap)/BIT_WIDTH)] |= (1 << (((char*)(obj)-heap)%BIT_WIDTH)))
Free_Chunk* get_free_chunk(size_t size);

static void gc_start_marksweep();
static inline void* gc_malloc_marksweep(size_t size);
static void gc_term_marksweep();

#define GET_HEADER(obj)      ((MarkSweep_GC_Header*)obj-1)
#define GET_OBJECT_SIZE(obj) (GET_HEADER(obj)->obj_size)

#define MARK_STACK_SIZE 500
static int mark_stack_top = 0;
static Cell* mark_stack = NULL;

static void mark_object(Cell* objp);
static void mark();
static void sweep();

void mark_object(Cell* objp)
{
  Cell obj = *objp;
  if( obj && !is_marked(obj) ){
    set_mark(obj);
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
  int int_size         = sizeof(int);
  int total_heap_size  = get_heap_size();
  int byte_count       = BIT_WIDTH/int_size;

  //mark stack.
  int mark_stack_size  = sizeof(Cell)*MARK_STACK_SIZE;
  mark_stack           = (Cell*)aq_heap;
  total_heap_size      -= mark_stack_size;

  //mark table.
  mark_tbl_size        = total_heap_size / ( byte_count + 1 );
  mark_tbl_size        = (((mark_tbl_size + 3) / 4) * 4);
  mark_tbl             = (int*)(aq_heap+mark_stack_size);

  //heap.
  int heap_size        = total_heap_size - mark_tbl_size;
  heap                 = (char*)mark_tbl + mark_tbl_size;

  //freelist.
  freelist             = (Free_Chunk*)heap;
  freelist->chunk_size = heap_size;
  freelist->next       = NULL;

  gc_info->gc_malloc        = gc_malloc_marksweep;
  gc_info->gc_start         = gc_start_marksweep;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_marksweep;
}

//Allocation.
void* gc_malloc_marksweep( size_t size )
{
  int allocate_size = (sizeof(MarkSweep_GC_Header) + size + MEMORY_ALIGNMENT-1) / MEMORY_ALIGNMENT * MEMORY_ALIGNMENT;
  if( g_GC_stress ){
    gc_start_marksweep();
  }
  Free_Chunk* chunk = NULL;
  chunk = get_free_chunk(allocate_size);
  if( !chunk ){
    gc_start_marksweep();
    chunk = get_free_chunk(allocate_size);
    if( !chunk ){
      printf("Heap Exhausted.\n");
      exit(-1);
    }
  }
  if(chunk->chunk_size > allocate_size){
    allocate_size = chunk->chunk_size;
  }

  MarkSweep_GC_Header* new_header = (MarkSweep_GC_Header*)chunk;
  Cell ret                 = (Cell)(new_header+1);
  new_header->obj_size     = allocate_size;

  return ret;
}

void mark()
{
  mark_stack_top = 0;
  memset(mark_tbl, 0, sizeof(char)*mark_tbl_size);

  //mark root objects.
  trace_roots(mark_object);

  Cell obj = NULL;
  while(mark_stack_top > 0){
    obj = mark_stack[--mark_stack_top];
    trace_object(obj, mark_object);
  }
}

Free_Chunk* get_free_chunk(size_t size)
{
  Free_Chunk* chunk = aq_get_free_chunk(&freelist, size);
  return chunk;
}

void sweep_bitwise(int* tbl_index, int* start, size_t* chunk_size, Free_Chunk** chunk_top, Free_Chunk** chunk_last)
{
  int i;
  for(i=(*start); i<BIT_WIDTH; i++){
    char* scan = heap + (*tbl_index) * BIT_WIDTH + i;
    Cell obj = (Cell)((MarkSweep_GC_Header*)scan+1);
    if(is_marked(obj)){
      if( *chunk_size > 0){
	//reclaim.
	if( !freelist ){
	  freelist = *chunk_top;
	  freelist->next = NULL;
	  freelist->chunk_size = *chunk_size;
	      
	  (*chunk_last) = freelist;
	}else{
	  if( (char*)(*chunk_last) + (*chunk_last)->chunk_size == (char*)(*chunk_top) ){
	    //coalesce.
	    (*chunk_last)->chunk_size += (*chunk_size);
	    (*chunk_last)->next = NULL;
	  }else{
	    (*chunk_top)->chunk_size = (*chunk_size);
	    (*chunk_top)->next = NULL;
	    (*chunk_last)->next = (*chunk_top);
	    (*chunk_last) = (*chunk_top);
	  }
	}
	(*chunk_size) = 0;
	(*chunk_top)  = NULL;
      }
      size_t obj_size = GET_OBJECT_SIZE(obj);
      if( i+obj_size >= BIT_WIDTH ){
	(*start) = (i+obj_size) % BIT_WIDTH;
	(*tbl_index) += ((i+obj_size) / BIT_WIDTH);
	return;
      }else{
	i += (obj_size-1);
      }
    }
    else{
      if(!(*chunk_top)){
	(*chunk_top) = (Free_Chunk*)scan;
      }
      (*chunk_size)++;
    }
  }

  (*tbl_index)++;
  (*start) = 0;
}

void sweep()
{
  char* scan = heap;
  size_t chunk_size = 0;
  Free_Chunk* chunk_top = NULL;
  Free_Chunk* chunk_last = NULL;
  freelist = NULL;
  int tbl_index = 0;
  int tbl_offset = 0;

  int mark_tbl_count = mark_tbl_size/sizeof(int);
  while(tbl_index < mark_tbl_count){
    if( tbl_offset == 0 && mark_tbl[tbl_index] == 0){
      scan = heap + tbl_index * BIT_WIDTH;
      if(!chunk_top){
	chunk_top = (Free_Chunk*)scan;
      }
      chunk_size += BIT_WIDTH;
      scan += BIT_WIDTH;
      tbl_index++;
    }else{
      sweep_bitwise(&tbl_index, &tbl_offset, &chunk_size, &chunk_top, &chunk_last);
    }
  }
}

//Start Garbage Collection.
void gc_start_marksweep()
{
  mark();
  sweep();
}

//term.
void gc_term_marksweep() {}

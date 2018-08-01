#include "base.h"
#include "marksweep.h"
#include "../aquario.h"
#include <stdio.h>
#include <stdint.h>

typedef struct marksweep_gc_header{
  int obj_size;
  Boolean mark_bit;
}MarkSweep_GC_Header;

//mark table: a bit per WORD
#define BIT_WIDTH          (32)
#define MEMORY_ALIGNMENT   (4)

Free_Chunk* get_free_chunk(size_t size);
static Free_Chunk*		freelist;
static char*			heap;

#define IS_MARKED(obj) (((MarkSweep_GC_Header*)(obj)-1)->mark_bit)
#define SET_MARK(obj) (((MarkSweep_GC_Header*)(obj)-1)->mark_bit=TRUE)
#define CLEAR_MARK(obj) (((MarkSweep_GC_Header*)(obj)-1)->mark_bit=FALSE)

static void gc_start_marksweep();
static inline void* gc_malloc_marksweep(size_t size);
static void gc_term_marksweep();

#define GET_OBJECT_SIZE(obj) (((MarkSweep_GC_Header*)(obj)-1)->obj_size)

#define MARK_STACK_SIZE 500
static int mark_stack_top;
static Cell* mark_stack = NULL;

static void mark_object(Cell* objp);
static void mark();
static void sweep();
int get_obj_size( size_t size );

typedef struct ms_measure
{
  double mark_elapsed_time;
  double sweep_elapsed_time;
}MS_Measure;

static MS_Measure measure;
static void printMeasureMS();

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

    increase_live_object(GET_OBJECT_SIZE(obj), 1);
  }
}

//Initialization.
void gc_init_marksweep(GC_Init_Info* gc_info)
{
  //mark stack.
  int mark_stack_size = sizeof(Cell) * MARK_STACK_SIZE;
  mark_stack = (Cell*)aq_heap;
  
  //heap.
  heap = aq_heap + mark_stack_size;

  //freelist.
  freelist = (Free_Chunk*)heap;
  freelist->chunk_size = get_heap_size() - mark_stack_size;
  freelist->next = NULL;
  
  gc_info->gc_malloc        = gc_malloc_marksweep;
  gc_info->gc_start         = gc_start_marksweep;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_marksweep;

  gc_info->printMeasure     = printMeasureMS;
  measure.mark_elapsed_time  = 0.0;
  measure.sweep_elapsed_time = 0.0;
}

//Allocation.
void* gc_malloc_marksweep( size_t size )
{
  int allocate_size = (get_obj_size(size) + MEMORY_ALIGNMENT-1) / MEMORY_ALIGNMENT * MEMORY_ALIGNMENT;
  if( g_GC_stress ){
    gc_start();
  }
  Free_Chunk* chunk = NULL;

  chunk = get_free_chunk(allocate_size);
  if( !chunk ){
    gc_start();
    chunk = get_free_chunk(allocate_size);
    if( !chunk ){
      heap_exhausted_error();
    }
  }
  if(chunk->chunk_size > allocate_size){
    allocate_size = chunk->chunk_size;
  }

  MarkSweep_GC_Header* new_header = (MarkSweep_GC_Header*)chunk;
  Cell ret                 = (Cell)(new_header+1);
  new_header->obj_size     = allocate_size;
  new_header->mark_bit     = FALSE;
  
  return ret;
}

void mark()
{
  mark_stack_top = 0;

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
  return aq_get_free_chunk(&freelist, size);
}

int get_obj_size( size_t size ){
  return sizeof( MarkSweep_GC_Header ) + size;
}

void sweep()
{
  char* scan            = heap;
  char* scan_end        = aq_heap + get_heap_size();
  Free_Chunk* chunk_top = NULL;
  
  while(scan < scan_end){
    Cell obj = (Cell)((MarkSweep_GC_Header*)scan+1);
    size_t obj_size = GET_OBJECT_SIZE(obj);
    
    if(!IS_MARKED(obj)){
      if(chunk_top == NULL) {
	chunk_top = (Free_Chunk*)scan;
	chunk_top->next = NULL;
	chunk_top->chunk_size = obj_size;
	freelist = chunk_top;
      } else if( (char*)chunk_top + chunk_top->chunk_size == scan ) {
	chunk_top->chunk_size += obj_size;
      } else {
	chunk_top->next = (Free_Chunk*)scan;
 	chunk_top = chunk_top->next;
	chunk_top->chunk_size = obj_size;
	chunk_top->next = NULL;
      }
    }else{
      CLEAR_MARK(obj);
      get_measure_info()->live_object_count++;
    }
    scan += obj_size;
  }
}

void printMeasureMS()
{
  AQ_PRINTF("------------------------------\n");
  AQ_PRINTF("mark elapsed time:  %.8f\n", measure.mark_elapsed_time);
  AQ_PRINTF("sweep elapsed time: %.8f\n", measure.sweep_elapsed_time);
}

//Start Garbage Collection.
void gc_start_marksweep()
{
  struct rusage usage;
  struct timeval ut1;
  struct timeval ut2;
  
  getrusage(RUSAGE_SELF, &usage );
  ut1 = usage.ru_utime;

  get_measure_info()->live_object_count = 0;
  mark();
  
  getrusage(RUSAGE_SELF, &usage );
  ut2 = usage.ru_utime;
  measure.mark_elapsed_time += (ut2.tv_sec - ut1.tv_sec)+(double)(ut2.tv_usec-ut1.tv_usec)*1e-6;

  getrusage(RUSAGE_SELF, &usage );
  ut1 = usage.ru_utime;
  
  sweep();
  
  getrusage(RUSAGE_SELF, &usage );
  ut2 = usage.ru_utime;
  measure.sweep_elapsed_time += (ut2.tv_sec - ut1.tv_sec)+(double)(ut2.tv_usec-ut1.tv_usec)*1e-6;  
}

//term.
void gc_term_marksweep(){}

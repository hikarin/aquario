#include "base.h"

struct _marksweep_gc_header{
  int obj_size;
  aq_bool mark_bit;
};
typedef struct _marksweep_gc_header marksweep_gc_header;

//mark table: a bit per WORD
#define BIT_WIDTH          (32)
#define MEMORY_ALIGNMENT   (4)

free_chunk* get_free_chunk(size_t size);
static free_chunk*		freelist;
static char*			heap;

#define IS_MARKED(obj) (((marksweep_gc_header*)(obj)-1)->mark_bit)
#define SET_MARK(obj) (((marksweep_gc_header*)(obj)-1)->mark_bit=TRUE)
#define CLEAR_MARK(obj) (((marksweep_gc_header*)(obj)-1)->mark_bit=FALSE)

static void gc_start_marksweep();
static inline void* gc_malloc_marksweep(size_t size);
static void gc_term_marksweep();

#define GET_OBJECT_SIZE(obj) (((marksweep_gc_header*)(obj)-1)->obj_size)

#define MARK_STACK_SIZE 500
static int mark_stack_top;
static Cell* mark_stack = NULL;

static void mark_object(Cell* objp);
static void mark();
static void sweep();
int get_obj_size( size_t size );

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
void gc_init_marksweep(aq_gc_info* gc_info)
{
  //mark stack.
  int mark_stack_size = sizeof(Cell) * MARK_STACK_SIZE;
  mark_stack = (Cell*)aq_heap;
  
  //heap.
  heap = aq_heap + mark_stack_size;

  //freelist.
  freelist = (free_chunk*)heap;
  freelist->chunk_size = get_heap_size() - mark_stack_size;
  freelist->next = NULL;
  
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
  int allocate_size = (get_obj_size(size) + MEMORY_ALIGNMENT-1) / MEMORY_ALIGNMENT * MEMORY_ALIGNMENT;
  if( g_GC_stress ) {
    if( freelist && freelist->chunk_size < get_heap_size() - sizeof(Cell) * MARK_STACK_SIZE ) {
      gc_start();
    }
  }
  free_chunk* chunk = NULL;

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

  marksweep_gc_header* new_header = (marksweep_gc_header*)chunk;
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

free_chunk* get_free_chunk(size_t size)
{
  return aq_get_free_chunk(&freelist, size);
}

int get_obj_size( size_t size ){
  return sizeof( marksweep_gc_header ) + size;
}

void sweep()
{
  char* scan            = heap;
  char* scan_end        = aq_heap + get_heap_size();
  free_chunk* chunk_top = NULL;
  
  while(scan < scan_end){
    Cell obj = (Cell)((marksweep_gc_header*)scan+1);
    size_t obj_size = GET_OBJECT_SIZE(obj);
    
    if(!IS_MARKED(obj)){
      if(chunk_top == NULL) {
	chunk_top = (free_chunk*)scan;
	chunk_top->next = NULL;
	chunk_top->chunk_size = obj_size;
	freelist = chunk_top;
      } else if( (char*)chunk_top + chunk_top->chunk_size == scan ) {
	chunk_top->chunk_size += obj_size;
      } else {
	chunk_top->next = (free_chunk*)scan;
 	chunk_top = chunk_top->next;
	chunk_top->chunk_size = obj_size;
	chunk_top->next = NULL;
      }
    }else{
      CLEAR_MARK(obj);
    }
    scan += obj_size;
  }
}

//Start Garbage Collection.
void gc_start_marksweep()
{
  mark();
  sweep();
}

//term.
void gc_term_marksweep(){}

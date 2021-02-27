#include "base.h"
#include <string.h>

typedef struct reference_count_header{
  int obj_size;
  int ref_cnt;
}Reference_Count_Header;

static void gc_start_reference_count();
static inline void* gc_malloc_reference_count(size_t size);
static void gc_write_barrier_reference_count(Cell obj, Cell* cellp, Cell newcell);
static void gc_write_barrier_root_reference_count(Cell* cellp, Cell newcell);
static void gc_init_ptr_reference_count(Cell* cellp, Cell newcell);
static void gc_memcpy_reference_count(char* dst, char* src, size_t size);
static void reclaim_obj( Cell obj );
static void increment_count(Cell* objp);
static void decrement_count(Cell* objp);
static void gc_term_reference_count();

static char* heap           = NULL;
static Free_Chunk* freelist = NULL;

static void push_reference_count(Cell c);
static Cell pop_reference_count();

#define GET_OBJECT_SIZE(obj) (((Reference_Count_Header*)(obj)-1)->obj_size)

#define REF_CNT(obj) (((Reference_Count_Header*)(obj)-1)->ref_cnt)
#define INC_REF_CNT(obj) (REF_CNT(obj)++);
#define DEC_REF_CNT(obj) (REF_CNT(obj)--);

//Initialization.
void gc_init_reference_count(aq_gc_info* gc_info)
{
  heap     = (char*)aq_heap;
  freelist = (Free_Chunk*)heap;
  freelist->chunk_size = get_heap_size();
  freelist->next       = NULL;

  gc_info->gc_malloc             = gc_malloc_reference_count;
  gc_info->gc_start              = gc_start_reference_count;
  gc_info->gc_write_barrier      = gc_write_barrier_reference_count;
  gc_info->gc_write_barrier_root = gc_write_barrier_root_reference_count;
  gc_info->gc_init_ptr           = gc_init_ptr_reference_count;
  gc_info->gc_memcpy             = gc_memcpy_reference_count;
  gc_info->gc_term               = gc_term_reference_count;
  gc_info->gc_push_arg           = push_reference_count;
  gc_info->gc_pop_arg            = pop_reference_count;
}

void push_reference_count(Cell c)
{
  increment_count(&c);
  push_arg_default(c);
}

Cell pop_reference_count()
{
  Cell c = pop_arg_default();
  decrement_count(&c);
  return c;
}

//Allocation.
void* gc_malloc_reference_count( size_t size )
{
  size += sizeof(Reference_Count_Header);
  int allocate_size = (sizeof( Reference_Count_Header ) + size + 3 ) / 4 * 4;
  Free_Chunk* chunk = aq_get_free_chunk( &freelist, allocate_size );
  if( !chunk ){
    heap_exhausted_error();
  }else if(chunk->chunk_size > allocate_size){
    //size of chunk might be larger than it is required.
    allocate_size = chunk->chunk_size;
  }
  Reference_Count_Header* new_header = (Reference_Count_Header*)chunk;
  Cell ret = (Cell)(new_header+1);
  GET_OBJECT_SIZE(ret) = allocate_size;
  REF_CNT(ret)         = 0;

  return ret;
}

void reclaim_obj( Cell obj )
{
  REF_CNT(obj) = -1;
  trace_object( obj, decrement_count );
  
  Free_Chunk* obj_top = (Free_Chunk*)((Reference_Count_Header*)obj - 1);
  size_t obj_size = GET_OBJECT_SIZE( obj );
  put_chunk_to_freelist(&freelist, obj_top, obj_size);
}

//Start Garbage Collection.
void gc_start_reference_count()
{
  //Nothing.
}

//For compatibility to trace_object(), this function receives a pointer to Cell.
void increment_count(Cell* objp)
{
  if( !CELL_P(*objp) ){
    return;
  }
  Cell obj = *objp;
  if( obj ){
    INC_REF_CNT( obj )
  }
}

void decrement_count(Cell* objp)
{
  if( !CELL_P(*objp) ){
    return;
  }
  Cell obj = *objp;
  if( obj ){
    DEC_REF_CNT( obj );
    if( REF_CNT( obj ) <= 0 ){
      reclaim_obj(obj);
    }
  }
}

//Write Barrier.
void gc_write_barrier_reference_count(Cell obj, Cell* cellp, Cell newcell)
{
  gc_write_barrier_root_reference_count(cellp, newcell);
}

void gc_write_barrier_root_reference_count(Cell* cellp, Cell newcell)
{
  increment_count( &newcell );
  decrement_count( cellp );
  *cellp = newcell;
}

//Init Pointer.
void gc_init_ptr_reference_count(Cell* cellp, Cell newcell)
{
  increment_count(&newcell);
  *cellp = newcell;
}

//memcpy.
void gc_memcpy_reference_count(char* dst, char* src, size_t size)
{
  memcpy(dst, src, size);

  trace_object( (Cell)dst, increment_count );
}

//term.
void gc_term_reference_count(){}

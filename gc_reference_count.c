#include "gc_base.h"
#include "gc_reference_count.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "aquario.h"

typedef struct reference_count_header{
  int obj_size;
  int ref_cnt;
}Reference_Count_Header;

struct free_chunk;

typedef struct free_chunk{
  int chunk_size;
  struct free_chunk* next;
}Free_Chunk;

static void gc_start_reference_count();
static inline void* gc_malloc_reference_count(size_t size);
static void gc_write_barrier_reference_count(Cell obj, Cell* cellp, Cell newcell);
static void gc_write_barrier_root_reference_count(Cell* cellp, Cell newcell);
static void gc_init_ptr_reference_count(Cell* cellp, Cell newcell);
static void gc_memcpy_reference_count(char* dst, char* src, size_t size);
static int get_obj_size( size_t size );
static Free_Chunk* get_free_chunk( size_t size );
static void reclaim_obj( Cell obj );
static void increment_count(Cell* objp);
static void decrement_count(Cell* objp);
static void gc_term_reference_count();

#if defined( _DEBUG )
static void reference_count_stack_check( Cell* cell );
#endif

static char* heap           = NULL;
static Free_Chunk* freelist = NULL;

static void push_reference_count(Cell* cellp);
static Cell* pop_reference_count();
static void push_reference_count(Cell* cellp);
static Cell* pop_reference_count();

#define GET_OBJECT_SIZE(obj) (((Reference_Count_Header*)(obj)-1)->obj_size)

#define REF_CNT(obj) (((Reference_Count_Header*)(obj)-1)->ref_cnt)
#define INC_REF_CNT(obj) (REF_CNT(obj)++);
#define DEC_REF_CNT(obj) (REF_CNT(obj)--);

#if defined( _DEBUG )
#define DEBUG_REF_CNT(obj) (((Reference_Count_Header*)(obj)-1)->debug_ref_cnt)
#define DEBUG_INC_REF_CNT(obj) (DEBUG_REF_CNT(obj)++);
#define DEBUG_DEC_REF_CNT(obj) (DEBUG_REF_CNT(obj)--);
#endif

//Initialization.
void gc_init_reference_count(GC_Init_Info* gc_info)
{
  heap     = (char*)aq_malloc(HEAP_SIZE);
  freelist = (Free_Chunk*)heap;
  freelist->chunk_size = HEAP_SIZE;
  freelist->next       = NULL;

  gc_info->gc_malloc        = gc_malloc_reference_count;
  gc_info->gc_start         = gc_start_reference_count;
  gc_info->gc_write_barrier = gc_write_barrier_reference_count;
  gc_info->gc_write_barrier_root = gc_write_barrier_root_reference_count;
  gc_info->gc_init_ptr      = gc_init_ptr_reference_count;
  gc_info->gc_memcpy        = gc_memcpy_reference_count;
  gc_info->gc_term          = gc_term_reference_count;
  gc_info->gc_pushArg       = push_reference_count;
  gc_info->gc_popArg        = pop_reference_count;
#if defined( _DEBUG )
  gc_info->gc_stack_check = reference_count_stack_check;
#endif //_DEBUG

  printf("GC: Reference Counting\n");
}

void push_reference_count(Cell* cellp)
{
  increment_count(cellp);
  pushArg_default(cellp);
}

Cell* pop_reference_count()
{
  Cell* cellp = popArg_default();
  decrement_count(cellp);
  return cellp;
}

//Allocation.
void* gc_malloc_reference_count( size_t size )
{
  int allocate_size = ( get_obj_size(size) + 3 ) / 4 * 4;
  Free_Chunk* chunk = get_free_chunk( allocate_size );
  if( !chunk ){
    printf("Heap Exhausted.\n");
    exit(-1);
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

Free_Chunk* get_free_chunk( size_t size )
{
  //returns a chunk which size is larger than required size.
  Free_Chunk** chunk = &freelist;
  Free_Chunk* ret = NULL;
  while(*chunk){
    if((*chunk)->chunk_size >= size){
      //a chunk found.
      ret = *chunk;
      if((*chunk)->chunk_size >= size + sizeof(Free_Chunk)){
	int chunk_size = (*chunk)->chunk_size - size;
	Free_Chunk* next = (*chunk)->next;
	(*chunk)->chunk_size = size;

	*chunk = (Free_Chunk*)((char*)(*chunk)+size);
	(*chunk)->chunk_size = chunk_size;
	(*chunk)->next = next;
      }else{
	*chunk = (*chunk)->next;
      }
      break;
    }
    chunk = &((*chunk)->next);
  }

  return ret;
}

void reclaim_obj( Cell obj )
{
  REF_CNT(obj) = -1;
  trace_object( obj, decrement_count );
  
  Free_Chunk* obj_top = (Free_Chunk*)((Reference_Count_Header*)obj - 1);
  size_t obj_size = GET_OBJECT_SIZE( obj );

  if(!freelist){
    //No object in freelist.
    freelist             = obj_top;
    freelist->chunk_size = obj_size;
    freelist->next       = NULL;
  }else if( obj_top < freelist ){
    if( (char*)obj_top + obj_size == (char*)freelist ){
      //Coalesce.
      obj_top->next       = freelist->next;
      obj_top->chunk_size = obj_size + freelist->chunk_size;
    }else{
      obj_top->next       = freelist;
      obj_top->chunk_size = obj_size;
    }
    freelist = obj_top;
  }else{
    Free_Chunk* tmp = NULL;
    for( tmp = freelist; tmp->next; tmp = tmp->next ){
      if( (char*)tmp < (char*)obj_top && (char*)obj_top < (char*)tmp->next ){
	//Coalesce.
	if( (char*)tmp + tmp->chunk_size == (char*)obj_top ){
	  if( (char*)obj_top + obj_size == (char*)tmp->next ){
	    //Coalesce with previous and next Free_Chunk.
	    tmp->chunk_size += (obj_size + tmp->next->chunk_size);
	    tmp->next        = tmp->next->next;
	  }else{
	    //Coalesce with previous Free_Chunk.
	    tmp->chunk_size += obj_size;
	  }
	}else if( (char*)obj_top + obj_size == (char*)tmp->next ){
	  //Coalesce with next Free_Chunk.
	  size_t new_size      = tmp->next->chunk_size + obj_size;
	  Free_Chunk* new_next = tmp->next->next;
	  obj_top->chunk_size  = new_size;
	  obj_top->next        = new_next;
	  tmp->next            = obj_top;
	}else{
	  //Just put obj into freelist.
	  obj_top->chunk_size  = obj_size;
	  obj_top->next        = tmp->next;
	  tmp->next            = obj_top;
	}
	return;
      }
    }
    tmp->next           = obj_top;
    obj_top->next       = NULL;
    obj_top->chunk_size = obj_size;
  }
}

#if defined( _DEBUG )
void reference_count_stack_check(Cell* cell)
{
  if( !(heap <= (char*)cell && (char*)cell < heap + HEAP_SIZE ) ){
    printf("[WARNING] cell %p points out of heap\n", cell);
  }
}
#endif //_DEBUG

int get_obj_size( size_t size )
{
  return sizeof( Reference_Count_Header ) + size;
}

//Start Garbage Collection.
void gc_start_reference_count()
{
  //Nothing.
}

//For compatibility to trace_object(), this function receives a pointer to Cell.
void increment_count(Cell* objp)
{
  Cell obj = *objp;
  if( obj ){
    INC_REF_CNT( obj )
  }
}

void decrement_count(Cell* objp)
{
  Cell obj = *objp;
  if( obj ){
#if defined( _DEBUG )
    if( REF_CNT( obj ) <= 0 ){
      printf("REF COUNT: %d, minus\n", REF_CNT(obj));
    }
#endif
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
  if( newcell ){
    INC_REF_CNT(newcell);
  }
  *cellp = newcell;
}

//memcpy.
void gc_memcpy_reference_count(char* dst, char* src, size_t size)
{
  memcpy(dst, src, size);

  trace_object( (Cell)dst, increment_count );
}

//term.
void gc_term_reference_count()
{
  aq_free(heap);
}

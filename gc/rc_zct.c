#include "base.h"
#include <string.h>

struct _rc_zct_header{
  int obj_size;
  int ref_cnt;
  aq_bool in_zct;
};
typedef struct _rc_zct_header rc_zct_header;

static void gc_start_reference_coun();
static inline void* gc_malloc_reference_coun(size_t size);
static void gc_write_barrier_reference_coun(Cell obj, Cell* cellp, Cell newcell);
static void gc_write_barrier_root_reference_coun(Cell* cellp, Cell newcell);
static void gc_init_ptr_reference_coun(Cell* cellp, Cell newcell);
static void gc_memcpy_reference_coun(char* dst, char* src, size_t size);
static void reclaim_obj( Cell obj );
static void increment_count(Cell* objp);
static void decrement_count(Cell* objp);
static void gc_term_reference_coun();

static char* heap           = NULL;
static free_chunk* freelist = NULL;
static int zct_index        = 0;
static void add_zct(Cell c);
#define ZCT_SIZE (10)
static Cell ZCT[ZCT_SIZE];

static void push_reference_coun(Cell c);
static Cell pop_reference_coun();

#define GET_OBJECT_SIZE(obj) (((rc_zct_header*)(obj)-1)->obj_size)

#define REF_CNT(obj) (((rc_zct_header*)(obj)-1)->ref_cnt)
#define INC_REF_CNT(obj) (REF_CNT(obj)++);
#define DEC_REF_CNT(obj) (REF_CNT(obj)--);
#define IN_ZCT(obj) (((rc_zct_header*)(obj)-1)->in_zct)

//Initialization.
void gc_init_rc_zct(aq_gc_info* gc_info)
{
  heap     = (char*)aq_heap;
  freelist = (free_chunk*)heap;
  freelist->chunk_size = get_heap_size();
  freelist->next       = NULL;

  gc_info->gc_malloc             = gc_malloc_reference_coun;
  gc_info->gc_start              = gc_start_reference_coun;
  gc_info->gc_write_barrier      = gc_write_barrier_reference_coun;
  gc_info->gc_write_barrier_root = gc_write_barrier_root_reference_coun;
  gc_info->gc_init_ptr           = gc_init_ptr_reference_coun;
  gc_info->gc_memcpy             = gc_memcpy_reference_coun;
  gc_info->gc_term               = gc_term_reference_coun;
  gc_info->gc_push_arg           = push_reference_coun;
  gc_info->gc_pop_arg            = pop_reference_coun;

  zct_index = 0;
}

void push_reference_coun(Cell c)
{
  push_arg_default(c);
}

Cell pop_reference_coun()
{
  return pop_arg_default();
}

//Allocation.
void* gc_malloc_reference_coun( size_t size )
{
  size += sizeof(rc_zct_header);
  int allocate_size = (sizeof( rc_zct_header ) + size + 3 ) / 4 * 4;
  free_chunk* chunk = aq_get_free_chunk( &freelist, allocate_size );
  if( !chunk ){
      gc_start_reference_coun();    
  }else if(chunk->chunk_size > allocate_size){
    //size of chunk might be larger than it is required.
    allocate_size = chunk->chunk_size;
  }
  rc_zct_header* new_header = (rc_zct_header*)chunk;
  Cell ret = (Cell)(new_header+1);
  GET_OBJECT_SIZE(ret) = allocate_size;
  REF_CNT(ret) = 0;
  IN_ZCT(ret) = FALSE;

  return ret;
}

void reclaim_obj( Cell obj )
{
  REF_CNT(obj) = -1;
  trace_object( obj, decrement_count );
  
  free_chunk* obj_top = (free_chunk*)((rc_zct_header*)obj - 1);
  size_t obj_size = GET_OBJECT_SIZE( obj );
  put_chunk_to_freelist(&freelist, obj_top, obj_size);
}

//Start Garbage Collection.
void gc_start_reference_coun()
{
  trace_roots(increment_count);
  int index = zct_index-1;
  while(index >= 0)
  {
    if(REF_CNT(ZCT[index]) == 0)
    {
      reclaim_obj(ZCT[index]);
      ZCT[index] = ZCT[zct_index--];
      index--;
    }
  }
  trace_roots(decrement_count);
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

void add_zct(Cell obj)
{
  if(!IN_ZCT(obj))
  {
      IN_ZCT(obj) = TRUE;
      ZCT[zct_index++] = obj;
      if(zct_index >= ZCT_SIZE)
      {
	  gc_start_reference_coun();
      }
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
	add_zct(obj);
    }
  }
}

//Write Barrier.
void gc_write_barrier_reference_coun(Cell obj, Cell* cellp, Cell newcell)
{
  increment_count( &newcell );
  decrement_count( cellp );
  *cellp = newcell;
}

void gc_write_barrier_root_reference_coun(Cell* cellp, Cell newcell)
{
    *cellp = newcell;
}

//Init Pointer.
void gc_init_ptr_reference_coun(Cell* cellp, Cell newcell)
{
  increment_count(&newcell);
  *cellp = newcell;
}

//memcpy.
void gc_memcpy_reference_coun(char* dst, char* src, size_t size)
{
  memcpy(dst, src, size);
  trace_object( (Cell)dst, increment_count );
}

//term.
void gc_term_reference_coun(){}

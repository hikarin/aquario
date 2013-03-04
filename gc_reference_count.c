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
static void gc_write_barrier_reference_count(Cell* cellp, Cell newcell);
static void gc_init_ptr_reference_count(Cell* cellp, Cell newcell);
static void gc_memcpy_reference_count(char* dst, char* src, size_t size);
static int get_obj_size( size_t size );
static char* get_free_chunk( size_t size );
static void reclaim_obj( Cell obj );

#if defined( _DEBUG )
static void reference_count_stack_check( Cell cell );
#endif //_DEBUG

static char* heap           = NULL;
static Free_Chunk* freelist = NULL;

#define GET_OBJECT_SIZE(obj) (((Reference_Count_Header*)(obj)-1)->obj_size)

#define REF_CNT(obj) (((Reference_Count_Header*)(obj)-1)->ref_cnt)
#define INC_REF_CNT(obj) (REF_CNT(obj)++)
#define DEC_REF_CNT(obj) (REF_CNT(obj)--)

//Initialization.
void reference_count_init(GC_Init_Info* gc_info){
  printf( "reference count init\n");
  heap     = (char*)malloc(HEAP_SIZE);
  freelist = (Free_Chunk*)heap;
  freelist->chunk_size = HEAP_SIZE;

  gc_info->gc_malloc        = gc_malloc_reference_count;
  gc_info->gc_start         = gc_start_reference_count;
  gc_info->gc_write_barrier = gc_write_barrier_reference_count;
  gc_info->gc_init_ptr      = gc_init_ptr_reference_count;
  gc_info->gc_memcpy        = gc_memcpy_reference_count;
#if defined( _DEBUG )
  gc_info->gc_stack_check = reference_count_stack_check;
#endif //_DEBUG
}

//Allocation.
void* gc_malloc_reference_count( size_t size )
{
  int allocate_size = ( get_obj_size(size) + 3 ) / 4 * 4;
  char* chunk = get_free_chunk( allocate_size );
  if( !chunk ){
    printf("Heap Exhausted.\n ");
    exit(-1);
  }
  Reference_Count_Header* new_header = (Reference_Count_Header*)chunk;
  Cell ret = (Cell)(new_header+1);
  new_header->obj_size = allocate_size;
  new_header->ref_cnt  = 0;
  
  return ret;
}

char* get_free_chunk( size_t size )
{
  Free_Chunk** chunkp = &freelist;
  while( *chunkp ){
    if( (*chunkp)->chunk_size >= size ){
      int old_size = (*chunkp)->chunk_size;
      char* ret = (char*)*chunkp;
      Free_Chunk* next = NULL;
      if( old_size - size >= sizeof( Free_Chunk ) ){
	next             = (Free_Chunk*)((char*)(*chunkp) + size);
	next->chunk_size = old_size - size;
      }else{
	next             = ((Free_Chunk*)ret)->next;
      }
      if( (char*)chunkp < heap || heap + HEAP_SIZE < (char*)chunkp ){
	freelist = next;
      }else{
	((Free_Chunk*)(chunkp + 1) - 1)->next = next;
      }
      return ret;
    }
    chunkp = &((*chunkp)->next);
  }
  return NULL;
}

void reclaim_obj( Cell obj )
{
  Free_Chunk** chunkp = &freelist;
  while( *chunkp ){
    if( (char*)*chunkp == ( (char*)( (Reference_Count_Header*)obj - 1) + GET_OBJECT_SIZE(obj) ) ){
      //Coalescing.
#if defined( _DEBUG )
      printf("Coalesce\n");
#endif //_DEBUG
      //      int size              = (*chunkp)->chunk_size + GET_OBJECT_SIZE(obj);
      //      Free_Chunk* next      = (*chunkp)->next;
      int size         = 0;
      Free_Chunk* next = NULL;
      if( (char*)(*chunkp)->next == ( (char*)( (Reference_Count_Header*)obj - 1) + GET_OBJECT_SIZE(obj) ) ){
	size = (*chunkp)->chunk_size + GET_OBJECT_SIZE(obj) + (*chunkp)->next->chunk_size;
	next = (*chunkp)->next;
      }else{
	size = (*chunkp)->chunk_size + GET_OBJECT_SIZE(obj);
	next = (*chunkp)->next;
      }

      (*chunkp)             = (Free_Chunk*)( (Reference_Count_Header*)obj - 1);
      (*chunkp)->next       = next;
      (*chunkp)->chunk_size = size;
      return;
    }else if( (char*)*chunkp > (char*)( (Reference_Count_Header*)obj - 1 ) + GET_OBJECT_SIZE(obj) ){
      size_t size           = GET_OBJECT_SIZE(obj);
      Free_Chunk* next      = *chunkp;
      *chunkp               = (Free_Chunk*)( (Reference_Count_Header*)obj - 1 );
      (*chunkp)->next       = next;
      (*chunkp)->chunk_size = size;
      return;
    }
    chunkp = &((*chunkp)->next);
  }
#if defined( _DEBUG )
  printf( "--- OMG ------\n");
#endif //_DEBUG
}

#if defined( _DEBUG )
void reference_count_stack_check(Cell cell){
  if( !(heap <= (char*)cell && (char*)cell < heap + HEAP_SIZE ) ){
    printf("[WARNING] cell %p points out of heap\n", cell);
  }
}
#endif //_DEBUG
int get_obj_size( size_t size ){
  return sizeof( Reference_Count_Header ) + size;
}

//Start Garbage Collection.
void gc_start_reference_count(){
#if defined( _DEBUG )
  printf("gc() does nothing.\n");
#endif //_DEBUG
}

//Write Barrier.
void gc_write_barrier_reference_count(Cell* cellp, Cell newcell)
{
  if( newcell ){
    INC_REF_CNT( newcell );
  }
  if( *cellp ){
    DEC_REF_CNT( *cellp );
    if( REF_CNT( *cellp ) <= 0 ){
      reclaim_obj( *cellp );
    }
  }
  *cellp = newcell;
}

//Init Pointer.
void gc_init_ptr_reference_count(Cell* cellp, Cell newcell)
{
  if( newcell ){
    INC_REF_CNT( newcell );
  }
  *cellp = newcell;
}

//memcpy.
void gc_memcpy_reference_count(char* dst, char* src, size_t size)
{
  memcpy(dst, src, size);
  Reference_Count_Header* new_header = (Reference_Count_Header*)dst - 1;
  new_header->obj_size = GET_OBJECT_SIZE(src);
  new_header->ref_cnt  = 0;

  Cell dst_cell = (Cell)dst;
  switch( type(dst_cell) ){
  case T_PAIR:
    if( car(dst_cell) ){
      INC_REF_CNT(car(dst_cell));
    }
    if( cdr(dst_cell) ){
      INC_REF_CNT(cdr(dst_cell));
    }
    break;
 case T_LAMBDA:
   if( lambdaparam(dst_cell) ){
     INC_REF_CNT( lambdaparam(dst_cell) );
   }
   if( lambdaexp(dst_cell) ){
     INC_REF_CNT( lambdaexp(dst_cell) );
   }
   break;
  default:
    break;
  }
}

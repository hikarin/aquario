#include "gc_base.h"
#include "gc_reference_count.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "aquario.h"

typedef struct reference_count_header{
  int obj_size;
  int ref_cnt;
#if defined( _DEBUG )
  Boolean is_visit;
#endif //_DEBUG
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
static void increment_count(Cell* objp);
static void decrement_count(Cell* objp);

#if defined( _DEBUG )
static void reference_count_stack_check( Cell cell );
static void check_reference();
static void check_freelist();
#endif //_DEBUG

static char* heap           = NULL;
static Free_Chunk* freelist = NULL;

#define GET_OBJECT_SIZE(obj) (((Reference_Count_Header*)(obj)-1)->obj_size)

#define REF_CNT(obj) (((Reference_Count_Header*)(obj)-1)->ref_cnt)
#define INC_REF_CNT(obj) (REF_CNT(obj)++)
#define DEC_REF_CNT(obj) (REF_CNT(obj)--)

//Initialization.
void reference_count_init(GC_Init_Info* gc_info)
{
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
  GET_OBJECT_SIZE(ret) = allocate_size;
  REF_CNT(ret)         = 0;
#if defined( _DEBUG )
  //  printf( "   allocated: %p(size: %d) ===>  ", new_header, allocate_size );
  check_freelist();
  check_reference();
  new_header->is_visit = FALSE;
#endif //_DEBUG
  
  return ret;
}

char* get_free_chunk( size_t size )
{
  //returns a chunk which size is larger than required size.
  Free_Chunk** chunkp = &freelist;
  while( *chunkp ){
    if( (*chunkp)->chunk_size >= size ){
      int old_size = (*chunkp)->chunk_size;
      char* ret = (char*)*chunkp;
      Free_Chunk* next = NULL;
      if( old_size - size >= sizeof( Free_Chunk ) ){
	Free_Chunk* new_next = (*chunkp)->next;
	next                 = (Free_Chunk*)(ret + size);
	next->next           = new_next;
	next->chunk_size     = old_size - size;
#if defined( _DEBUG )
	printf( "type: 111");
#endif //_DEBUG
      }else{
#if defined( _DEBUG )
	printf( "type: 000" );
#endif //_DEBUG
	next             = ((Free_Chunk*)ret)->next;
      }
      if( (char*)chunkp < heap || heap + HEAP_SIZE < (char*)chunkp ){
	freelist = next;
#if defined( _DEBUG )
	printf("[get_free_chunk] => freelist: %p(1)\n", freelist->next );
#endif //_DEBUG
      }else{
	((Free_Chunk*)(chunkp + 1) - 1)->next = next;
#if defined( _DEBUG )
	printf( "[get_free_chunk] => freelist: %p(2)\n", freelist->next );
#endif //_DEBUG
      }
      return ret;
    }
    chunkp = &((*chunkp)->next);
  }
  return NULL;
}

void reclaim_obj( Cell obj )
{
  size_t obj_size = GET_OBJECT_SIZE( obj );
  trace_object( obj, decrement_count );
  Reference_Count_Header* header = (Reference_Count_Header*)obj - 1;
  Free_Chunk** chunkp = &freelist;
#if defined( _DEBUG )
  printf( " <reclaim_obj>: %p\n", obj );
#endif //_DBEUG
  while( *chunkp ){
    if( ( (char*)(*chunkp) + (*chunkp)->chunk_size ) == (char*)header ){
      //Coalescing.
#if defined( _DEBUG )
      printf("Coalesce\n");
#endif //_DEBUG

      (*chunkp)->chunk_size += obj_size;
#if defined( _DEBUG )
      check_reference();
      check_freelist();
      printf( "freelist: %p(3)\n", freelist->next);
#endif //_DBEUG
      return;
#if defined( _CUT )
    }else if( (char*)*chunkp > ( (char*)header + obj_size ) ){
#else
    }else if( (char*)*chunkp > (char*)header ){
#endif //_CUT
      Free_Chunk* next      = *chunkp;
      *chunkp               = (Free_Chunk*)header;
      (*chunkp)->next       = next;
      (*chunkp)->chunk_size = obj_size;
      printf("  [reclaimed] header: %p\n", header );
#if defined( _DEBUG )
      check_reference();
      check_freelist();
      printf( "freelist: %p\n", freelist->next);
#endif //_DEBUG
      return;
    }
    chunkp = &((*chunkp)->next);
  }
#if defined( _DEBUG )
  printf( "--- OMG ------\n");
#endif //_DEBUG
}

#if defined( _DEBUG )
void reference_count_stack_check(Cell cell)
{
  if( !(heap <= (char*)cell && (char*)cell < heap + HEAP_SIZE ) ){
    printf("[WARNING] cell %p points out of heap\n", cell);
  }
}

void check_reference_object(Cell* objp)
{
  Cell obj = *objp;
  if( obj ){
    Reference_Count_Header* header = (Reference_Count_Header*)obj - 1;
    if( header->is_visit == FALSE ){
      header->is_visit = TRUE;
      if( REF_CNT( obj ) <= 0 ){
	printf( "[WARNING!!!!!] count is NG: %p\n", obj);
      }else{
	//	printf( "%p ", (Reference_Count_Header*)obj-1);
	//	printf( "%p ", obj);
      }
      trace_object( obj, check_reference_object );
    }
  }
}

void clear_visit_flag( Cell* objp )
{
  Cell obj = *objp;
  if( obj ){
    Reference_Count_Header* header = (Reference_Count_Header*)obj - 1;
    if( header->is_visit == TRUE ){
      header->is_visit = FALSE;
      trace_object( obj, clear_visit_flag );
    }
  }
}

void check_reference()
{
  printf( "chk_ref.... " );
  trace_roots( check_reference_object );
  trace_roots( clear_visit_flag );
  printf( "\n" );
}

void check_freelist()
{
  printf( "check_freelist..." );

  Free_Chunk* tmp = freelist;
  while( tmp ){
    printf( "%d(%p: ), ", tmp->chunk_size, tmp );
    tmp = tmp->next;
  }
  printf( "\n");
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

void increment_count(Cell* objp)
{
  Cell obj = *objp;
  if( obj ){
    INC_REF_CNT( obj );
  }
}

void decrement_count(Cell* objp)
{
  Cell obj = *objp;
  if( obj ){
    DEC_REF_CNT( obj );
    if( REF_CNT( obj ) <= 0 ){
      //reclamation.
      reclaim_obj( obj );
#if defined( _DEBUG )
      check_reference();
#endif //_DEBUG
    }
  }
}

//Write Barrier.
void gc_write_barrier_reference_count(Cell* cellp, Cell newcell)
{
  increment_count( &newcell );
  decrement_count( cellp );
  *cellp = newcell;
}

//Init Pointer.
void gc_init_ptr_reference_count(Cell* cellp, Cell newcell)
{
  increment_count( &newcell );
  *cellp = newcell;
}

//memcpy.
void gc_memcpy_reference_count(char* dst, char* src, size_t size)
{
  memcpy(dst, src, size);

  trace_object( (Cell)dst, increment_count );
#if defined( _DEBUG )
  printf("memcpy\n");
#endif //_DEBUG
}

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
  Boolean visit_flag;
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
static void check_reference(Cell* objp);
static void check_obj(Cell obj);
static void clear_reference(Cell* objp);
static Cell check_stack[1000];
static int check_stack_top = 0;
#endif //_DEBUG

static char* heap           = NULL;
static Free_Chunk* freelist = NULL;

#define ZCT_SIZE 0xFF
static Cell zct[ZCT_SIZE];
static void add_zct(Cell obj);
static void scan_zct();
static void root_inc_cnt(Cell* objp);
static void root_dec_cnt(Cell* objp);
static int zct_top          = 0;

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
  freelist->next       = NULL;

  gc_info->gc_malloc        = gc_malloc_reference_count;
  gc_info->gc_start         = gc_start_reference_count;
  gc_info->gc_write_barrier = gc_write_barrier_reference_count;
  gc_info->gc_init_ptr      = gc_init_ptr_reference_count;
  gc_info->gc_memcpy        = gc_memcpy_reference_count;
#if defined( _DEBUG )
  gc_info->gc_stack_check = reference_count_stack_check;
#endif //_DEBUG
  memset(zct, 0, sizeof(zct));
  zct_top = 0;
}

 //Allocation.
 void* gc_malloc_reference_count( size_t size )
 {
   int allocate_size = ( get_obj_size(size) + 3 ) / 4 * 4;
   char* chunk = get_free_chunk( allocate_size );
   if( !chunk ){
     scan_zct();
     chunk = get_free_chunk( allocate_size );
     if( !chunk ){
       printf("Heap Exhausted.\n ");
       exit(-1);
     }
   }
   Reference_Count_Header* new_header = (Reference_Count_Header*)chunk;
   Cell ret = (Cell)(new_header+1);
   GET_OBJECT_SIZE(ret) = allocate_size;
   REF_CNT(ret)         = 0;
#if defined( _DEBUG )
   new_header->visit_flag = FALSE;
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
   size_t obj_size = GET_OBJECT_SIZE( obj );
   REF_CNT(obj) = -1;
   trace_object( obj, decrement_count );

#if defined( _DEBUG )
   check_obj(obj);
   printf( "reclaim\n" );
#endif //_DEBUG

   Free_Chunk* obj_top = (Free_Chunk*)((Reference_Count_Header*)obj - 1);

   if( !freelist ){
     freelist = obj_top;
     freelist->chunk_size = obj_size;
     freelist->next       = NULL;
   }else{
     Free_Chunk* tmp = NULL;
     for( tmp = freelist; tmp->next; tmp = tmp->next ){
       if( (char*)tmp < (char*)obj_top && (char*)obj_top < (char*)tmp->next ){
	 //Coalesce.
	 if( (char*)tmp + tmp->chunk_size == (char*)obj_top ){
	   if( (char*)obj_top + obj_size == (char*)tmp->next ){
	     tmp->next        = tmp->next->next;
	     tmp->chunk_size += (obj_size + tmp->next->chunk_size);
	   }else{
	     tmp->chunk_size += obj_size;
	   }
	 }else if( (char*)obj_top + obj_size == (char*)tmp->next ){
	   //TODO.
	   size_t new_size      = tmp->next->chunk_size + obj_size;
	   Free_Chunk* new_next = tmp->next->next;
	   obj_top->chunk_size  = new_size;
	   obj_top->next        = new_next;
	   tmp->next            = obj_top;
	 }else{
	   obj_top->next       = tmp->next;
	   obj_top->chunk_size = obj_size;
	   tmp->next           = obj_top;
	 }
	 return;
       }
     }
     tmp->next           = obj_top;
     obj_top->next       = NULL;
     obj_top->chunk_size = obj_size;
   }
#if defined( _CUT )
   ///////////
   while( *chunkp ){
     if( ( (char*)(*chunkp) + (*chunkp)->chunk_size ) == (char*)header ){
       //Coalescing.
 #if defined( _DEBUG )
       printf("Coalesce\n");
 #endif //_DEBUG
       (*chunkp)->chunk_size += obj_size;
       return;
     }else if( (char*)*chunkp > (char*)header ){
       Free_Chunk* next      = *chunkp;
       *chunkp               = (Free_Chunk*)header;
       (*chunkp)->next       = next;
       (*chunkp)->chunk_size = obj_size;
       printf("  [reclaimed] header: %p\n", header );
       return;
     }
     chunkp = &((*chunkp)->next);
   }
   *chunkp = (Free_Chunk*)obj;
   freelist->chunk_size = obj_size;
   freelist->next       = NULL;
#endif 
 }

#if defined( _DEBUG )
void reference_count_stack_check(Cell cell)
{
  if( !(heap <= (char*)cell && (char*)cell < heap + HEAP_SIZE ) ){
    printf("[WARNING] cell %p points out of heap\n", cell);
  }
}

void check_obj(Cell obj)
{
  check_stack_top = 0;
  trace_roots(check_reference);
  Cell tmp = NULL;
  while( check_stack_top > 0 ){
    tmp = check_stack[--check_stack_top-1];
    if( tmp == obj ){
      printf( "----------\n");
    }
  }

  check_stack_top = 0;
  trace_roots(clear_reference);
  while( check_stack_top > 0 ){
    tmp = check_stack[--check_stack_top-1];
  }
}

void check_reference(Cell* objp)
{
  Cell obj = *objp;
  if( !obj ){
    return;
  }
  Reference_Count_Header* header = (Reference_Count_Header*)obj - 1;
  if( header->visit_flag ){
    return;
  }
  header->visit_flag = TRUE;
  check_stack[check_stack_top++] = obj;
}

void clear_reference(Cell* objp)
{
  Cell obj = *objp;
  if( !obj ){
    return;
  }
  Reference_Count_Header* header = (Reference_Count_Header*)obj - 1;
  if( !header->visit_flag ){
    return;
  }
  header->visit_flag = FALSE;
  check_stack[check_stack_top++] = obj;
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
    if( REF_CNT( obj ) == 0 ){
      add_zct(obj);
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

void add_zct(Cell obj)
{
  if( zct_top >= ZCT_SIZE ){
    scan_zct();
  }

  zct[zct_top++] = obj;
}

void scan_zct()
{
  Cell obj;
  trace_roots(root_inc_cnt);
  for(; zct_top>0; zct_top--){
    obj = zct[zct_top-1];
    if(REF_CNT(obj) <= 0){
      reclaim_obj(obj);
    }
  }
  trace_roots(root_dec_cnt);
#if defined( _DEBUG )
  memset(zct, 0, sizeof(zct));
#endif //_DEBUG
}

void root_inc_cnt(Cell* objp)
{
  Cell obj = *objp;
  if( obj ){
    INC_REF_CNT(obj);
  }
}

void root_dec_cnt(Cell* objp)
{
  Cell obj = *objp;
  if( obj ){
    DEC_REF_CNT(obj);
  }
}

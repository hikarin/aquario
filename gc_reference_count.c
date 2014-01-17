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
  Boolean mark_bit;
#endif
}Reference_Count_Header;

struct free_chunk;

typedef struct free_chunk{
  int chunk_size;
  struct free_chunk* next;
}Free_Chunk;

static void gc_start_reference_count();
static inline void* gc_malloc_reference_count(size_t size);
static void gc_write_barrier_reference_count(Cell obj, Cell* cellp, Cell newcell);
static void gc_init_ptr_reference_count(Cell* cellp, Cell newcell);
static void gc_memcpy_reference_count(char* dst, char* src, size_t size);
static int get_obj_size( size_t size );
static char* get_free_chunk( size_t size );
static void reclaim_obj( Cell obj );
static void increment_count(Cell* objp);
static void decrement_count(Cell* objp);
static void gc_term_reference_count();

#if defined( _DEBUG )
static void reference_count_stack_check( Cell cell );
static void reference_check();
static void mark_obj(Cell* objp);
static void clear_obj(Cell* objp);

#define MARK_STACK_SIZE 1000
static int mark_stack_top = 0;
static Cell mark_stack[MARK_STACK_SIZE];
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

#if defined( _DEBUG )
#define IS_MARKED(obj)  (((Reference_Count_Header*)(obj) - 1)->mark_bit)
#define CLEAR_MARK(obj) (((Reference_Count_Header*)(obj) - 1)->mark_bit = FALSE)
#define SET_MARK(obj)   (((Reference_Count_Header*)(obj) - 1)->mark_bit = TRUE)
#endif

//Initialization.
void gc_init_reference_count(GC_Init_Info* gc_info)
{
  heap     = (char*)malloc(HEAP_SIZE);
  freelist = (Free_Chunk*)heap;
  freelist->chunk_size = HEAP_SIZE;
  freelist->next       = NULL;

  gc_info->gc_malloc        = gc_malloc_reference_count;
  gc_info->gc_start         = gc_start_reference_count;
  gc_info->gc_write_barrier = gc_write_barrier_reference_count;
  gc_info->gc_init_ptr      = gc_init_ptr_reference_count;
  gc_info->gc_memcpy        = gc_memcpy_reference_count;
  gc_info->gc_term          = gc_term_reference_count;
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
       printf("Heap Exhausted.\n");
       exit(-1);
     }
  }
  Reference_Count_Header* new_header = (Reference_Count_Header*)chunk;
  Cell ret = (Cell)(new_header+1);
  GET_OBJECT_SIZE(ret) = allocate_size;
  REF_CNT(ret)         = 0;
#if defined( _DEBUG )
  CLEAR_MARK(ret);
  reference_check();
#endif
  return ret;
}

char* get_free_chunk( size_t size )
{
  //returns a chunk which size is larger than required size.
  if( freelist ){
    if( !freelist->next ){
      if( freelist->chunk_size >= size ){
	char* ret = (char*)freelist;
	if( freelist->chunk_size - size >= sizeof( Free_Chunk ) ){
	  size_t old_size = freelist->chunk_size;
	  freelist = (Free_Chunk*)((char*)freelist + size);
	  freelist->chunk_size = old_size - size;
	  freelist->next = NULL;
	}else{
	  freelist = NULL;
	}
	return ret;
      }
    }else{
      Free_Chunk* tmp = freelist;
      while( tmp->next ){
	if( tmp->next->chunk_size >= size){
	  char* ret = (char*)tmp->next;
	  Free_Chunk* new_next = NULL;
	  if( tmp->next->chunk_size - size >= sizeof( Free_Chunk ) ){
	    Free_Chunk* next = tmp->next;
	    size_t old_size = next->chunk_size;
	    next->chunk_size -= size;	    
	    new_next = (Free_Chunk*)((char*)next + size);
	    new_next->chunk_size = old_size - size;
	    new_next->next       = next->next;
	  }else{
	    new_next  = tmp->next->next;
	  }
	  tmp->next   = new_next;
	  return ret;
	}
	tmp = tmp->next;
      }
    }
  }
  return NULL;
}

void reclaim_obj( Cell obj )
{
  size_t obj_size = GET_OBJECT_SIZE( obj );
  REF_CNT(obj) = -1;
  trace_object( obj, decrement_count );
  
  Free_Chunk* obj_top = (Free_Chunk*)((Reference_Count_Header*)obj - 1);
  
  if( !freelist ){
    //No object in freelist.
    freelist             = obj_top;
    freelist->chunk_size = obj_size;
    freelist->next       = NULL;
  }else if( obj_top < freelist ){
    Free_Chunk* chunk = obj_top;
    chunk->next       = freelist;
    if( (char*)obj_top + obj_size == (char*)freelist ){
      //Coalesce.
      chunk->chunk_size = obj_size + freelist->chunk_size;
    }else{
      chunk->chunk_size = obj_size;
    }
    freelist = chunk;
  }else{
    Free_Chunk* tmp = NULL;
    for( tmp = freelist; tmp->next; tmp = tmp->next ){
      if( (char*)tmp < (char*)obj_top && (char*)obj_top < (char*)tmp->next ){
	//Coalesce.
	if( (char*)tmp + tmp->chunk_size == (char*)obj_top ){
	  if( (char*)obj_top + obj_size == (char*)tmp->next ){
	    //Coalesce with previous and next Free_Chunk.
	    tmp->next        = tmp->next->next;
	    tmp->chunk_size += (obj_size + tmp->next->chunk_size);
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
void reference_count_stack_check(Cell cell)
{
  if( !(heap <= (char*)cell && (char*)cell < heap + HEAP_SIZE ) ){
    printf("[WARNING] cell %p points out of heap\n", cell);
  }
}

void clear_obj(Cell* objp)
{
  if( objp ){
    Cell obj = *objp;
    if( obj && IS_MARKED(obj) ){
      if( mark_stack_top >= MARK_STACK_SIZE ){
	printf("STACK OVERFLOW!!\n");
	exit(-1);
      }
      mark_stack[mark_stack_top++] = obj;
      CLEAR_MARK(obj);
    }
  }
}

void clear()
{
  mark_stack_top = 0;
  trace_roots(clear_obj);

  while( mark_stack_top > 0 ){
    Cell obj = mark_stack[--mark_stack_top];
    trace_object(obj, clear_obj);
  }
}

void mark_obj(Cell* objp)
{
  if( objp ){
    Cell obj = *objp;
    if( obj && !IS_MARKED(obj) ){
      if( mark_stack_top >= MARK_STACK_SIZE ){
	printf("STACK_OVERFLOW____!!!\n");
	exit(-1);
      }
      mark_stack[mark_stack_top++] = obj;
      SET_MARK(obj);
    }
  }
}

void mark()
{
  mark_stack_top = 0;
  trace_roots(mark_obj);

  while( mark_stack_top > 0 ){
    Cell obj = mark_stack[--mark_stack_top];
    trace_object(obj, mark_obj);
  }
}

void reference_check()
{
  mark();

  char* scan = heap;
  char* free_list_scan = (char*)freelist;
  while( scan < heap + HEAP_SIZE ){
    if( scan == free_list_scan ){
      //scan is chunk.
      Free_Chunk* chunk = (Free_Chunk*)scan;
      
      scan += chunk->chunk_size;
      free_list_scan = (char*)chunk->next;
    }else{
      Cell obj = (Cell)((Reference_Count_Header*)heap + 1);
      if( !IS_MARKED(obj) ){
	printf("%d ", type(obj) );
      }
      int obj_size = GET_OBJECT_SIZE(obj);
      scan += obj_size;
      //      printf("%d ", obj_size);
    }
  }
  clear();
}
#endif //_DEBUG

int get_obj_size( size_t size )
{
  return sizeof( Reference_Count_Header ) + size;
}

//Start Garbage Collection.
void gc_start_reference_count()
{
  scan_zct();
}

//For compatibility to trace_object(), this function receives a pointer to Cell.
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
void gc_write_barrier_reference_count(Cell obj, Cell* cellp, Cell newcell)
{
  increment_count( &newcell );
  decrement_count( cellp );
  *cellp = newcell;
#if defined( _DEBUG )
  reference_check();
#endif
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
  free(heap);
}

void add_zct(Cell obj)
{
  if( zct_top >= ZCT_SIZE ){
#if defined( _DEBUG )
    //    printf("scan zct\n");
#endif
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
      //      printf("scan: %d, type; %d\n", zct_top, type(obj) );
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

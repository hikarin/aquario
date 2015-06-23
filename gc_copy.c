#include "gc_base.h"
#include "gc_copy.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#if defined( _DEBUG )
#include "aquario.h"
#endif //_DEBUG

typedef struct copy_gc_header{
  int obj_size;
  Cell forwarding;
}Copy_GC_Header;

static void gc_start_copy();
static inline void* gc_malloc_copy(size_t size);
static void gc_term_copy();

static int get_obj_size( size_t size );

static void* copy_object(Cell obj);
static void copy_and_update(Cell* objp);

#define IS_ALLOCATABLE( size ) (top + sizeof( Copy_GC_Header ) + (size) < from_space + HEAP_SIZE/2 )
#define GET_OBJECT_SIZE(obj) (((Copy_GC_Header*)(obj)-1)->obj_size)

#define FORWARDING(obj) (((Copy_GC_Header*)(obj)-1)->forwarding)
#define IS_COPIED(obj) (FORWARDING(obj) != (obj) || !(from_space <= (char*)(obj) && (char*)(obj) < from_space+HEAP_SIZE/2))

static char* from_space  = NULL;
static char* to_space    = NULL;
static char* top         = NULL;

void* copy_object(Cell obj)
{
  Cell new_cell;
  long size;
  
  if( obj == NULL ){
    return NULL;
  }

  if( IS_COPIED(obj) ){
    return FORWARDING(obj);
  }
  Copy_GC_Header* new_header = (Copy_GC_Header*)top;
  Copy_GC_Header* old_header = ((Copy_GC_Header*)obj)-1;
  size = GET_OBJECT_SIZE(obj);
  memcpy(new_header, old_header, size);
  top += size;
  
  new_cell = (Cell)(((Copy_GC_Header*)new_header)+1);
  FORWARDING(obj) = new_cell;
  FORWARDING(new_cell) = new_cell;
  return new_cell;
}

void copy_and_update(Cell* objp)
{
  *objp = copy_object(*objp);
}

//Initialization.
void gc_init_copy(GC_Init_Info* gc_info)
{
  from_space = (char*)aq_malloc(HEAP_SIZE/2);
  to_space   = (char*)aq_malloc(HEAP_SIZE/2);
  top        = from_space;
  
  gc_info->gc_malloc        = gc_malloc_copy;
  gc_info->gc_start         = gc_start_copy;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_copy;

  printf("GC: Copying\n");
}

//Allocation.
void* gc_malloc_copy( size_t size )
{
  if( g_GC_stress || !IS_ALLOCATABLE(size) ){
    gc_start_copy();
    if( !IS_ALLOCATABLE( size ) ){
      printf("Heap Exhausted.\n ");
      exit(-1);
    }
  }
  Copy_GC_Header* new_header = (Copy_GC_Header*)top;
  Cell ret = (Cell)(new_header+1);
  int allocate_size = ( get_obj_size(size) + 3 ) / 4 * 4;
  top += allocate_size;
  FORWARDING(ret) = ret;
  new_header->obj_size = allocate_size;
  return ret;
}

int get_obj_size( size_t size ){
  return sizeof( Copy_GC_Header ) + size;
}

//Start Garbage Collection.
void gc_start_copy()
{
  top = to_space;
  
  //Copy all objects that are reachable from roots.
  trace_roots(copy_and_update);
  
  //Trace all objects that are in to space but not scanned.
  char* scanned = to_space;
  while( scanned < top ){
    Cell cell = (Cell)(((Copy_GC_Header*)scanned) + 1);
    trace_object(cell, copy_and_update);
    scanned += GET_OBJECT_SIZE(cell);
  }
  
  //swap from space and to space.
  void* tmp = from_space;
  from_space = to_space;
  to_space = tmp;
}

//term.
void gc_term_copy()
{
  aq_free( from_space );
  aq_free( to_space );

#if defined( _DEBUG )
  printf("used memory: %ld\n", get_total_malloc_size());
#endif
}

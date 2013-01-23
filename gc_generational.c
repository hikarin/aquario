#include "gc_base.h"
#include "gc_generational.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#if defined( _DEBUG )
#include "aquario.h"
#endif //_DEBUG

typedef struct generational_gc_header{
  int obj_size;
  Cell forwarding;
  int obj_age;
}Generational_GC_Header;

static void gc_start_generational();
static void minor_gc();
static void major_gc();

static void* gc_malloc_generational(size_t size);
static int get_obj_size( size_t size );

static void* copy_object(Cell obj);
static void copy_and_update(Cell* objp);
#if defined( _DEBUG )
static void generational_gc_stack_check(Cell cell);
#endif //_DEBUG

#define NERSARY_SIZE (HEAP_SIZE/32)
#define IS_ALLOCATABLE_NERSARY( size ) (top + sizeof( Generational_GC_Header ) + (size) < from_space + NERSARY_SIZE )
#define GET_OBJECT_SIZE(obj) (((Generational_GC_Header*)(obj)-1)->obj_size)

#define FORWARDING(obj) (((Generational_GC_Header*)(obj)-1)->forwarding)
#define IS_COPIED(obj) (FORWARDING(obj) != (obj))

#define TENURING_THRESHOLD 10
#define TENURED_SIZE (HEAP_SIZE-NERSARY_SIZE*2)
#define AGE(obj) (((Generational_GC_Header*)(obj)-1)->obj_age)
#define IS_OLD(obj) (AGE(obj) >= TENURING_THRESHOLD)
#define IS_ALLOCATABLE_TENURED() (tenured_top + NERSARY_SIZE < tenured_space + NERSARY_SIZE )

//nersary space.
static char* from_space  = NULL;
static char* to_space    = NULL;
static char* top         = NULL;

//tenured space.
static char* tenured_space = NULL;
static char* tenured_top   = NULL;

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
  Generational_GC_Header* new_header = (Generational_GC_Header*)( IS_OLD(obj) ? tenured_top : top );
  Generational_GC_Header* old_header = ((Generational_GC_Header*)obj)-1;
  size = GET_OBJECT_SIZE(obj);
  memcpy(new_header, old_header, size);
  top += size;
  
  new_cell = (Cell)(((Generational_GC_Header*)new_header)+1);
  FORWARDING(obj) = new_cell;
  FORWARDING(new_cell) = new_cell;
  AGE(new_cell)++;
  
  return new_cell;
}

void copy_and_update(Cell* objp)
{
  *objp = copy_object(*objp);
}

//Initialization.
void generational_gc_init(GC_Init_Info* gc_info){
  printf( "generational gc init\n");

  //nersary space.
  from_space = (char*)malloc(NERSARY_SIZE);
  to_space = (char*)malloc(NERSARY_SIZE);
  top = from_space;

  //tenured space.
  tenured_space = (char*)malloc(TENURED_SIZE);
  tenured_top   = tenured_space;
  
  gc_info->gc_malloc = gc_malloc_generational;
  gc_info->gc_start  = gc_start_generational;
#if defined( _DEBUG )
  gc_info->gc_stack_check = generational_gc_stack_check;
#endif //_DEBUG
}

//Allocation.
inline void* gc_malloc_generational( size_t size ){
  if( g_GC_stress || !IS_ALLOCATABLE_NERSARY(size) ){
    gc_start_generational();
    if( !IS_ALLOCATABLE_NERSARY( size ) ){
      printf("Heap Exhausted.\n ");
      exit(-1);
    }
  }
  Generational_GC_Header* new_header = (Generational_GC_Header*)top;
  Cell ret = (Cell)(new_header+1);
  int allocate_size = ( get_obj_size(size) + 3 ) / 4 * 4;
  top += allocate_size;
  FORWARDING(ret) = ret;
  new_header->obj_size = allocate_size;
  return ret;
}

#if defined( _DEBUG )
void generational_gc_stack_check(Cell cell){
  if( !(from_space <= (char*)cell && (char*)cell < from_space + NERSARY_SIZE ) && cell ){
    printf("[WARNING] cell %p points out of heap\n", cell);
  }
}
#endif //_DEBUG
int get_obj_size( size_t size ){
  return sizeof( Generational_GC_Header ) + size;
}

//Start Garbage Collection.
void gc_start_generational()
{
  if( !IS_ALLOCATABLE_TENURED() ){
    major_gc();
    if( !IS_ALLOCATABLE_TENURED() ){
      printf("Heap Exhausted!\n");
      exit(-1);
    }
  }
  minor_gc();
}

void minor_gc()
{
#if defined( _DEBUG )
  printf("GC start\n");
#endif //_DEBUG
  top = to_space;
  
  //Copy all objects that are reachable from roots.
  trace_roots(copy_and_update);
  
  //Trace all objects that are in to space but not scanned.
  char* scanned = to_space;
  while( scanned < top ){
    Cell cell = (Cell)(((Generational_GC_Header*)scanned) + 1);
    trace_object(cell, copy_and_update);
    scanned += GET_OBJECT_SIZE(cell);
  }
  
  //swap from space and to space.
  void* tmp = from_space;
  from_space = to_space;
  to_space = tmp;
#if defined( _DEBUG )
  memset(to_space, 0, NERSARY_SIZE);
#endif //_DEBUG

#if defined( _DEBUG )
  printf("GC end\n");
  scanned = from_space;
  while( scanned < top ){
    Cell cell = (Cell)(((Generational_GC_Header*)scanned)+1);
    printf( "%d ", AGE(cell));
    scanned += GET_OBJECT_SIZE(cell);
  }
  printf("\n");
#endif //_DEBUG
}

void major_gc()
{
  
}


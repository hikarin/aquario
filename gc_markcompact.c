#include "gc_base.h"
#include "gc_markcompact.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#if defined( _DEBUG )
#include "aquario.h"
#endif //_DEBUG

typedef struct markcompact_gc_header{
  int obj_size;
  Cell forwarding;
  Boolean mark_bit;
}MarkCompact_GC_Header;

static void gc_start_markcompact();
static void* gc_malloc_markcompact(size_t size);
static int get_obj_size( size_t size );
#if defined( _DEBUG )
static void markcompact_gc_stack_check(Cell cell);
#endif //_DEBUG

#define IS_ALLOCATABLE( size ) (top + sizeof( MarkCompact_GC_Header ) + (size) < heap + HEAP_SIZE )
#define GET_OBJECT_SIZE(obj) (((MarkCompact_GC_Header*)(obj)-1)->obj_size)

#define FORWARDING(obj) (((MarkCompact_GC_Header*)(obj)-1)->forwarding)
#define IS_MARKED(obj) (((MarkCompact_GC_Header*)(obj)-1)->mark_bit)
#define SET_MARK(obj) (((MarkCompact_GC_Header*)(obj)-1)->mark_bit=TRUE)
#define CLEAR_MARK(obj) (((MarkCompact_GC_Header*)(obj)-1)->mark_bit=FALSE)

static char* heap        = NULL;
static char* top         = NULL;
static char* new_top     = NULL;

#define MARK_STACK_SIZE 1000
static int mark_stack_top;
static Cell mark_stack[MARK_STACK_SIZE];

static void mark_object(Cell* objp);
static void move_object(Cell obj);
static void update(Cell* objp);
static void calc_new_address();
static void update_pointer();
static void slide();
static void mark();
static void compact();

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

void move_object(Cell obj)
{
  long size = GET_OBJECT_SIZE(obj);
  MarkCompact_GC_Header* new_header = ((MarkCompact_GC_Header*)(FORWARDING(obj)))-1;
  MarkCompact_GC_Header* old_header = ((MarkCompact_GC_Header*)obj)-1;
  memcpy(new_header, old_header, size);
  Cell new_cell = (Cell)(((MarkCompact_GC_Header*)new_header)+1);

  FORWARDING(new_cell) = new_cell;
}

//Initialization.
void markcompact_gc_init(GC_Init_Info* gc_info){
  printf( "mark compact gc init\n");
  heap = (char*)malloc(HEAP_SIZE);
  top = heap;

  gc_info->gc_malloc = gc_malloc_markcompact;
  gc_info->gc_start  = gc_start_markcompact;
#if defined( _DEBUG )
  gc_info->gc_stack_check = markcompact_gc_stack_check;
#endif //_DEBUG
}

//Allocation.
inline void* gc_malloc_markcompact( size_t size )
{
  if( g_GC_stress || !IS_ALLOCATABLE( size ) ){
    gc_start_markcompact();
    if( !IS_ALLOCATABLE( size ) ){
      printf("Heap Exhausted.\n");
      exit(-1);
    }
  }
  MarkCompact_GC_Header* new_header = (MarkCompact_GC_Header*)top;
  Cell ret = (Cell)(new_header+1);
  int allocate_size = (get_obj_size(size) + 3) / 4 * 4;
  top += allocate_size;
  FORWARDING(ret) = ret;
  CLEAR_MARK(ret);
  new_header->obj_size = allocate_size;
  return ret;
}

#if defined( _DEBUG )
void markcompact_gc_stack_check(Cell cell)
{
  if( !(heap <= (char*)cell && (char*)cell < heap + HEAP_SIZE ) && cell ){
    printf("[WARNING] cell %p points out of heap\n", cell);
  }  
}
#endif //_DEBUG

int get_obj_size( size_t size ){
  return sizeof( MarkCompact_GC_Header ) + size;
}

void update(Cell* cellp)
{
  if( *cellp ){
    *cellp = FORWARDING(*cellp);
  }
}

void calc_new_address()
{
  char* scanned = heap;
  new_top = heap;
  Cell cell = NULL;
  int obj_size = 0;
  while( scanned < top ){
    cell = (Cell)((MarkCompact_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      FORWARDING(cell) = (Cell)((MarkCompact_GC_Header*)new_top+1);
      new_top += obj_size;
    }
    scanned += obj_size;
  }
}

void update_pointer()
{
  char* scanned = heap;
  Cell cell = NULL;
  int obj_size = 0;
  trace_roots(update);
  while( scanned < top ){
    cell = (Cell)((MarkCompact_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      trace_object(cell, update);
    }
    scanned += obj_size;
  }
}

void slide()
{
  char* scanned = heap;
  Cell cell = NULL;
  int obj_size = 0;
  while( scanned < top ){
    cell = (Cell)((MarkCompact_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      CLEAR_MARK(cell);
      move_object(cell);
    }
    scanned += obj_size;
  }
  top = new_top;
#if defined( _DEBUG )
  memset(top, 0, heap+HEAP_SIZE-top);
#endif //_DEBUG
}


//Start Garbage Collection.
void mark()
{
  //mark root objects.
  trace_roots(mark_object);

  Cell obj = NULL;
  while(mark_stack_top > 0){
    obj = mark_stack[--mark_stack_top];
    trace_object(obj, mark_object);
  }
}

void compact()
{
  calc_new_address();
  update_pointer();
  slide();
}

void gc_start_markcompact()
{
  //initialization.
  mark_stack_top = 0;

  //mark phase.
  mark();

  //compaction phase.
  compact();

#if defined( _DEBUG )
  printf("gc end\n");
#endif //_DEBUG
}

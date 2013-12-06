#include "gc_base.h"
#include "gc_generational.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#if defined( _DEBUG )
#include "aquario.h"
#endif //_DEBUG

//TODO: try to make it smaller.
typedef struct generational_gc_header{
  int obj_size;
  int mark_bit;
  Cell forwarding;
  int obj_age;
  Boolean visited_flag;
}Generational_GC_Header;

static void gc_start_generational();
static void minor_gc();
static void major_gc();

static inline void* gc_malloc_generational(size_t size);
static void gc_term_generational();

static int get_obj_size( size_t size );

static void* copy_object(Cell obj);
static void copy_and_update(Cell* objp);
#if defined( _DEBUG )
static void generational_gc_stack_check(Cell cell);
static void check_minor_gc();
static int major_gc_count = 0;
static int minor_gc_count = 0;
#endif //_DEBUG

//nersary space.
static char* from_space    = NULL;
static char* to_space      = NULL;
static char* nersary_top   = NULL;

//tenured space.
static char* tenured_space   = NULL;
static char* tenured_top     = NULL;
static char* tenured_new_top = NULL;

//remembered set.
#define REMEMBERED_SET_SIZE 0x200
static Cell remembered_set[ REMEMBERED_SET_SIZE ];
static int remembered_set_top = 0;
static void add_remembered_set(Cell obj);
static void gc_write_barrier_generational(Cell obj, Cell* cellp, Cell newcell);

#define NERSARY_SIZE (HEAP_SIZE/32)
#define TENURED_SIZE (HEAP_SIZE-NERSARY_SIZE*2)
#define TENURING_THRESHOLD 10

#define IS_ALLOCATABLE_NERSARY( size ) (nersary_top + sizeof( Generational_GC_Header ) + (size) < from_space + NERSARY_SIZE )
#define GET_OBJECT_SIZE(obj) (((Generational_GC_Header*)(obj)-1)->obj_size)
#define IS_TENURED(obj) (tenured_space <= (char*)(obj) && (char*)(obj) < tenured_space + TENURED_SIZE )
#define IS_NERSARY(obj) ( (from_space <= (char*)(obj) && (char*)(obj) < from_space + NERSARY_SIZE ) || (to_space <= (char*)(obj) && (char*)(obj) < to_space + NERSARY_SIZE ) )

#define FORWARDING(obj) (((Generational_GC_Header*)(obj)-1)->forwarding)
#define IS_COPIED(obj) (FORWARDING(obj) != (obj))
#define IS_VISITED(obj) (((Generational_GC_Header*)(obj)-1)->visited_flag)

#define AGE(obj) (((Generational_GC_Header*)(obj)-1)->obj_age)
#define IS_OLD(obj) (AGE(obj) >= TENURING_THRESHOLD)

#define IS_ALLOCATABLE_TENURED() (tenured_top + NERSARY_SIZE < tenured_space + TENURED_SIZE )

#define IS_MARKED(obj) (((Generational_GC_Header*)(obj)-1)->mark_bit)
#define SET_MARK(obj) (((Generational_GC_Header*)(obj)-1)->mark_bit=TRUE)
#define CLEAR_MARK(obj) (((Generational_GC_Header*)(obj)-1)->mark_bit=FALSE)

#define MARK_STACK_SIZE 1000
static int mark_stack_top;
static Cell mark_stack[MARK_STACK_SIZE];

static Boolean has_young_child(Cell obj);
static void reset_mark_bit();

static void mark_object(Cell* objp);
static void move_object(Cell obj);
static void update(Cell* objp);
static void calc_new_address();
static void update_pointer();
static void slide();
static void mark();
static void compact();

//Initialization.
void generational_gc_init(GC_Init_Info* gc_info)
{
  printf( "generational gc init\n");
  printf( "NERSARY_SIZE: %d, TENURED_SIZE: %d\n", NERSARY_SIZE, TENURED_SIZE);

  //nersary space.
  from_space = (char*)malloc(NERSARY_SIZE);
  to_space = (char*)malloc(NERSARY_SIZE);
  nersary_top = from_space;

  //tenured space.
  tenured_space   = (char*)malloc(TENURED_SIZE);
  tenured_top     = tenured_space;
  tenured_new_top = tenured_space;
  
  gc_info->gc_malloc        = gc_malloc_generational;
  gc_info->gc_start         = gc_start_generational;
  gc_info->gc_term          = gc_term_generational;
  gc_info->gc_write_barrier = gc_write_barrier_generational;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
#if defined( _DEBUG )
  gc_info->gc_stack_check   = generational_gc_stack_check;
#endif //_DEBUG
}


Boolean has_young = FALSE;

static void set_young_flag(Cell* pObj)
{
  Cell obj = *pObj;
  if( IS_NERSARY(obj) ){
    has_young = TRUE;
  }
}

void reset_mark_bit()
{
  char* scan = from_space;
  int obj_size = 0;
  while( nersary_top < from_space ){
    Cell obj = (Cell)( scan + sizeof(Generational_GC_Header) );
    obj_size = GET_OBJECT_SIZE(obj);
    if( IS_MARKED(obj) ){
      CLEAR_MARK(obj);
    }
    scan += obj_size;
  }
}

Boolean has_young_child(Cell obj)
{
  has_young = FALSE;
  trace_object(obj, set_young_flag);
  return has_young;
}

//Allocation.
void* gc_malloc_generational( size_t size )
{
  if( g_GC_stress || !IS_ALLOCATABLE_NERSARY(size) ){
    gc_start_generational();
    if( !IS_ALLOCATABLE_NERSARY( size ) ){
      printf("gc_malloc_generational: Heap Exhausted.\n ");
      exit(-1);
    }
  }
  Generational_GC_Header* new_header = (Generational_GC_Header*)nersary_top;
  Cell ret = (Cell)(new_header+1);
  int allocate_size = ( get_obj_size(size) + 3 ) / 4 * 4;
  nersary_top += allocate_size;
  FORWARDING(ret) = ret;
  new_header->obj_size = allocate_size;
  AGE(ret) = 0;
  new_header->visited_flag = FALSE;
  return ret;
}

void gc_term_generational()
{
  free(from_space);
  free(to_space);
  free(tenured_space);
}

#if defined( _DEBUG )
void generational_gc_stack_check(Cell cell){
  if( !( (from_space <= (char*)cell && (char*)cell < from_space + NERSARY_SIZE) ||
	 (tenured_space <= (char*)cell && (char*)cell < tenured_space + TENURED_SIZE ) ) ){
    printf("[WARNING] cell %p points out of heap(age: %d)\n", cell, AGE(cell));
    printf("\tfrom_space: %p - %p\n"
	   "\tto_space: %p - %p\n"
	   "tenured_space: %p - %p\n",
	   from_space, from_space + NERSARY_SIZE,
	   to_space, to_space + NERSARY_SIZE,
	   tenured_space, tenured_space + TENURED_SIZE );
  }
}

void check_minor_gc_object(Cell* cellp)
{
  Cell cell = *cellp;
  generational_gc_stack_check(cell);
}

void check_minor_gc()
{
  printf("check start....");
  char* scan = from_space;
  int obj_size = 0;
  while( scan < nersary_top ){
    Cell obj = (Cell)(scan + sizeof(Generational_GC_Header));
    trace_object(obj, check_minor_gc_object);
    obj_size = GET_OBJECT_SIZE(obj);
    scan += obj_size;
  }
  printf("end(used: %ld, tolerance: %ld)\n", scan - from_space, nersary_top - scan);
}
#endif //_DEBUG

int get_obj_size( size_t size ){
  return sizeof( Generational_GC_Header ) + size;
}

//Start Garbage Collection.
void gc_start_generational()
{
  if( !IS_ALLOCATABLE_TENURED() ){
    minor_gc();
    printf("------- major GC start --------\n");
    major_gc();
    if( !IS_ALLOCATABLE_TENURED() ){
      printf("Heap Exhausted!\n");
      exit(-1);
    }
  }else{
    minor_gc();
  }
}

/**** for Minor GC ****/
void minor_gc()
{
#if defined( _DEBUG )
  printf("minor GC start ");
#endif //_DEBUG
  nersary_top = to_space;
  
  //copy all objects that are reachable from roots.
  trace_roots(copy_and_update);

  //scan remembered set.
#if defined( _DEBUG )
  printf( "remembered object: %d\n", remembered_set_top );
#endif //defined( _DEBUG )
  int rem_index;
  for(rem_index = 0; rem_index < remembered_set_top; rem_index++){
    Cell cell = remembered_set[rem_index];
    trace_object( cell, copy_and_update );
  }

  //swap from space and to space.
  void* tmp = from_space;
  from_space = to_space;
  to_space = tmp;
#if defined( _DEBUG )
  printf( "minor GC end: %d\n", minor_gc_count++);
  check_minor_gc();
#endif //_DEBUG
}

void add_remembered_set(Cell obj)
{
  if( !IS_VISITED(obj) ){
    if( remembered_set_top >= REMEMBERED_SET_SIZE ){
      printf("remembered set full\n");
      major_gc();
    }
    remembered_set[remembered_set_top++] = obj;
    IS_VISITED(obj) = TRUE;
  }
}

void gc_write_barrier_generational(Cell obj, Cell* cellp, Cell newcell)
{
  if( IS_TENURED(obj) && IS_NERSARY(newcell) ){
    add_remembered_set(obj);
  }
  *cellp = newcell;
}

void copy_and_update(Cell* objp)
{
  *objp = copy_object(*objp);
}

void* copy_object(Cell obj)
{
  Cell new_cell;
  long size;
  
  if( obj == NULL ){
    return NULL;
  }
  if( IS_COPIED(obj) || IS_TENURED( obj ) ){
    return FORWARDING(obj);
  }
  size = GET_OBJECT_SIZE(obj);
  Generational_GC_Header* new_header = NULL;
  if( IS_OLD( obj ) ){
    //Promotion.
    new_header = (Generational_GC_Header* )tenured_top;
    if( has_young_child((Cell)new_header) ){
      add_remembered_set((Cell)new_header);
    }else{
      //      printf("no young child");
    }
    tenured_top += size;
  }else{
    new_header =( Generational_GC_Header* )nersary_top;
    nersary_top += size;
  }
  Generational_GC_Header* old_header = ((Generational_GC_Header*)obj)-1;
  memcpy(new_header, old_header, size);
  
  new_cell = (Cell)(((Generational_GC_Header*)new_header)+1);
  FORWARDING(obj) = new_cell;
  FORWARDING(new_cell) = new_cell;
  AGE(new_cell)++;

  trace_object(new_cell, copy_and_update);
  return new_cell;
}

/**** for Major GC ****/
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
  Generational_GC_Header* new_header = ((Generational_GC_Header*)(FORWARDING(obj)))-1;
  Generational_GC_Header* old_header = ((Generational_GC_Header*)obj)-1;
  memcpy(new_header, old_header, size);
  Cell new_cell = (Cell)(((Generational_GC_Header*)new_header)+1);

  FORWARDING(new_cell) = new_cell;
}

void update(Cell* cellp)
{
  if( *cellp ){
    *cellp = FORWARDING(*cellp);
  }
}

void calc_new_address()
{
  Cell cell = NULL;
  int obj_size = 0;
  char* scanned = tenured_space;
  tenured_new_top = tenured_space;
  while( scanned < tenured_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      FORWARDING(cell) = (Cell)((Generational_GC_Header*)tenured_new_top+1);
      tenured_new_top += obj_size;
    }
    scanned += obj_size;
  }
}

void update_pointer()
{
  trace_roots(update);

  char* scanned = NULL;
  Cell cell = NULL;
  int obj_size = 0;
  scanned = to_space;
  while( scanned < nersary_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    trace_object(cell, update);
    scanned += obj_size;
  }

  scanned = tenured_space;
  while( scanned < tenured_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    IS_VISITED(cell) = FALSE;
    if( IS_MARKED(cell) ){
      trace_object(cell, update);
      if( has_young_child(cell) ){
	add_remembered_set(cell);
      }
    }
    scanned += obj_size;
  }
}

void slide()
{
  char* scanned = tenured_space;
  Cell cell = NULL;
  int obj_size = 0;

  //scan tenured space.
  while( scanned < tenured_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      CLEAR_MARK(cell);
      move_object(cell);
    }
    scanned += obj_size;
  }
  tenured_top = tenured_new_top;

  //scan nersary space.
  scanned = to_space;
  while( scanned < nersary_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    CLEAR_MARK(cell);
    scanned += obj_size;
  }
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

void major_gc()
{
  printf("\tmajor GC start ... ");

  //initialization.
  mark_stack_top = 0;
  remembered_set_top = 0;

  //mark phase.
  mark();

  //compaction phase.
  compact();

  //reset mark bit in nersary space.
  reset_mark_bit();
}

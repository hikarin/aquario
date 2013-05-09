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
  int mark_bit;
  Cell forwarding;
  int obj_age;
#if defined( _DEBUG )
  Boolean visited_flag;
#endif //_DEBUG
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
static int gc_count = 0;
#endif //_DEBUG

//nersary space.
static char* from_space  = NULL;
static char* to_space    = NULL;
static char* nersary_top = NULL;

//tenured space.
static char* tenured_space   = NULL;
static char* tenured_top     = NULL;
static char* tenured_new_top = NULL;

//remembered set.
#define REMEMBERED_SET_SIZE 300
static Cell remembered_set[ REMEMBERED_SET_SIZE ];
static int remembered_set_top = 0;
static void add_remembered_set(Cell obj);
static void gc_write_barrier_generational(Cell obj, Cell* cellp, Cell newcell);

#define NERSARY_SIZE (HEAP_SIZE/32)
#define TENURED_SIZE (HEAP_SIZE-NERSARY_SIZE*2)
#define TENURING_THRESHOLD 4

#define IS_ALLOCATABLE_NERSARY( size ) (nersary_top + sizeof( Generational_GC_Header ) + (size) < from_space + NERSARY_SIZE )
#define GET_OBJECT_SIZE(obj) (((Generational_GC_Header*)(obj)-1)->obj_size)
#define IS_TENURED(obj) (tenured_space <= (char*)(obj) && (char*)(obj) < tenured_space + TENURED_SIZE )
#define IS_NERSARY(obj) ( (from_space <= (char*)(obj) && (char*)(obj) < from_space + NERSARY_SIZE ) || (to_space <= (char*)(obj) && (char*)(obj) < to_space + NERSARY_SIZE ) )

#define FORWARDING(obj) (((Generational_GC_Header*)(obj)-1)->forwarding)
#define IS_COPIED(obj) (FORWARDING(obj) != (obj))

#define AGE(obj) (((Generational_GC_Header*)(obj)-1)->obj_age)
#define IS_OLD(obj) (AGE(obj) >= TENURING_THRESHOLD)

#define IS_ALLOCATABLE_TENURED() (tenured_top + NERSARY_SIZE < tenured_space + TENURED_SIZE )

#define IS_MARKED(obj) (((Generational_GC_Header*)(obj)-1)->mark_bit)
#define SET_MARK(obj) (((Generational_GC_Header*)(obj)-1)->mark_bit=TRUE)
#define CLEAR_MARK(obj) (((Generational_GC_Header*)(obj)-1)->mark_bit=FALSE)

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
#if defined( _DEBUG )
  gc_info->gc_stack_check = generational_gc_stack_check;
#endif //_DEBUG
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
#if defined( _DEBUG )
  new_header->visited_flag = FALSE;
#endif //_DEBUG
  return ret;
}

#if defined( _DEBUG )
static Cell mark_stack[ 1000 ];
static int mark_stack_top = 0;

static void check_ref();
static void mark_obj(Cell* objp);
static void clear_obj(Cell* objp);

void check_ref()
{
  mark_stack_top = 0;
  trace_roots(mark_obj);
  Cell obj = NULL;
  while( mark_stack_top > 0 ){
    obj = mark_stack[ --mark_stack_top ];
    mark_obj( &obj );
  }

  trace_roots(clear_obj);
  while( mark_stack_top > 0 ){
    obj = mark_stack[ --mark_stack_top ];
    clear_obj( &obj );
  }  
}

void mark_obj(Cell* objp)
{
  if( !objp || !*objp ){
    return;
  }
  Cell obj = *objp;
  Generational_GC_Header* header = (Generational_GC_Header*)obj - 1;
  if( header->visited_flag == FALSE ){
    header->visited_flag = TRUE;
    mark_stack[stack_top++] = obj;
  }
}


void clear_obj(Cell* objp)
{
  if( !objp || !*objp ){
    return;
  }
  Cell obj = *objp;
  Generational_GC_Header* header = (Generational_GC_Header*)obj - 1;
  if( header->visited_flag == TRUE ){
    header->visited_flag = FALSE;
    mark_stack[stack_top++] = obj;
  }
}

#endif //_DEBUG

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
  while( remembered_set_top > 0 ){
    Cell cell = remembered_set[--remembered_set_top];
    trace_object( cell, copy_and_update );
  }

  //traverse copied objects.
  Cell cell = NULL;
  int obj_size = 0;
  char* scanned = to_space;
  while( scanned < nersary_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    trace_object( cell, copy_and_update );
    scanned += obj_size;
  }

#if defined( _DEBUG )
  printf("\n");
#endif //_DEBUG

  //swap from space and to space.
  void* tmp = from_space;
  from_space = to_space;
  to_space = tmp;
#if defined( _DEBUG )
  memset(to_space, 0, NERSARY_SIZE);
  printf(".... end\n");
  check_ref();
#endif //_DEBUG
}

void add_remembered_set(Cell obj)
{
  if( remembered_set_top >= REMEMBERED_SET_SIZE ){
    gc_start_generational();
  }
  remembered_set[remembered_set_top++] = obj;
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
#if defined( _DEBUG )
  if( !*objp ){
    printf("NO GOOD\n");
  }
#endif //_DEBUG
}

void* copy_object(Cell obj)
{
  Cell new_cell;
  long size;
  
  if( obj == NULL ){
    return NULL;
  }
  if( IS_COPIED(obj) || IS_TENURED( obj ) ){
#if defined( _DEBUG )
    if( AGE( obj ) > TENURING_THRESHOLD + 1){
      printf("OMG TOO OLD: %d\n", AGE(obj) );
    }else if( !IS_TENURED(FORWARDING(obj)) && !IS_NERSARY(FORWARDING(obj)) ){
      printf("OMG copy_and_update: %p, %p\n", obj, FORWARDING(obj) );
    }
#endif //_DEBUG
    return FORWARDING(obj);
  }
  size = GET_OBJECT_SIZE(obj);
  Generational_GC_Header* new_header = NULL;
  if( IS_OLD( obj ) ){
    //Promotion.
    new_header = (Generational_GC_Header*)tenured_top;
    tenured_top += size;
  }else{
    new_header =( Generational_GC_Header*)nersary_top;
    nersary_top += size;
  }
  Generational_GC_Header* old_header = ((Generational_GC_Header*)obj)-1;
  memcpy(new_header, old_header, size);
  
  new_cell = (Cell)(((Generational_GC_Header*)new_header)+1);
  FORWARDING(obj) = new_cell;
  FORWARDING(new_cell) = new_cell;
  AGE(new_cell)++;
#if defined( _DEBUG )
  printf( "cp(%d) ", (int)size );
#endif //_DEBUG
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
#if defined( _CUT )
  scanned = from_space;
#else
  scanned = to_space;
#endif //_CUT
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
    if( IS_MARKED(cell) ){
      trace_object(cell, update);
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
#if defined( _DEBUG )
  if( tenured_top > tenured_space + TENURED_SIZE ){
    printf("OMG slide\n");
  }
#endif //defined( _DEBUG )

  //scan nersary space.
#if defined( _CUT )
  scanned = from_space;
#else
  scanned = to_space;
#endif //_CUT
  while( scanned < nersary_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    CLEAR_MARK(cell);
    scanned += obj_size;
  }

#if defined( _DEBUG )
  scanned = tenured_space;
  printf("[DEBUG] ");
  while( scanned < tenured_top ){
    Cell cell = (Cell)((Generational_GC_Header*)scanned+1);
    if( !( 0 <= type(cell) && type(cell) < 10 ) || GET_OBJECT_SIZE(cell) > 100 ){
      printf("%d,%d  ", type(cell), GET_OBJECT_SIZE(cell) );
    }
    scanned += GET_OBJECT_SIZE(cell);
  }
  printf("\n");
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

void major_gc()
{
#if defined( _DEBUG )
  printf("major GC start ... ");
#endif //_DEBUG
  //initialization.
  mark_stack_top = 0;

  //mark phase.
  mark();

  //compaction phase.
  compact();

#if defined( _DEBUG )
  //memset(tenured_top, 0, tenured_space + TENURED_SIZE - tenured_top);
  printf("major GC end(%d)\n", gc_count++);
#endif //defined( _DEBUG )
}

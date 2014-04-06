#include "gc_base.h"
#include "gc_generational.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "aquario.h"

//#define _CUT

#if defined( _DEBUG )
#include "aquario.h"
#endif //_DEBUG

typedef struct generational_gc_header{
  int obj_size;
  int flags;
  Cell forwarding;
}Generational_GC_Header;

#define NERSARY_SIZE (HEAP_SIZE/10)
#define TENURED_SIZE (HEAP_SIZE-NERSARY_SIZE*2)
#define TENURING_THRESHOLD 30

#define MASK_OBJ_AGE        (0x000000FF)
#define MASK_MARK_BIT       (1<<8)
#define MASK_REMEMBERED_BIT (1<<9)
#define MASK_TENURED_BIT    (1<<10)

#define OBJ_HEADER(obj) ((Generational_GC_Header*)(obj)-1)
#define OBJ_FLAGS(obj) ((OBJ_HEADER(obj))->flags)

#if defined( _CUT )
#define IS_MARKED(obj) (OBJ_FLAGS(obj) & MASK_MARK_BIT)
#define SET_MARK(obj) (OBJ_FLAGS(obj) |= MASK_MARK_BIT)
#define CLEAR_MARK(obj) (OBJ_FLAGS(obj) =~ MASK_MARK_BIT)
#else //defined( _CUT )
//mark table: a bit per WORD
static int nersary_mark_tbl[NERSARY_SIZE/64+1];
static int tenured_mark_tbl[TENURED_SIZE/64+1];

#define IS_MARKED_TENURED(obj) (tenured_mark_tbl[( ((char*)obj-tenured_top)/64 )] & (1 << (((char*)obj-tenured_top)%64) ))
#define IS_MARKED_NERSARY(obj) (nersary_mark_tbl[( ((char*)obj-from_space)/64 )] & (1 << (((char*)obj-from_space)%64) ) )
#define IS_MARKED(obj) (IS_OLD(obj) ? IS_MARKED_TENURED(obj) : IS_MARKED_NERSARY(obj))

#define SET_MARK(obj) if(IS_OLD(obj)){					\
    tenured_mark_tbl[( ((char*)obj-tenured_top)/64 )] |= (1 << (((char*)obj-tenured_top)%64) ); \
  }else{								\
  nersary_mark_tbl[( ((char*)obj-from_space)/64 )] |= (1 << (((char*)obj-from_space)%64) ); \
  }
#endif //defined( _CUT )

#define IS_REMEMBERED(obj)   (OBJ_FLAGS(obj) & MASK_REMEMBERED_BIT)
#define SET_REMEMBERED(obj)    (OBJ_FLAGS(obj) |= MASK_REMEMBERED_BIT)
#define CLEAR_REMEMBERED(obj)  (OBJ_FLAGS(obj) =~ MASK_REMEMBERED_BIT)

#define AGE(obj)     (OBJ_FLAGS(obj) & MASK_OBJ_AGE)
#define IS_OLD(obj)  (AGE(obj) >= TENURING_THRESHOLD)
//#define INC_AGE(obj) (OBJ_FLAGS(obj) = (OBJ_FLAGS(obj) & MASK_EXCLUDE_AGE) | (AGE(obj)+1))
#define INC_AGE(obj) (OBJ_FLAGS(obj)++)

static void gc_start_generational();
static void minor_gc();
static void major_gc();

static inline void* gc_malloc_generational(size_t size);
static void gc_term_generational();

static void* copy_object(Cell obj);
static void copy_and_update(Cell* objp);
static void copy_and_update_add_rems(Cell* objp);
#if defined( _DEBUG )
static void generational_gc_stack_check(Cell cell);
#endif //_DEBUG

//nersary space.
static char* from_space    = NULL;
static char* to_space      = NULL;
static char* nersary_top   = NULL;

//tenured space.
static char* tenured_space   = NULL;
static char* tenured_top     = NULL;

//remembered set.
#define REMEMBERED_SET_SIZE 1000
static Cell remembered_set[ REMEMBERED_SET_SIZE ];
static int remembered_set_top = 0;
static void add_remembered_set(Cell obj);
static void gc_write_barrier_generational(Cell obj, Cell* cellp, Cell newcell);

#define IS_ALLOCATABLE_NERSARY( size ) (nersary_top + sizeof( Generational_GC_Header ) + (size) < from_space + NERSARY_SIZE )
#define GET_OBJECT_SIZE(obj) (((Generational_GC_Header*)(obj)-1)->obj_size)

#define IS_TENURED_OLD(obj) (tenured_space <= (char*)(obj) && (char*)(obj) < tenured_space + TENURED_SIZE )
#define IS_NERSARY_OLD(obj) ( (from_space <= (char*)(obj) && (char*)(obj) < from_space + NERSARY_SIZE ) || (to_space <= (char*)(obj) && (char*)(obj) < to_space + NERSARY_SIZE ) )

#define IS_TENURED(obj)    (OBJ_FLAGS(obj) & MASK_TENURED_BIT)
#define SET_TENURED(obj)   (OBJ_FLAGS(obj) |= MASK_TENURED_BIT)
#define CLEAR_TENURED(obj) (OBJ_FLAGS(obj) =~ MASK_TENURED_BIT)
#define IS_NERSARY(obj)    (!IS_TENURED(obj))

#define FORWARDING(obj) (((Generational_GC_Header*)(obj)-1)->forwarding)

#define IS_COPIED(obj) (FORWARDING(obj) != (obj) || (to_space <= (char*)(obj) && (char*)(obj) < to_space+NERSARY_SIZE))
//#define IS_COPIED(obj) (FORWARDING(obj) != (obj))

#define IS_ALLOCATABLE_TENURED() (tenured_top + NERSARY_SIZE < tenured_space + TENURED_SIZE )

#define MARK_STACK_SIZE 1000
static int mark_stack_top;
static Cell mark_stack[MARK_STACK_SIZE];

static Boolean has_young_child = FALSE;

static void mark_object(Cell* objp);
static void move_object(Cell obj);
static void update_tenured(Cell* objp);
static void update_nersary(Cell* objp);
static void calc_new_address();
static void update_pointer();
static void slide();
static void mark();
static void compact();

//Initialization.
void gc_init_generational(GC_Init_Info* gc_info)
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
  
  gc_info->gc_malloc        = gc_malloc_generational;
  gc_info->gc_start         = gc_start_generational;
  gc_info->gc_term          = gc_term_generational;
  gc_info->gc_write_barrier = gc_write_barrier_generational;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
#if defined( _DEBUG )
  gc_info->gc_stack_check   = generational_gc_stack_check;
#endif //_DEBUG

#if defined( _CUT )
#else
  memset( nersary_mark_tbl, 0, sizeof(nersary_mark_tbl) );
  memset( tenured_mark_tbl, 0, sizeof(tenured_mark_tbl) );
#endif
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
  int allocate_size = ( size + sizeof(Generational_GC_Header) + 3 ) / 4 * 4;
  memset(new_header, 0, allocate_size);
  nersary_top += allocate_size;
  FORWARDING(ret) = ret;
  new_header->obj_size = allocate_size;
  return ret;
}

void gc_term_generational()
{
  free(from_space);
  free(to_space);
  free(tenured_space);
}

#if defined( _DEBUG )
void generational_gc_stack_check(Cell cell)
{
  //Do Nothing.
}
#endif //_DEBUG

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
  nersary_top = to_space;
  char* prev_nersary_top = nersary_top;
  char* prev_tenured_top = tenured_top;
  int prev_rems_top = 0;

  //copy all objects that are reachable from roots.
  trace_roots(copy_and_update);

  //scan remembered set.
  while( prev_nersary_top < nersary_top
	 || prev_rems_top < remembered_set_top
	 || prev_tenured_top < tenured_top ){
    //scan remembered set.
    int rem_index;
    for(rem_index = prev_rems_top; rem_index < remembered_set_top; rem_index++){
      Cell cell = remembered_set[rem_index];
      trace_object( cell, copy_and_update );
    }
    prev_rems_top = remembered_set_top;

    //scan nersary space.
    char* scan = prev_nersary_top;
    while( scan < (char*)nersary_top ){
      Cell obj = (Cell)((Generational_GC_Header*)scan + 1);
      int obj_size = GET_OBJECT_SIZE(obj);
      trace_object(obj, copy_and_update_add_rems);
      scan += obj_size;
    }
    prev_nersary_top = nersary_top;
    
    //scan tenured space.
    scan = prev_tenured_top;
    while( scan < (char*)tenured_top ){
      Cell obj = (Cell)((Generational_GC_Header*)scan + 1);
#if defined( _DEBUG )
      if( !IS_TENURED(obj) ){
	printf("OMG: not TENURED\n");
      }
#endif
      int obj_size = GET_OBJECT_SIZE(obj);
      has_young_child = FALSE;
      trace_object(obj, copy_and_update_add_rems);
      if( has_young_child ){
	add_remembered_set( obj );
      }
      scan += obj_size;
    }
    prev_tenured_top = tenured_top;
  }
  
  //swap from space and to space.
  void* tmp = from_space;
  from_space = to_space;
  to_space = tmp;
}

void add_remembered_set(Cell obj)
{
  if( !IS_REMEMBERED(obj) ){
    if( remembered_set_top >= REMEMBERED_SET_SIZE ){
      printf("remembered set full.\n");
      exit(-1);
    }
    remembered_set[remembered_set_top++] = obj;
    SET_REMEMBERED(obj);
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

void copy_and_update_add_rems(Cell* objp)
{
  *objp = copy_object(*objp);
  if( !IS_TENURED( *objp ) ){
    has_young_child = TRUE;
  }
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
#if defined( _DEBUG )
  printf("FLAGS: %d(%d) => ", OBJ_FLAGS(obj), AGE(obj));
  if( AGE(obj)  >= TENURING_THRESHOLD ){
    printf("why?? ");
  }
#endif
  INC_AGE(obj);
#if defined( _DEBUG )
  printf("FLAGS: %d(%d)\n", OBJ_FLAGS(obj), AGE(obj));
#endif

  if( IS_OLD(obj) ){
    //Promotion.
    new_header = (Generational_GC_Header*)tenured_top;
    tenured_top += size;
    SET_TENURED(obj);
  }else{
    new_header = (Generational_GC_Header*)nersary_top;
    nersary_top += size;
  }
  Generational_GC_Header* old_header = ((Generational_GC_Header*)obj)-1;
  memcpy(new_header, old_header, size);
  
  new_cell = (Cell)(((Generational_GC_Header*)new_header)+1);
  FORWARDING(obj) = new_cell;
  FORWARDING(new_cell) = new_cell;
  return new_cell;
}

/**** for Major GC ****/
void mark_object(Cell* objp)
{
  Cell obj = *objp;
  if( obj && !IS_MARKED(obj) ){
#if defined( _CUT )
    SET_MARK(obj);
#else
    SET_MARK(obj)
#endif
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
  Generational_GC_Header* new_header = (Generational_GC_Header*)FORWARDING(obj) - 1;
  Generational_GC_Header* old_header = (Generational_GC_Header*)obj - 1;

  memcpy(new_header, old_header, size);
  Cell new_cell = (Cell)(((Generational_GC_Header*)new_header)+1);

  FORWARDING(new_cell) = new_cell;
}

void update_tenured(Cell* cellp)
{
  if( *cellp ){
    *cellp = FORWARDING(*cellp);
    if( !IS_TENURED( *cellp ) ){
      has_young_child = TRUE;
    }
  }
}

void update_nersary(Cell* cellp)
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
  char* tenured_new_top = tenured_space;
  while( scanned < tenured_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED_TENURED(cell) ){
      FORWARDING(cell) = (Cell)((Generational_GC_Header*)tenured_new_top+1);
      tenured_new_top += obj_size;
    }
    scanned += obj_size;
  }
}

void update_pointer()
{
  trace_roots(update_nersary);

  char* scanned = NULL;
  Cell cell = NULL;
  int obj_size = 0;
  scanned = tenured_space;
  while( scanned < tenured_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED_TENURED( cell ) ){
      has_young_child = FALSE;
      trace_object(cell, update_tenured);
      if( has_young_child ){
	add_remembered_set(cell);
      }
    }
    scanned += obj_size;
  }

  scanned = from_space;
  while( scanned < nersary_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED_NERSARY( cell ) ){
      trace_object(cell, update_nersary);
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
  char* tenured_new_top = tenured_space;
  while( scanned < tenured_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED_TENURED(cell) ){
      (((Generational_GC_Header*)cell)-1)->flags = 0;
      move_object(cell);
      //      SET_TENURED(cell);
      tenured_new_top += obj_size;
    }
    scanned += obj_size;
  }
  tenured_top = tenured_new_top;

  //clear mark bit in young objects.
#if defined( _CUT )
  scanned = from_space;
  while( scanned < nersary_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED(cell) ){
      CLEAR_MARK(cell);
    }
    scanned += obj_size;
  }
#else
  memset( nersary_mark_tbl, 0, sizeof(nersary_mark_tbl) );
  memset( tenured_mark_tbl, 0, sizeof(tenured_mark_tbl) );
#endif
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
  //initialization.
  mark_stack_top = 0;
  remembered_set_top = 0;

  //mark phase.
  mark();

  //compaction phase.
  compact();

#if defined( _DEBUG )
  printf("major GC end\n");
#endif
}

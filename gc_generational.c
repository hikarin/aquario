#include "gc_base.h"
#include "gc_generational.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "aquario.h"

#if defined( _DEBUG )
#include "aquario.h"
#endif //_DEBUG

typedef struct generational_gc_header{
  int obj_size;
  int flags;
  Cell forwarding;
}Generational_GC_Header;

#define NERSARY_SIZE (HEAP_SIZE/10)
#define TENURED_SIZE (HEAP_SIZE-(NERSARY_SIZE*2))
#define TENURING_THRESHOLD  (10)

#define MASK_OBJ_AGE        (0x000000FF)
#define MASK_REMEMBERED_BIT (1<<8)
#define MASK_TENURED_BIT    (1<<9)

#define OBJ_HEADER(obj) ((Generational_GC_Header*)(obj)-1)

#define IS_TENURED(obj)    (OBJ_FLAGS(obj) & MASK_TENURED_BIT)
#define SET_TENURED(obj)   (OBJ_FLAGS(obj) |= MASK_TENURED_BIT)
#define IS_NERSARY(obj)    (!IS_TENURED(obj))

#define OBJ_FLAGS(obj) ((OBJ_HEADER(obj))->flags)

#define IS_REMEMBERED(obj)     (OBJ_FLAGS(obj) & MASK_REMEMBERED_BIT)
#define SET_REMEMBERED(obj)    (OBJ_FLAGS(obj) |= MASK_REMEMBERED_BIT)
#define CLEAR_REMEMBERED(obj)  (OBJ_FLAGS(obj) &= ~MASK_REMEMBERED_BIT)

#define AGE(obj)     (OBJ_FLAGS(obj) & MASK_OBJ_AGE)
#define IS_OLD(obj)  (AGE(obj) >= TENURING_THRESHOLD)
#define INC_AGE(obj) (OBJ_FLAGS(obj)++)

//mark table: a bit per WORD
static int nersary_mark_tbl[NERSARY_SIZE/64+1];
static int tenured_mark_tbl[TENURED_SIZE/64+1];

#define IS_MARKED_TENURED(obj) (tenured_mark_tbl[( ((char*)(obj)-tenured_space)/64 )] & (1 << (((char*)(obj)-tenured_space)%64) ))
#define IS_MARKED_NERSARY(obj) (nersary_mark_tbl[( ((char*)(obj)-from_space)/64 )] & (1 << (((char*)(obj)-from_space)%64) ) )
#define IS_MARKED(obj) (IS_TENURED(obj) ? IS_MARKED_TENURED(obj) : IS_MARKED_NERSARY(obj))

#define SET_MARK_TENURED(obj) (tenured_mark_tbl[( ((char*)(obj)-tenured_space)/64 )] |= (1 << (((char*)(obj)-tenured_space)%64) ))
#define SET_MARK_NERSARY(obj) (nersary_mark_tbl[( ((char*)(obj)-from_space)/64 )] |= (1 << (((char*)(obj)-from_space)%64) ))
#define SET_MARK(obj)  (IS_TENURED(obj) ? SET_MARK_TENURED(obj) : SET_MARK_NERSARY(obj))

static void gc_start_generational();
static void minor_gc();
static void major_gc();

static void* gc_malloc_generational(size_t size);
static void gc_term_generational();

static void* copy_object(Cell obj);
static void copy_and_update(Cell* objp);
static Boolean is_nersary_obj(Cell* objp);
#if defined( _DEBUG )
static void generational_gc_stack_check(Cell cell);
static void remembered_set_check();
static void minor_gc_check();
#endif

//nersary space.
static char* from_space    = NULL;
static char* to_space      = NULL;
static char* nersary_top   = NULL;

//tenured space.
static char* tenured_space   = NULL;
static char* tenured_top     = NULL;

//remembered set.
#define REMEMBERED_SET_SIZE  300
static Cell remembered_set[ REMEMBERED_SET_SIZE ];
static int remembered_set_top = 0;
static void add_remembered_set(Cell obj);
static void clean_remembered_set();
static void gc_write_barrier_generational(Cell obj, Cell* cellp, Cell newcell);

#define IS_ALLOCATABLE_NERSARY( size ) (nersary_top + sizeof( Generational_GC_Header ) + (size) < from_space + NERSARY_SIZE )
#define GET_OBJECT_SIZE(obj) (((Generational_GC_Header*)(obj)-1)->obj_size)

#define FORWARDING(obj) (((Generational_GC_Header*)(obj)-1)->forwarding)

#define IS_COPIED(obj) (FORWARDING(obj) != (obj) || (to_space <= (char*)(obj) && (char*)(obj) < to_space+NERSARY_SIZE))
#define IS_ALLOCATABLE_TENURED() (tenured_top + NERSARY_SIZE < tenured_space + TENURED_SIZE)

#define MARK_STACK_SIZE 1000
static int mark_stack_top;
static Cell mark_stack[MARK_STACK_SIZE];

static void mark_object(Cell* objp);
static void move_object(Cell obj);
static void update_forwarding(Cell* objp);
static void calc_new_address();
static void update_pointer();
static void slide();
static void mark();
static void compact();

//Initialization.
void gc_init_generational(GC_Init_Info* gc_info)
{
  printf( "generational gc init\n");

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

  memset( nersary_mark_tbl, 0, sizeof(nersary_mark_tbl) );
  memset( tenured_mark_tbl, 0, sizeof(tenured_mark_tbl) );
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
  OBJ_FLAGS(ret) = 0;
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

void remembered_set_check()
{
  char* scan = tenured_space;
  while(scan < tenured_top){
    Cell obj = (Cell)((Generational_GC_Header*)scan + 1);
    int obj_size = GET_OBJECT_SIZE(obj);
    if(trace_object_bool(obj, is_nersary_obj)){
      int rems_index = 0;
      Boolean found = FALSE;
      for(rems_index=0; rems_index < remembered_set_top; rems_index++){
	Cell check_obj = remembered_set[rems_index];
	if( check_obj == obj ){
	  found = TRUE;
	  break;
	}
      }
      if( !found ){
	printf("remembered_set_check: NOT REGISTERED!\n");
      }
    }
    scan += obj_size;
  }
}

void minor_gc_check()
{
  memset( nersary_mark_tbl, 0, sizeof(nersary_mark_tbl) );
  memset( tenured_mark_tbl, 0, sizeof(tenured_mark_tbl) );
  mark_stack_top = 0;
  mark();

  int rem_index;
  for(rem_index = 0; rem_index < remembered_set_top; rem_index++){
    Cell cell = remembered_set[rem_index];
    trace_object( cell, mark_object );
  }
  Cell obj = NULL;
  while(mark_stack_top > 0){
    obj = mark_stack[--mark_stack_top];
    trace_object(obj, mark_object);
  }

  char* scan = from_space;
  while(scan < nersary_top){
    Cell obj = (Cell)((Generational_GC_Header*)scan + 1);
    if( !IS_NERSARY(obj) || !IS_MARKED_NERSARY(obj) ){
      printf("minor_gc_check: FAILED!!(nersary)\n");
      printf("flag: %d, size: %d, type: %d, MARKED: %d, offset: %d, offset_top: %d\n",
	     OBJ_FLAGS(obj),
	     GET_OBJECT_SIZE(obj),
	     type(obj),
	     IS_MARKED_NERSARY(obj),
	     (int)(scan - from_space),
	     (int)(nersary_top - from_space));
      exit(-1);
    }
    int obj_size = GET_OBJECT_SIZE(obj);
    if(obj_size <= 0){
      printf("size 0!!!(nersary)\n");
      exit(-1);
    }
    scan += obj_size;
  }

  scan = tenured_space;
  while(scan < tenured_top){
    Cell obj = (Cell)((Generational_GC_Header*)scan + 1);
    if( !IS_TENURED(obj) ){
      printf("minor_gc_check: FAILED!!: %x, MARKED: %d, size: %d, kind: %d(tenured)\n",
	     OBJ_FLAGS(obj),
	     IS_MARKED_TENURED(obj),
	     GET_OBJECT_SIZE(obj),
	     type(obj) );
      exit(-1);
    }
    int obj_size = GET_OBJECT_SIZE(obj);
    if(obj_size <= 0){
      printf("size 0!!!(tenured)\n");
      exit(-1);
    }
    scan += obj_size;
  }

  //clear mark bit in young objects.
  memset( nersary_mark_tbl, 0, sizeof(nersary_mark_tbl) );
  memset( tenured_mark_tbl, 0, sizeof(tenured_mark_tbl) );

  static int minor_gc_cnt = 0;
  printf("minor_gc_check finished: %d\n", minor_gc_cnt++);
}
#endif

//Start Garbage Collection.
void gc_start_generational()
{
  minor_gc();
  if( !IS_ALLOCATABLE_TENURED() ){
    major_gc();
    if( !IS_ALLOCATABLE_TENURED() ){
      printf("Heap Exhausted!\n");
      exit(-1);
    }
  }
}

/**** for Minor GC ****/
void minor_gc()
{
  nersary_top = to_space;
  char* prev_nersary_top = nersary_top;
  char* prev_tenured_top = tenured_top;
  char* scan_tenured     = tenured_top;
#if defined( _DEBUG )
  remembered_set_check();
#endif

  //copy all objects that are reachable from roots.
  trace_roots(copy_and_update);

  //scan remembered set.
  int rem_index;
  for(rem_index = 0; rem_index < remembered_set_top; rem_index++){
    Cell cell = remembered_set[rem_index];
    trace_object( cell, copy_and_update );
  }

  while( prev_nersary_top < nersary_top || prev_tenured_top < tenured_top ){
    //scan copied objects in nersary space.
    char* scan = prev_nersary_top;

    while( scan < (char*)nersary_top ){
      Cell obj = (Cell)((Generational_GC_Header*)scan + 1);
      int obj_size = GET_OBJECT_SIZE(obj);
      trace_object(obj, copy_and_update);
      scan += obj_size;
    }
    prev_nersary_top = nersary_top;

    //scan copied objects in tenured space.
    scan = prev_tenured_top;
    while( scan < (char*)tenured_top ){
      Cell obj = (Cell)((Generational_GC_Header*)scan + 1);
#if defined( _DEBUG )
      if( !IS_TENURED(obj) ){
	printf("OMG: not TENURED!\n");
      }
#endif
      int obj_size = GET_OBJECT_SIZE(obj);
      trace_object(obj, copy_and_update);
      scan += obj_size;
    }
    prev_tenured_top = tenured_top;
  }

  //maintain remembered set.
  while( scan_tenured < tenured_top ){
    Cell obj = (Cell)((Generational_GC_Header*)scan_tenured + 1);
    if( trace_object_bool( obj, is_nersary_obj ) && !IS_REMEMBERED(obj) ){
      add_remembered_set(obj);
    }
    int obj_size = GET_OBJECT_SIZE(obj);
    scan_tenured += obj_size;
  }
#if defined( _DEBUG )
  remembered_set_check();
#endif
  //swap from space and to space.
  void* tmp = from_space;
  from_space = to_space;
  to_space = tmp;
#if defined( _DEBUG )
  minor_gc_check();
#endif
}

void clean_remembered_set()
{
  int rem_index;
  int remembered_set_top_new = 0;
  for(rem_index = 0; rem_index < remembered_set_top; rem_index++){
    Cell cell = remembered_set[rem_index];
    if( trace_object_bool(cell, is_nersary_obj) ){
      remembered_set[remembered_set_top_new++] = cell;
      SET_REMEMBERED(cell);
    }
    else{
      CLEAR_REMEMBERED(cell);
    }
  }

  remembered_set_top = remembered_set_top_new;
}

void add_remembered_set(Cell obj)
{
  if( remembered_set_top >= REMEMBERED_SET_SIZE ){
    clean_remembered_set();
    if( remembered_set_top >= REMEMBERED_SET_SIZE ){
      printf("remembered set full.\n");
      exit(-1);
    }
  }
  remembered_set[remembered_set_top++] = obj;
  SET_REMEMBERED(obj);
}

void gc_write_barrier_generational(Cell obj, Cell* cellp, Cell newcell)
{
  if( IS_TENURED(obj) && IS_NERSARY(newcell) && !IS_REMEMBERED(obj) ){
    add_remembered_set(obj);
  }
  *cellp = newcell;
}

void copy_and_update(Cell* objp)
{
  if( IS_COPIED(*objp) || IS_TENURED(*objp) ){
    *objp = FORWARDING(*objp);
  }else{
    *objp = copy_object(*objp);
  }
}

Boolean is_nersary_obj(Cell* objp)
{
  if(IS_NERSARY(*objp)){
    return TRUE;
  }else{
    return FALSE;
  }
}

void* copy_object(Cell obj)
{
  Cell new_cell;
  long size;
  
  size = GET_OBJECT_SIZE(obj);
  Generational_GC_Header* new_header = NULL;
  INC_AGE(obj);
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
  Generational_GC_Header* new_header = (Generational_GC_Header*)FORWARDING(obj) - 1;
  Generational_GC_Header* old_header = (Generational_GC_Header*)obj - 1;

  memcpy(new_header, old_header, size);
  Cell new_cell = (Cell)(((Generational_GC_Header*)new_header)+1);
  SET_TENURED(obj);

  FORWARDING(new_cell) = new_cell;
  if(IS_REMEMBERED(new_cell)){
    add_remembered_set(new_cell);
  }

  clean_remembered_set();
}

void update_forwarding(Cell* cellp)
{
  if(*cellp){
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
  trace_roots(update_forwarding);

  char* scanned = NULL;
  Cell cell = NULL;
  int obj_size = 0;
  scanned = tenured_space;
  while( scanned < tenured_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    if( IS_MARKED_TENURED( cell ) ){
      trace_object(cell, update_forwarding);
      if( trace_object_bool(cell, is_nersary_obj) ){
	if( remembered_set_top >= REMEMBERED_SET_SIZE ){
	  printf("remembered set full.\n");
	  exit(-1);
	}
	SET_REMEMBERED(cell);
      }else{
	CLEAR_REMEMBERED(cell);
      }
    }
    scanned += obj_size;
  }

  scanned = from_space;
  while( scanned < nersary_top ){
    cell = (Cell)((Generational_GC_Header*)scanned+1);
    obj_size = GET_OBJECT_SIZE(cell);
    trace_object(cell, update_forwarding);
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
      move_object(cell);
      tenured_new_top += obj_size;
    }
    scanned += obj_size;
  }
  tenured_top = tenured_new_top;

  //clear mark bit in young objects.
  memset( nersary_mark_tbl, 0, sizeof(nersary_mark_tbl) );
  memset( tenured_mark_tbl, 0, sizeof(tenured_mark_tbl) );
}

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
  printf("major GC start ...");
  printf("remembered_set_top: %d  ", remembered_set_top);
#endif
  //initialization.
  mark_stack_top = 0;
  remembered_set_top = 0;

  //mark phase.
  mark();
#if defined( _DEBUG )
  printf("[mark done] ");
#endif

  //compaction phase.
  compact();
#if defined( _DEBUG )
  printf("[compact done] ");
  printf("remembered_set_top: %d  ", remembered_set_top);
  printf("major GC end\n");
#endif
}

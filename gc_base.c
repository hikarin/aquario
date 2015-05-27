#if !defined( __GC_BASE_H__ )
#define __GC_BASE_H__

#include <stdlib.h>
#include "aquario.h"
#include "gc_base.h"
#include "gc_copy.h"
#include "gc_markcompact.h"
#include "gc_reference_count.h"
#include "gc_generational.h"

static void gc_write_barrier_default(Cell obj, Cell* cellp, Cell cell);     //write barrier;
static void gc_init_ptr_default(Cell* cellp, Cell cell);          //init pointer;
static void gc_memcpy_default(char* dst, char* src, size_t size); //memcpy;
#if defined( _DEBUG )
static void gc_stack_check_default(Cell* obj);
static int total_malloc_size;
#endif //_DEBUG

//definitions of Garbage Collectors' name.
#define GC_STR_COPYING      "copy"
#define GC_STR_MARKCOMPACT  "mc"
#define GC_STR_GENERATIONAL "gen"

void gc_init(const char* gc_char, GC_Init_Info* gc_init)
{
#if defined( _DEBUG )
  total_malloc_size = 0;
#endif
  if( strcmp( gc_char, GC_STR_COPYING ) == 0 ){
    gc_init_copy(gc_init);
  }else if( strcmp( gc_char, GC_STR_MARKCOMPACT ) == 0 ){
    gc_init_markcompact(gc_init);
  }else if( strcmp( gc_char, GC_STR_GENERATIONAL) == 0 ){
    gc_init_generational(gc_init);
  }else{
    //default.
    gc_init_generational(gc_init);
  }
  if(!gc_init->gc_write_barrier){
    //option.
    gc_init->gc_write_barrier = gc_write_barrier_default;
  }
  if(!gc_init->gc_init_ptr){
    //option.
    gc_init->gc_init_ptr = gc_init_ptr_default;
  }
  if(!gc_init->gc_memcpy){
    //option.
    gc_init->gc_memcpy = gc_memcpy_default;
  }
#if defined( _DEBUG )
  if(!gc_init->gc_stack_check){
    gc_init->gc_stack_check = gc_stack_check_default;
  }
#endif //_DEBUG
}

void trace_roots(void (*trace) (Cell* cellp)){
  //trace machine stack.
  int scan = stack_top;
  while( scan > 0 ){
    Cell* cellp = stack[ --scan ];
    trace( cellp );
  }

  //trace global variable.
  trace( &NIL );
  trace( &T );
  trace( &F );
  trace( &UNDEF );
  trace( &EOFobj );

  //trace return value.
  trace( &retReg );

  //trace env.
  int i;
  for( i=0; i<ENVSIZE; i++ ){
    if( env[i] ){
      trace( &env[i] );
    }
  }
}

void trace_object( Cell cell, void (*trace) (Cell* cellp)){
  if( cell ){
    switch(type(cell)){
    case T_NONE:
      break;
    case T_CHAR:
      break;
    case T_STRING:
      break;
    case T_INTEGER:
      break;
    case T_PAIR:
      trace(&(car(cell)));
      trace(&(cdr(cell)));
      break;
    case T_PROC:
      break;
    case T_SYNTAX:
      break;
    case T_SYMBOL:
      break;
    case T_LAMBDA:
      trace(&(lambdaparam(cell)));
      trace(&(lambdaexp(cell)));
      break;
    default:
      printf("Object Corrupted.\n");
      printf("%d\n", type(cell));
      exit(-1);
    }
  }
}

Boolean trace_object_bool(Cell cell, Boolean (*trace) (Cell* cellp)){
  if( cell ){
    switch(type(cell)){
    case T_NONE:
      break;
    case T_CHAR:
      break;
    case T_STRING:
      break;
    case T_INTEGER:
      break;
    case T_PAIR:
      if(trace(&(car(cell)))){
	return TRUE;
      }
      if(trace(&(cdr(cell)))){
	return TRUE;
      }
      break;
    case T_PROC:
      break;
    case T_SYNTAX:
      break;
    case T_SYMBOL:
      break;
    case T_LAMBDA:
      if(trace(&(lambdaparam(cell)))){
	return TRUE;
      }
      if(trace(&(lambdaexp(cell)))){
	return TRUE;
      }
      break;
    default:
      printf("Object Corrupted.\n");
      printf("%d\n", type(cell));
      exit(-1);
    }
  }

  return FALSE;
}

void gc_write_barrier_default(Cell obj, Cell* cellp, Cell cell)
{
  *cellp = cell;
}

void gc_init_ptr_default(Cell* cellp, Cell cell)
{
  *cellp = cell;
}

void gc_memcpy_default(char* dst, char* src, size_t size)
{
  memcpy( dst, src, size );
}

void* aq_malloc(size_t size)
{
#if defined( _DEBUG )
  total_malloc_size += size;
#endif
  return malloc(size);
}

void  aq_free(void* p)
{
  free(p);
}

#if defined( _DEBUG )
size_t get_total_malloc_size()
{
  return total_malloc_size;
}

void gc_stack_check_default(Cell* cell)
{
  //Do nothing.
}
#endif //_DEBUG
#endif	//!__GC_BASE_H__

#if !defined( __GC_BASE_H__ )
#define __GC_BASE_H__

#include <stdlib.h>
#include "aquario.h"
#include "gc_base.h"
#include "gc_copy.h"
#include "gc_markcompact.h"
#include "gc_reference_count.h"

static void gc_write_barrier_default(Cell* cellp, Cell cell);     //write barrier;
static void gc_init_ptr_default(Cell* cellp, Cell cell);          //init pointer;
static void gc_memcpy_default(char* dst, char* src, size_t size); //memcpy;
#if defined( _DEBUG )
static void gc_stack_check_default(Cell obj);
#endif //_DEBUG

void gc_init(const char* gc_char, GC_Init_Info* gc_init)
{
  if( strcmp( gc_char, "copying" ) == 0 ){
    gc_init_copy(gc_init);
    printf("Garbage Collector: copying\n");
  }else if( strcmp( gc_char, "mark_compact" ) == 0 ){
    gc_init_markcompact(gc_init);
    printf("Garbage Collector: mark_compact\n");
  }else if( strcmp( gc_char, "reference_count" ) == 0 ){
    gc_init_reference_count(gc_init);
    printf("Garbage Collector: reference_count\n");
  }else{
    gc_init_reference_count(gc_init);
    printf("Garbage Collector: reference_count\n");
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
    Cell* cellp = &stack[ --scan ];
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
    trace( &env[i] );
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

void gc_write_barrier_default(Cell* cellp, Cell cell)
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

#if defined( _DEBUG )
void gc_stack_check_default(Cell cell)
{
  //Do nothing.
}
#endif //_DEBUG
#endif	//!__GC_BASE_H__

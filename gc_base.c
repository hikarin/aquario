#if !defined( __GC_BASE_H__ )
#define __GC_BASE_H__

#include <stdlib.h>
#include "aquario.h"
#include "gc_base.h"
#include "gc_copy.h"
#include "gc_markcompact.h"
#include "gc_reference_count.h"
#include "gc_generational.h"

static void gc_write_barrier_default(Cell obj, Cell* cellp, Cell cell);   //write barrier;
static void gc_write_barrier_root_default(Cell* cellp, Cell cell);        //write barrier;
static void gc_init_ptr_default(Cell* cellp, Cell cell);                  //init pointer;
static void gc_memcpy_default(char* dst, char* src, size_t size);         //memcpy;
#if defined( _DEBUG )
static void gc_stack_check_default(Cell* obj);
static int total_malloc_size;
#endif //_DEBUG

Cell* popArg_default();
void pushArg_default(Cell* cellp);

//definitions of Garbage Collectors' name.
#define GC_STR_COPYING         "copy"
#define GC_STR_MARKCOMPACT     "mc"
#define GC_STR_GENERATIONAL    "gen"
#define GC_STR_REFERENCE_COUNT "ref"

void gc_init(const char* gc_char, GC_Init_Info* gc_init)
{
#if defined( _DEBUG )
  total_malloc_size = 0;
#endif
  if( strcmp( gc_char, GC_STR_COPYING ) == 0 ){
    gc_init_copy(gc_init);
  }else if( strcmp( gc_char, GC_STR_MARKCOMPACT ) == 0 ){
    gc_init_markcompact(gc_init);
  }else if( strcmp( gc_char, GC_STR_GENERATIONAL ) == 0 ){
    gc_init_generational(gc_init);
  }else if( strcmp( gc_char, GC_STR_REFERENCE_COUNT ) == 0 ){
    gc_init_reference_count(gc_init);
  }else{
    //default.
    gc_init_generational(gc_init);
  }
  if(!gc_init->gc_write_barrier){
    //option.
    gc_init->gc_write_barrier = gc_write_barrier_default;
  }
  if(!gc_init->gc_write_barrier_root){
    //option.
    gc_init->gc_write_barrier_root = gc_write_barrier_root_default;
  }

  if(!gc_init->gc_init_ptr){
    //option.
    gc_init->gc_init_ptr = gc_init_ptr_default;
  }
  if(!gc_init->gc_memcpy){
    //option.
    gc_init->gc_memcpy = gc_memcpy_default;
  }

  if(!gc_init->gc_pushArg){
    //option.
    gc_init->gc_pushArg = pushArg_default;
  }
  if(!gc_init->gc_popArg){
    //option.
    gc_init->gc_popArg = popArg_default;
  }

#if defined( _DEBUG )
  if(!gc_init->gc_stack_check){
    gc_init->gc_stack_check = gc_stack_check_default;
  }
#endif //_DEBUG
}

Cell* popArg_default()
{
  Cell* c = stack[ --stack_top ];
#if defined( _DEBUG )
  if( stack_top < 0 ){
    printf( "OMG....stack underflow\n" );
  }
#endif //_DEBUG

#if defined( _DEBUG )
  //  gc_stack_check(c);  
#endif //_DEBUG
  return c;
}

void pushArg_default(Cell* cellp)
{
  if( stack_top >= STACKSIZE ){
    setParseError( "Stack Overflow" );
    return;
  }
#if defined( _DEBUG )
  //  gc_stack_check(c);
#endif //_DEBUG
  
  stack[stack_top++] = cellp;
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
      printf("trace_object: Object Corrupted(%p).\n", cell);
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
      printf("trace_object_bool: Object Corrupted(%p).\n", cell);
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

void gc_write_barrier_root_default(Cell* cellp, Cell cell)
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

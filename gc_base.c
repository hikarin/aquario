#if !defined( __GC_BASE_H__ )
#define __GC_BASE_H__

#include <stdlib.h>
#include "aquario.h"
#include "gc_base.h"
#include "gc_copy.h"
#include "gc_markcompact.h"
#include "gc_reference_count.h"
#include "gc_generational.h"

void gc_init(const char* gc_char, GC_Init_Info* gc_init)
{
  if( strcmp( gc_char, "copying" ) == 0 ){
    copy_gc_init(gc_init);
  }else if( strcmp( gc_char, "mark_compact" ) == 0 ){
    markcompact_gc_init(gc_init);
  }else if( strcmp( gc_char, "reference_count" ) == 0 ){
    reference_count_init(gc_init);
  }else if( strcmp( gc_char, "generational" ) == 0 ){
    generational_gc_init(gc_init);
  }else{
    reference_count_init(gc_init);
  }
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

#endif	//!__GC_BASE_H__

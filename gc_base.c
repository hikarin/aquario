#if !defined( __GC_BASE_H__ )
#define __GC_BASE_H__

#include "gc_base.h"
#include "aquario.h"
#include <stdlib.h>

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

#if !defined( __GC_BASE_H__ )
#define __GC_BASE_H__

#include <stdlib.h>
#include "../aquario.h"
#include "base.h"
#include "copy.h"
#include "markcompact.h"
#include "reference_count.h"
#include "generational.h"
#include "marksweep.h"

static void gc_write_barrier_default(Cell obj, Cell* cellp, Cell cell);   //write barrier;
static void gc_write_barrier_root_default(Cell* cellp, Cell cell);        //write barrier;
static void gc_init_ptr_default(Cell* cellp, Cell cell);                  //init pointer;
static void gc_memcpy_default(char* dst, char* src, size_t size);         //memcpy;

Cell* popArg_default();
void pushArg_default(Cell* cellp);

#if defined( _DEBUG )
static int total_malloc_size;
#endif

//definitions of Garbage Collectors' name.
#define GC_STR_COPYING         "copy"
#define GC_STR_MARKCOMPACT     "mc"
#define GC_STR_GENERATIONAL    "gen"
#define GC_STR_REFERENCE_COUNT "ref"
#define GC_STR_MARK_SWEEP      "ms"

static int heap_size = 0;

int get_heap_size()
{
  return heap_size;
}

void gc_init(char* gc_char, int h_size, GC_Init_Info* gc_init)
{
#if defined( _DEBUG )
  total_malloc_size = 0;
#endif
  heap_size = h_size;
  aq_heap = AQ_MALLOC(heap_size);
  if( strcmp( gc_char, GC_STR_COPYING ) == 0 ){
    gc_init_copy(gc_init);
  }else if( strcmp( gc_char, GC_STR_MARKCOMPACT ) == 0 ){
    gc_init_markcompact(gc_init);
  }else if( strcmp( gc_char, GC_STR_GENERATIONAL ) == 0 ){
    gc_init_generational(gc_init);
  }else if( strcmp( gc_char, GC_STR_REFERENCE_COUNT ) == 0 ){
    gc_init_reference_count(gc_init);
  }else if( strcmp( gc_char, GC_STR_MARK_SWEEP ) == 0 ){
    gc_init_marksweep(gc_init);
  }else{
    //default.
    gc_init_marksweep(gc_init);
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
}

void gc_term_base()
{
  AQ_FREE(aq_heap);
}

Cell* popArg_default()
{
  Cell* c = stack[ --stack_top ];
#if defined( _DEBUG )
  if( stack_top < 0 ){
    printError("Stack Underflow");
  }
#endif //_DEBUG

  return c;
}

void pushArg_default(Cell* cellp)
{
  if( stack_top >= STACKSIZE ){
    printError( "Stack Overflow" );
    return;
  }
  stack[stack_top++] = cellp;
}

void trace_roots(void (*trace) (Cell* cellp)){
  //trace machine stack.
  int scan = stack_top;
  while( scan > 0 ){
    Cell* cellp = stack[ --scan ];
    if(CELL_P(*cellp)){
      trace( cellp );
    }
  }

  //trace return value.
  if(CELL_P(retReg)){
    trace( &retReg );
  }

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
    case T_CHAR:
      break;
    case T_STRING:
      break;
    case T_PAIR:
      if( CELL_P(car(cell)) ){
	trace(&(car(cell)));
      }
      if( CELL_P(cdr(cell)) ){
	trace(&(cdr(cell)));
      }
      break;
    case T_PROC:
      break;
    case T_SYNTAX:
      break;
    case T_SYMBOL:
      break;
    case T_LAMBDA:
      if( CELL_P(lambdaparam(cell)) ){
	trace(&(lambdaparam(cell)));
      }
      if( CELL_P(lambdaexp(cell)) ){
	trace(&(lambdaexp(cell)));
      }
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
    case T_CHAR:
      break;
    case T_STRING:
      break;
    case T_PAIR:
      if( CELL_P(car(cell)) && trace(&(car(cell))) ){
	return TRUE;
      }
      if( CELL_P(cdr(cell)) && trace(&(cdr(cell))) ){
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
      if( CELL_P(lambdaparam(cell)) && trace(&(lambdaparam(cell))) ){
	return TRUE;
      }
      if( CELL_P(lambdaexp(cell)) && trace(&(lambdaexp(cell))) ){
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

inline Free_Chunk* aq_get_free_chunk( Free_Chunk** freelistp, size_t size )
{
  //returns a chunk which size is larger than required size.
  Free_Chunk** chunk = freelistp;
  Free_Chunk* ret = NULL;
  while(*chunk){
    Free_Chunk* tmp = *chunk;
    if(tmp->chunk_size >= size){
      //a chunk found.
      ret = tmp;
      if(tmp->chunk_size >= size + sizeof(Free_Chunk)){
	int chunk_size = tmp->chunk_size - size;
	Free_Chunk* next = tmp->next;
	tmp->chunk_size = size;
	
	tmp = (Free_Chunk*)((char*)tmp+size);
	tmp->chunk_size = chunk_size;
	tmp->next = next;
	*chunk = tmp;
      }else{
	*chunk = tmp->next;
      }
      break;
    }
    chunk = &(tmp->next);
  }

  return ret;
}

void put_chunk_to_freelist( Free_Chunk** freelistp, Free_Chunk* chunk, size_t size )
{
  Free_Chunk* freelist = *freelistp;
  if(!freelist){
    //No object in freelist.
    *freelistp        = chunk;
    chunk->chunk_size = size;
    chunk->next       = NULL;
  }else if( chunk < freelist ){
    if( (char*)chunk + size == (char*)freelist ){
      //Coalesce.
      chunk->next       = freelist->next;
      chunk->chunk_size = size + freelist->chunk_size;
    }else{
      chunk->next       = freelist;
      chunk->chunk_size = size;
    }
    *freelistp = chunk;
  }else{
    Free_Chunk* tmp = NULL;
    for( tmp = freelist; tmp->next; tmp = tmp->next ){
      if( (char*)tmp < (char*)chunk && (char*)chunk < (char*)tmp->next ){
	//Coalesce.
	if( (char*)tmp + tmp->chunk_size == (char*)chunk ){
	  if( (char*)chunk + size == (char*)tmp->next ){
	    //Coalesce with previous and next Free_Chunk.
	    tmp->chunk_size += (size + tmp->next->chunk_size);
	    tmp->next        = tmp->next->next;
	  }else{
	    //Coalesce with previous Free_Chunk.
	    tmp->chunk_size += size;
	  }
	}else if( (char*)chunk + size == (char*)tmp->next ){
	  //Coalesce with next Free_Chunk.
	  size_t new_size      = tmp->next->chunk_size + size;
	  Free_Chunk* new_next = tmp->next->next;
	  chunk->chunk_size  = new_size;
	  chunk->next        = new_next;
	  tmp->next            = chunk;
	}else{
	  //Just put obj into freelist.
	  chunk->chunk_size  = size;
	  chunk->next        = tmp->next;
	  tmp->next            = chunk;
	}
	return;
      }
    }
    tmp->next           = chunk;
    chunk->next       = NULL;
    chunk->chunk_size = size;
  }
}

void heap_exhausted_error()
{
  printError("Heap Exhausted");
  exit(-1);
}

#if defined( _DEBUG )
size_t get_total_malloc_size()
{
  return total_malloc_size;
}
#endif //_DEBUG
#endif	//!__GC_BASE_H__

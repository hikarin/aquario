#if !defined( __TYPES_H__ )
#define __TYPES_H__

#include <stdio.h>

typedef void (*opType)();
typedef enum type{
  T_CHAR,	//0.
  T_STRING,	//1.
  T_NONE,       //2
  T_INTEGER,	//3.
  T_PAIR,	//4.
  T_PROC,	//5.
  T_SYNTAX,	//6.
  T_SYMBOL,	//7.
  T_LAMBDA,	//8.
} Type;

#define AQ_FALSE  0
#define AQ_TRUE   2
#define AQ_NIL    4
#define AQ_UNDEF  6

typedef unsigned long VALUE;

#define AQ_IMMEDIATE_MASK 0x07
#define NIL_P(v)      ((VALUE)(v) == AQ_NIL)
#define TRUE_P(v)     ((VALUE)(v) == AQ_TRUE)
#define FALSE_P(v)    ((VALUE)(v) == AQ_FALSE)
#define CELL_P(v)     (((v) & AQ_IMMEDIATE_MASK) == 0)

typedef struct cell *Cell;
typedef union cellUnion
{
  char   _char;
  char   _string[1];
  int     _integer;
  struct{
    Cell  _car;
    Cell  _cdr;
  }       _cons;
  opType  _proc;
} CellUnion;
struct cell{
  Type _type;
  CellUnion _object;
};

Cell T, F, NIL, UNDEF, EOFobj;
Cell retReg;

typedef enum boolean{
  FALSE  = 0,
  TRUE   = 1,
}Boolean;

typedef struct gc_init_info{
  void* (*gc_malloc) (size_t);               //malloc function;
  void  (*gc_start) ();                      //gc function;
  void  (*gc_write_barrier) (Cell, Cell*, Cell);   //write barrier;
  void  (*gc_write_barrier_root) (Cell*, Cell);   //write barrier root;
  void  (*gc_init_ptr) (Cell*, Cell);        //init pointer;
  void  (*gc_memcpy) (char*, char*, size_t); //memcpy;
  void  (*gc_term) ();                       //terminate;
  void  (*gc_pushArg) (Cell* cellp);
  Cell* (*gc_popArg) ();
#if defined( _DEBUG )
  void (*gc_stack_check)(Cell*);              //stack check(option).
#endif //_DEBUG
}GC_Init_Info;

#endif	//!defined( __TYPES_H__ )

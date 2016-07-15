#if !defined( __TYPES_H__ )
#define __TYPES_H__

#include <stdio.h>

typedef void (*opType)();
typedef enum type{
  T_CHAR,	//0.
  T_STRING,	//1.
  T_PAIR,	//2.
  T_PROC,	//3.
  T_SYNTAX,	//4.
  T_SYMBOL,	//5.
  T_LAMBDA,	//6.
} Type;

#define AQ_FALSE  ((VALUE)0)
#define AQ_TRUE   ((VALUE)2)
#define AQ_UNDEF  ((VALUE)6)
#define AQ_EOF    ((VALUE)10)
#define AQ_NIL    ((VALUE)14)

typedef unsigned long VALUE;

#define AQ_IMMEDIATE_MASK    0x03
#define AQ_INTEGER_MASK      0x01
#define AQ_INT_MAX           (0x7FFFFFFF>>1)
#define AQ_INT_MIN           (-0x7FFFFFFF>>1)

#define NIL_P(v)      ((VALUE)(v) == AQ_NIL)
#define TRUE_P(v)     ((VALUE)(v) == AQ_TRUE)
#define FALSE_P(v)    ((VALUE)(v) == AQ_FALSE)
#define UNDEF_P(v)    ((VALUE)(v) == AQ_UNDEF)
#define EOF_P(v)      ((VALUE)(v) == AQ_EOF)
#define INTEGER_P(v)  ((VALUE)(v) & AQ_INTEGER_MASK)

#define CELL_P(v)     ((v) != NULL && (((VALUE)(v) & AQ_IMMEDIATE_MASK) == 0))

typedef struct cell *Cell;
typedef union cellUnion
{
  char   _char;
  char   _string[1];
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
}GC_Init_Info;

#endif	//!defined( __TYPES_H__ )

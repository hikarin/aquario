#if !defined( __ISTSP_H__ )
#define __ISTSP_H__

#include <stdio.h>
#include <stddef.h>

typedef enum boolean{
  FALSE  = 0,
  TRUE   = 1,
}Boolean;

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

typedef enum opcode{
  NOP   =  0,
  ADD   =  1,
  SUB   =  2,
  MUL   =  3,
  DIV   =  4,
  ADD1  =  5,
  SUB1  =  6,
  ADD2  =  7,
  SUB2  =  8,
  PRINT = 10,
  PUSH  = 20,
  POP   = 21,

  EQUAL = 22,
  LT    = 23,
  LTE   = 24,
  GT    = 25,
  GTE   = 26,

  JNEQ  = 31,
  JMP   = 32,

  LOAD = 40,
  RET  = 41,
  CONS = 42,
  CAR  = 43,
  CDR  = 44,

  PUSH_NIL   = 50,
  PUSH_TRUE  = 51,
  PUSH_FALSE = 52,


  SET   = 53,
  REF   = 54,
  FUNC  = 55,
  FUND  = 56,

  FUNCS = 57,
  SROT  = 58,

  PUSHS = 60,
  PUSH_SYM = 61,
  FUNDD = 62,

  EQ    = 70,
  
  HALT = 100,
}OPCODE;

#define AQ_FALSE  ((VALUE)0)
#define AQ_TRUE   ((VALUE)2)
#define AQ_NIL    ((VALUE)14)
#define AQ_UNDEF  ((VALUE)6)
#define AQ_SFRAME ((VALUE)10)

typedef unsigned long VALUE;

#define AQ_IMMEDIATE_MASK    0x03
#define AQ_INTEGER_MASK      0x01
#define AQ_INT_MAX           (0x7FFFFFFF>>1)
#define AQ_INT_MIN           (-0x7FFFFFFF>>1)

#define NIL_P(v)      ((VALUE)(v) == AQ_NIL)
#define TRUE_P(v)     ((VALUE)(v) == AQ_TRUE)
#define FALSE_P(v)    ((VALUE)(v) == AQ_FALSE)
#define UNDEF_P(v)    ((VALUE)(v) == AQ_UNDEF)
#define SFRAME_P(v)  ((VALUE)(v) == AQ_SFRAME)
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
  Boolean _flag;
  CellUnion _object;
};

typedef union _operand
{
  char _char;
  char* _string;
  Cell _num;
} Operand;

typedef struct _inst Inst;

struct _inst
{
  OPCODE op;
  Operand operand;
  Operand operand2;
  int offset;
  int size;
  struct _inst* prev;
  struct _inst* next;
};

typedef struct _instQueue
{
  Inst* head;
  Inst* tail;
}InstQueue;

typedef struct gc_init_info{
  void* (*gc_malloc) (size_t);               //malloc function;
  void  (*gc_start) ();                      //gc function;
  void  (*gc_write_barrier) (Cell, Cell*, Cell);   //write barrier;
  void  (*gc_write_barrier_root) (Cell*, Cell);   //write barrier root;
  void  (*gc_init_ptr) (Cell*, Cell);        //init pointer;
  void  (*gc_memcpy) (char*, char*, size_t); //memcpy;
  void  (*gc_term) ();                       //terminate;
  void  (*gc_pushArg) (Cell c);
  Cell (*gc_popArg) ();
}GC_Init_Info;

typedef enum errorType{
  ERR_TYPE_NONE = -1,
  ERR_TYPE_WRONG_NUMBER_ARG = 0,
  
  // compile error
  ERR_TYPE_MALFORMED_IF,
  ERR_TYPE_SYMBOL_LIST_NOT_GIVEN,
  ERR_TYPE_MALFORMED_DOT_LIST,
  ERR_TYPE_TOO_MANY_EXPRESSIONS,
  ERR_TYPE_EXTRA_CLOSE_PARENTHESIS,
  ERR_TYPE_SYMBOL_NOT_GIVEN,
  ERR_TYPE_SYNTAX_ERROR,
  ERR_TYPE_UNEXPECTED_TOKEN,

  // runtime error
  ERR_TYPE_PAIR_NOT_GIVEN,
  ERR_TYPE_INT_NOT_GIVEN,
  ERR_STACK_OVERFLOW,
  ERR_STACK_UNDERFLOW,
  ERR_UNDEFINED_SYMBOL,
  ERR_HEAP_EXHAUSTED,
  ERR_FILE_NOT_FOUND,

  ERR_TYPE_GENERAL_ERROR,
}ErrorType;

Boolean isError();
void handleError();
void set_error(ErrorType e);

#if defined( _TEST )
#define AQ_FPRINTF(x, ...)  (outbuf_index += sprintf(&outbuf[outbuf_index], __VA_ARGS__))
#define AQ_PRINTF(...)      AQ_FPRINTF(stdout, __VA_ARGS__)
#define AQ_FGETC(x)         aq_fgetc()
#define AQ_UNGETC           aq_ungetc
#else
#define AQ_FPRINTF          fprintf
#define AQ_PRINTF(...)      AQ_FPRINTF(stdout, __VA_ARGS__)
#define AQ_FGETC            fgetc
#define AQ_UNGETC           ungetc
#endif

#define type(p)         ((p)->_type)
#define car(p)          ((p)->_object._cons._car)
#define cdr(p)          ((p)->_object._cons._cdr)
#define caar(p)         car(car(p))
#define cadr(p)         car(cdr(p))
#define cdar(p)         cdr(car(p))
#define cddr(p)         cdr(cdr(p))
#define cadar(p)        car(cdr(car(p)))
#define caddr(p)        car(cdr(cdr(p)))
#define cadaar(p)       car(cdr(car(car(p))))
#define cadddr(p)       car(cdr(cdr(cdr(p))))
#define cddddr(p)       cdr(cdr(cdr(cdr(p))))

#define chvalue(p)      ((p)->_object._char)
#define strvalue(p)     ((p)->_object._string)
#define ivalue(p)       (((int)(p))>>1)
#define procvalue(p)    ((p)->_object._proc)
#define syntaxvalue(p)  ((p)->_object._proc)
#define symbolname(p)   strvalue(p)
#define lambdaparam(p)  car(p)
#define lambdaexp(p)    cdr(p)
#define lambdaAddr(p)     (car(p))
#define lambdaParamNum(p) (cdr(p))
#define lambdaFlag(p)   ((p)->_flag)

Cell newCell(Type t, size_t size);

#define isChar(p)       (CELL_P(p) && (p)->_type==T_CHAR)
#define isString(p)     (CELL_P(p) && (p)->_type==T_STRING)
#define isInteger(p)    (INTEGER_P(p))
#define isPair(p)       (CELL_P(p) && (p)->_type==T_PAIR)
#define isSymbol(p)     (CELL_P(p) && (p)->_type==T_SYMBOL)
#define isProc(p)       (CELL_P(p) && (p)->_type==T_PROC)
#define isSyntax(p)     (CELL_P(p) && (p)->_type==T_SYNTAX)
#define isLambda(p)     (CELL_P(p) && (p)->_type==T_LAMBDA)

Cell charCell(char ch);
Cell stringCell(char* str);
Cell pairCell(Cell* a, Cell* d);
Cell symbolCell(char* name);
Cell lambdaCell(int addr, int paramNum, Boolean isDotList);
Cell makeInteger(int val);
  
Boolean isdigitstr(char* str);
Boolean nullp(Cell c);
Boolean truep(Cell c);
Boolean notp(Cell c);

void printPair(FILE* fp, Cell c);
void printCell(FILE* fp, Cell c);
void printLineCell(FILE* fp, Cell c);

Inst* createInst(OPCODE op, int size);
Inst* createInstChar(OPCODE op, char c);
Inst* createInstStr(OPCODE op, char* str);
Inst* createInstNum(OPCODE op, int num);
Inst* createInstToken(InstQueue* instQ, char* token);

void addInstHead(InstQueue* queue, Inst* inst);
void addInstTail(InstQueue* queue, Inst* inst);
size_t writeInst(Inst* inst, char* buf);
void addPushTail(InstQueue* instQ, int num);
void addOneByteInstTail(InstQueue* instQ, OPCODE op);

size_t compile(FILE* fp, char* buf, int offset);
void compileToken(InstQueue* instQ, char* token, Cell symbolList);
int compileList(InstQueue* instQ, FILE* fp, Cell symbolList);
void compileElem(InstQueue* instQ, FILE* fp, Cell symbolList);
void compileQuote(InstQueue* instQ, FILE* fp);
void compileQuotedAtom(InstQueue* instQ, char* symbol, FILE* fp);
void compileQuotedList(InstQueue* instQ, FILE* fp);

void compileAdd(InstQueue* instQ, int num);
void compileSub(InstQueue* instQ, int num);
void compileIf(InstQueue* instQ, FILE* fp, Cell symbolList);
void compileDefine(InstQueue* instQ, FILE* fp, Cell symbolList);
void compileLambda(InstQueue* instQ, FILE* fp);
void compileProcedure(char* func, int num, InstQueue* instQ);
void compileSymbolList(char* var, Cell* symbolList);

void execute(char* buf, int* start, int end);

#define ENVSIZE (3000)
Cell env[ENVSIZE];
#define LINESIZE (1024)

#define STACKSIZE (1024 * 1024)
Cell stack[ STACKSIZE ];
int stack_top;

int hash(char* key);
Cell getVar(char* name);
void setVar(char* name, Cell c);
void dupArg();
void clearArgs();
void callProc(char* name);
Cell getReturn();
void setReturn(Cell c);

void updateOffsetReg();
int getOffsetReg();

Boolean isEndInput(int c);

void repl();

#if defined( _WIN32 ) || defined( _WIN64 )
#define STRCPY(mem, str)	strcpy_s(mem, sizeof(char) * (strlen(str)+1), str)
#else
#define STRCPY(mem, str)	strcpy(mem, str)
#endif
#endif	//!defined( __ISTSP_H__ )

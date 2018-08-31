#if !defined( __ISTSP_H__ )
#define __ISTSP_H__

#include <stddef.h>
#include "types.h"

#if defined( _TEST )
#define AQ_PRINTF_GUIDE(x) (void)0
#else
#define AQ_PRINTF_GUIDE(x) printf(x)
#endif
#define AQ_PRINTF          printf
#define AQ_FPRINTF         fprintf

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

Cell newCell(Type t, size_t size);

#define isChar(p)       (CELL_P(p) && (p)->_type==T_CHAR)
#define isString(p)     (CELL_P(p) && (p)->_type==T_STRING)
#define isInteger(p)    (INTEGER_P(p))
#define isPair(p)       (CELL_P(p) && (p)->_type==T_PAIR)
#define isSymbol(p)     (CELL_P(p) && (p)->_type==T_SYMBOL)
#define isProc(p)       (CELL_P(p) && (p)->_type==T_PROC)
#define isSyntax(p)     (CELL_P(p) && (p)->_type==T_SYNTAX)
#define isLambda(p)     (CELL_P(p) && (p)->_type==T_LAMBDA)

#define PUSH_ARGS2(c1, c2)                \
  pushArg(c1);                            \
  pushArg(c2);

#define PUSH_ARGS3(c1, c2, c3)            \
  pushArg(c1);                            \
  pushArg(c2);                            \
  pushArg(c3);

#define PUSH_ARGS4(c1, c2, c3, c4)        \
  PUSH_ARGS2(c1, c2)                      \
  PUSH_ARGS2(c3, c4)

#define PUSH_ARGS5(c1, c2, c3, c4, c5)    \
  PUSH_ARGS2(c1, c2)                      \
  PUSH_ARGS3(c3, c4, c5)

#define POP_ARGS2()                       \
  popArg();                               \
  popArg();

#define POP_ARGS3()                       \
  popArg();                               \
  popArg();                               \
  popArg();

#define POP_ARGS4()                       \
  POP_ARGS2()                             \
  POP_ARGS2()

#define POP_ARGS5()                       \
  POP_ARGS2()                             \
  POP_ARGS3()

void clone(Cell c);
Cell charCell(char ch);
Cell stringCell(char* str);
Cell pairCell(Cell a, Cell d);
Cell procCell(opType proc);
Cell syntaxCell(opType syn);
Cell symbolCell(char* name);
Cell lambdaCell(int addr, int paramNum);
Cell makeInteger(int val);
  
int isdigitstr(char* str);
int nullp(Cell c);
int truep(Cell c);
int notp(Cell c);
int zerop(Cell c);
int eqdigitp(Cell c);
int length(Cell ls);
void applyList(Cell ls);

void printPair(Cell c);
void printCell(Cell c);
void printLineCell(Cell c);
char* readTokenInDQuot();
char* readToken();

Inst* createInst(OPCODE op, Cell operand, int size);
void addInstHead(InstQueue* queue, Inst* inst);
void addInstTail(InstQueue* queue, Inst* inst);
size_t writeInst(InstQueue* instQ, char* buf);
Inst* tokenToInst(char* token, Cell symbolList);

size_t compile(FILE* fp, char* buf);
int compileList(InstQueue* instQ, FILE* fp, Cell symbolList);
void compileQuot(InstQueue* instQ, FILE* fp, Cell symbolList);
void  compileElem(InstQueue* instQ, FILE* fp, Cell symbolList);

int execute(char* buf, int pc);

void printError(char *fmt, ...);
void setParseError(char* str);

#define ENVSIZE (3000)
Cell env[ENVSIZE];
#define LINESIZE (1024)

#define STACKSIZE (1024)
Cell* stack[ STACKSIZE ];
int stack_top;

int hash(char* key);
Cell getVar(char* name);
void setVarCell(Cell strCell, Cell c);
void setVar(char* name, Cell c);
void dupArg();
void clearArgs();
void callProc(char* name);
Cell getReturn();
void setReturn(Cell c);

void updateOffsetReg();
int getOffsetReg();

int repl();

int getCellSize(Cell cell);

#if defined( _WIN32 ) || defined( _WIN64 )
#define STRCPY(mem, str)	strcpy_s(mem, sizeof(char) * (strlen(str)+1), str)
#else
#define STRCPY(mem, str)	strcpy(mem, str)
#endif
#endif	//!defined( __ISTSP_H__ )

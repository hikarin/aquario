#if !defined( __ISTSP_H__ )
#define __ISTSP_H__

#include <stddef.h>
#include "types.h"

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
#define ivalue(p)       ((p)->_object._integer)
#define procvalue(p)    ((p)->_object._proc)
#define syntaxvalue(p)  ((p)->_object._proc)
#define symbolname(p)   strvalue(p)
#define lambdaparam(p)  car(p)
#define lambdaexp(p)    cdr(p)

Cell newCell(Type t, size_t size);

#define isNone(p)       ((p)->_type==T_NONE)
#define isChar(p)       ((p)->_type==T_CHAR)
#define isString(p)     ((p)->_type==T_STRING)
#define isInteger(p)    ((p)->_type==T_INTEGER)
#define isPair(p)       ((p)->_type==T_PAIR)
#define isSymbol(p)     ((p)->_type==T_SYMBOL)
#define isProc(p)       ((p)->_type==T_PROC)
#define isSyntax(p)     ((p)->_type==T_SYNTAX)
#define isLambda(p)     ((p)->_type==T_LAMBDA)

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
Cell intCell(int val);
Cell pairCell(Cell a, Cell d);
Cell procCell(opType proc);
Cell syntaxCell(opType syn);
Cell symbolCell(char* name);
Cell lambdaCell(Cell param, Cell exp);
Cell noneCell();

int isdigitstr(char* str);
int nullp(Cell c);
int truep(Cell c);
int notp(Cell c);
int zerop(Cell c);
int eqdigitp(Cell c);
int length(Cell ls);
//Cell setAppendCell(Cell ls, Cell c);
void setAppendCell(Cell ls, Cell c);
Cell setAppendList(Cell ls, Cell append);
Cell reverseList(Cell ls);
//Cell applyList(Cell ls);
void applyList(Cell ls);

void printPair(Cell c);
void printCell(Cell c);
void printLineCell(Cell c);
char* readTokenInDQuot();
char* readToken();
Cell readList();
Cell readQuot();
Cell tokenToCell();
Cell readElem();

Cell evalExp(Cell exp);
Boolean evalPair(Cell* pExp,Cell* pProc, Cell* pParams, Cell* pExps, Boolean is_loop);
void letParam(Cell exp, Cell dummyParams, Cell realParams);
Cell findParam(Cell exp, Cell dummyParams, Cell realParams);
void printError(char *fmt, ...);
void setParseError(char* str);

#define ENVSIZE 3000
Cell env[ENVSIZE];
#define LINESIZE 1024

#define STACKSIZE 1024
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

void op_nullp();
void op_notp();
void op_eofp();
void op_zerop();
void op_eqdigitp();
void op_lessdigitp();
void op_lessoreqdigitp();
void op_greaterdigitp();
void op_greateroreqdigitp();
void op_car();
void op_cdr();
void op_cons();
void op_add();
void op_sub();
void op_eval();
void op_read();
void op_print();
void op_load();
void op_eqp();
void op_gc();
void op_gc_stress();

void syntax_define();
void syntax_ifelse();
void syntax_lambda();
void syntax_quote();
void syntax_set();
void syntax_begin();

int repl();

int getCellSize(Cell cell);

#endif	//!defined( __ISTSP_H__ )

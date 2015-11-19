#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#include "aquario.h"
#include "gc/base.h"
#include "gc/copy.h"
#include "gc/markcompact.h"

static void set_gc(char*);

Boolean g_GC_stress;

static void* (*gc_malloc) (size_t size);
static void (*gc_start) ();
static void (*gc_write_barrier) (Cell cell, Cell* cellp, Cell newcell);
static void (*gc_init_ptr) (Cell* cellp, Cell newcell);
static void (*gc_memcpy) (char* dst, char* src, size_t size);
static void (*gc_term) ();
static void (*pushArg) (Cell* cellp);
static Cell* (*popArg) ();

static void (*gc_write_barrier_root) (Cell* srcp, Cell dst);

static Cell getChain(char* name, int* key);
static void registerVar(Cell nameCell, Cell chain, Cell c, Cell* env);
static Cell* getStackTop();

static void init();
static void term();

#define UNDEF_RETURN(x)         \
  if( UNDEF_P(x) ){             \
    setReturn((Cell)AQ_UNDEF);  \
    return;                     \
  }

#define WRONG_NUMBER_ARGUMENTS_ERROR(num, args, proc)	   \
  int argNum = length( args );                             \
  if(argNum != num){                                       \
    printError("wrong number of arguments for %s: should be %d but giben %d", proc, num, argNum); \
    setReturn((Cell)AQ_UNDEF);				   \
    return;                                                \
  }

inline int getCellSize(Cell cell)
{
  switch( type(cell) ){
  case T_STRING:
  case T_SYMBOL:
    return sizeof(struct cell) + strlen(strvalue(cell));
  default:
    return sizeof(struct cell);
  }
}

inline Cell newCell(Type t, size_t size)
{
  Cell new_cell = (Cell)gc_malloc(size);
  new_cell->_type = t;
  return new_cell;
}

Cell charCell(char ch)
{
  Cell c = newCell(T_CHAR, sizeof(struct cell));
  chvalue(c) = ch;
  return c;
}

Cell stringCell(char* str)
{
  int obj_size = sizeof(struct cell) + sizeof(char)*strlen(str) - sizeof(CellUnion)+1;
  Cell c = newCell(T_STRING, obj_size);
  strcpy(strvalue(c), str);
  return c;
}

Cell intCell(int val)
{
  Cell c = newCell(T_INTEGER, sizeof(struct cell));
  ivalue(c) = val;
  return c;
}

Cell pairCell(Cell a, Cell d)
{
  PUSH_ARGS2(&a, &d);
  Cell cons = newCell(T_PAIR, sizeof(struct cell));

  gc_init_ptr(&cdr(cons), d);
  gc_init_ptr(&car(cons), a);

  POP_ARGS2();
  return cons;
}

Cell procCell(opType proc)
{
  Cell c = newCell(T_PROC, sizeof(struct cell));
  procvalue(c) = proc;
  return c;
}

Cell syntaxCell(opType syntax)
{
  Cell c = newCell(T_SYNTAX, sizeof(struct cell));
  procvalue(c) = syntax;
  return c;
}

Cell symbolCell(char* symbol)
{
  int obj_size = sizeof(struct cell) + sizeof(char)*strlen(symbol)-sizeof(CellUnion)+1;
  Cell c = newCell(T_SYMBOL, obj_size);
  strcpy(symbolname(c), symbol);
  return c;
}

Cell lambdaCell(Cell param, Cell exp)
{
  PUSH_ARGS2(&param, &exp);
  Cell c = newCell(T_LAMBDA, sizeof(struct cell));
  gc_init_ptr( &lambdaexp(c), exp );
  gc_init_ptr( &lambdaparam(c), param );
  popArg();
  popArg();
  return c;
}

Cell noneCell()
{
  Cell c = newCell(T_NONE, sizeof(struct cell));
  return c;
}

void clone(Cell src)
{
  int size = getCellSize( src );
  pushArg(&src);
  Cell new = gc_malloc( size );
  popArg();
  gc_memcpy( (char*)new, (char*)src, size );
  setReturn(new);
}

void cloneTree(Cell src)
{
  if(isPair(src)){
    clone(src);
    Cell top = getReturn();
    pushArg(&top);
    if(isPair(car(top))){
      cloneTree(car(top));
      gc_write_barrier(top, &car(top), getReturn());
    }
    if(isPair(cdr(top))){
      cloneTree(cdr(top));
      gc_write_barrier(top, &cdr(top), getReturn());
    }

    setReturn(top);
    popArg();
  }
  else if(isNone(src)){
    setReturn(src);
  }
  else{
    clone(src);
  }
}

void cloneSymbolTree(Cell src)
{
  if(isPair(src)){
    clone(src);
    Cell top = getReturn();
    pushArg(&top);
    //clone car.
    if(isPair(car(top))){
      cloneSymbolTree(car(top));
      gc_write_barrier(top, &car(top), getReturn());
    }else if(isSymbol(car(top))){
      cloneSymbolTree(car(top));
      gc_write_barrier(top, &car(top), getReturn());
    }

    //clone cdr.
    if(isPair(cdr(top))){
      cloneSymbolTree(cdr(top));
      gc_write_barrier(top, &cdr(top), getReturn());
    }else if(isSymbol(cdr(top))){
      cloneSymbolTree(cdr(top));
      gc_write_barrier(top, &cdr(top), getReturn());
    }

    setReturn(top);
    popArg();
  }
  else if(isSymbol(src)){
    clone(src);
  }
  else{
    setReturn(src);
  }
}

Cell evalExp(Cell exp)
{
  pushArg(&exp);
  Cell exps = pairCell( exp, NIL);
  Cell params = NIL;
  Cell proc = NIL;
  PUSH_ARGS3(&proc, &params, &exps);
  // => [... exp proc params exps]

  Boolean is_loop = TRUE;
  for(;is_loop==TRUE;gc_write_barrier_root(stack[stack_top-1]/*exps*/, cdr(exps))){
    gc_write_barrier_root(stack[stack_top-4]/*exp*/, car(exps));
    if( nullp(cdr(exps) ) ){
      is_loop = FALSE;
    }
    if( type(exp) == T_SYMBOL ){
      if( !is_loop ){
	setReturn( getVar(symbolname(exp)) );
      }
    }else if( type(exp) == T_PAIR ){
      gc_write_barrier_root(stack[stack_top-2], evalExp(car(exp)));
      is_loop = evalPair(stack[stack_top-4],
			 stack[stack_top-3],
			 stack[stack_top-2],
			 stack[stack_top-1], is_loop);
    }else{
      if( !is_loop ){
	setReturn(exp);
      }
    }
  }
  POP_ARGS4();

  return getReturn();
}

Boolean evalPair(Cell* pExp,Cell* pProc, Cell* pParams, Cell* pExps, Boolean is_loop)
{
  // => [... exp proc params exps]
  gc_write_barrier_root(pProc, evalExp(car(*pExp)));
  Cell args = cdr(*pExp);
  opType operator;
  if( !CELL_P(*pProc) ){
    setParseError("not proc");
    setReturn((Cell)AQ_UNDEF);
    is_loop = FALSE;
  }else{
    switch(type(*pProc)){
    case T_PROC:
      operator = procvalue(*pProc);
      applyList(args);
      args = getReturn();
      pushArg(&args);                                   //=> [....exps args]
      operator();                                       //=> [....exps]
      break;
    case T_SYNTAX:
      operator = syntaxvalue(*pProc);
      pushArg(&args);                                   //=> [....exps args]
      operator();                                       //=> [....exps]
      break;
    case T_LAMBDA:
      {
	pushArg(&args);
	// => [... exp proc params exps args]
	gc_write_barrier_root(pParams, lambdaparam(*pProc));
	if( !is_loop ){
	  is_loop = TRUE;
	  gc_write_barrier_root(pExps, lambdaexp(*pProc));
	  if(length(args) != length(*pParams)){
	    printf("wrong number arguments\n");
	    setReturn((Cell)AQ_UNDEF);
	    is_loop = FALSE;
	  }else{
	    cloneTree(args);
	    gc_write_barrier_root(stack[stack_top-1]/*args*/, getReturn());
	    applyList(args);
	    gc_write_barrier_root(stack[stack_top-1]/*args*/, getReturn());
	    cloneSymbolTree(*pExps);
	    gc_write_barrier_root(pExps, getReturn());
	    letParam(*pExps, *pParams, args);
	    gc_write_barrier_root(pExps, pairCell(NIL, *pExps));
	    popArg();
	    // => [... exp proc params exps]
	    return is_loop;
	  }
	}else{
	  if(length(args) != length(*pParams)){
	    printf("wrong number arguments\n");
	    setReturn((Cell)AQ_UNDEF);
	    is_loop = FALSE;
	  }else{
	    cloneTree(args);
	    gc_write_barrier_root(stack[stack_top-1]/*args*/, getReturn());
	    applyList(args);
	    gc_write_barrier_root(stack[stack_top-1]/*args*/, getReturn());
	    
	    Cell tmps = lambdaexp(*pProc);
	    cloneSymbolTree(tmps);
	    tmps = getReturn();
	    letParam(tmps, *pParams, args);

	    type(*pExp) = type(tmps);
	    gc_write_barrier( *pExp, &car(*pExp), car(tmps) );
	    gc_write_barrier( *pExp, &cdr(*pExp), cdr(tmps) );
	    evalExp(*pExp);
	  }
	}
	popArg();
	// => [... exp proc params exps]
	break;
      }
    default:
      setParseError("not proc");
      setReturn((Cell)AQ_UNDEF);
      is_loop = FALSE;
    }
  }

  return is_loop;
}

void letParam(Cell exp, Cell dummyParams, Cell realParams)
{
  if(nullp(exp)) return;
  else if(isPair(exp)){
    Cell carCell = car(exp);
    if(isPair(carCell)){
      letParam(carCell, dummyParams, realParams);
    }
    else if(isSymbol(carCell)){
      Cell find = findParam(carCell, dummyParams, realParams);
      if(!UNDEF_P(find)){
	gc_write_barrier( exp, &car(exp), find );
      }
    }

    Cell cdrCell = cdr(exp);
    if(isPair(cdrCell)){
      letParam(cdrCell, dummyParams, realParams);
    }
    else if(isSymbol(cdrCell)){
      Cell find = findParam(cdrCell, dummyParams, realParams);
      if(!UNDEF_P(find)){
        gc_write_barrier( exp, &cdr(exp), find );
      }
    }
  }
}

Cell findParam(Cell exp, Cell dummyParams, Cell realParams)
{
  char *var = symbolname(exp);
  while(!nullp(dummyParams)){
    char *key = strvalue(car(dummyParams));
    if(strcmp(var, key)==0){
      return car(realParams);
    }
    dummyParams = cdr(dummyParams);
    realParams = cdr(realParams);
  }
  return (Cell)AQ_UNDEF;
}

int isdigitstr(char* str)
{
  int i;
  for(i=0;i<strlen(str);++i){
    if(!isdigit(str[i])){
      if(strlen(str) < 2 || i!=0 ||
	 (str[0] != '-' && str[0] != '+')) return 0;
    }
  }
  return 1;
}

int nullp(Cell c)
{
  return c==NIL?1:0;
}

int truep(Cell c)
{
  return !( nullp(c) || notp(c) )?1:0;
}

int notp(Cell c)
{
  return c==F?1:0;
}

int length(Cell ls)
{
  int length = 0;
  for(;!nullp(ls) && ls != NULL;ls=cdr(ls)){
    ++length;
  }
  return length;
}

void setAppendCell(Cell ls, Cell c)
{
  if(nullp(ls)){
    if(nullp(c)){
      setReturn(ls);
      return;
    }
    else{
      setReturn(pairCell(c, NIL));
      return;
    }
  }
  Cell cdr = ls;
  while(!nullp(cdr(cdr))){
    cdr = cdr(cdr);
  }

  PUSH_ARGS3(&c, &ls, &cdr)

  Cell tmp = pairCell(c, NIL);
  gc_write_barrier( cdr, &cdr(cdr), tmp );

  setReturn(ls);

  POP_ARGS3();
}

Cell setAppendList(Cell ls, Cell append)
{
  if(nullp(ls)){
    return append;
  }
  Cell cdr = ls;
  while(!nullp(cdr(cdr))){
    cdr = cdr(cdr);
  }
  gc_write_barrier( cdr, &cdr(cdr), append );
  return ls;
}

void applyList(Cell ls)
{
  if(nullp(ls) || UNDEF_P(ls)){
    setReturn(ls);
    return;
  }
  pushArg(&ls);
  Cell c = evalExp(car(ls));
  if(UNDEF_P(c)){
    setReturn(c);
    popArg();
    return;
  }
  Cell top = pairCell(c, NIL);
  Cell last = top;

  PUSH_ARGS2(&top, &last);
  while( !nullp(cdr(ls)) ){
    Cell exp = evalExp(car(cdr(ls)));
    if(UNDEF_P(exp)){
      gc_write_barrier_root(stack[stack_top-2]/*top*/, (Cell)AQ_UNDEF);
      break;
    }

    gc_write_barrier(last, &cdr(last), pairCell(exp, NIL));
    gc_write_barrier_root(stack[stack_top-3]/*ls*/,   cdr(ls));
    gc_write_barrier_root(stack[stack_top-1]/*last*/, cdr(last));
  }

  setReturn(top);
  POP_ARGS3();
}

void printCons(Cell c)
{
  printf("(");
  while(isPair(cdr(c))){
    printCell(car(c));
    printf(" ");
    c = cdr(c);
  }
  printCell(car(c));
  if(!nullp(cdr(c))){
    printf(" . ");
    printCell(cdr(c));
  }
  printf(")");
}

void printLineCell(Cell c)
{
  printCell(c);
  putchar('\n');
}

void printCell(Cell c)
{
  if(!CELL_P(c)){
    if(UNDEF_P(c)){
      printf("#undef");
    }
    else if(EOF_P(c)){
      printf("#<eof>");
    }    
  }else{
    switch(type(c)){
    case T_NONE:
      if(c==T){
	printf("#t");
      }
      else if(c==F){
	printf("#f");
      }
      else if(c==NIL){
	printf("()");
      }
      else{
	setParseError("unknown cell");
      }
      break;
    case T_CHAR:
      printf("#\\%c", chvalue(c));
      break;
    case T_STRING:
      printf("\"%s\"", strvalue(c));
      break;
    case T_INTEGER:
      printf("%d", ivalue(c));
      break;
    case T_PROC:
      printf("#proc");
      break;
    case T_SYNTAX:
      printf("#syntax");
      break;
    case T_SYMBOL:
      printf("%s", symbolname(c));
      break;
    case T_PAIR:
      printCons(c);
      break;
    case T_LAMBDA:
      printf("#closure");
      break;
    default:
      fputs("\nunknown cell", stderr);
      break;
    }
  }
}

char* readTokenInDQuot(char* buf, int len, FILE* fp)
{
  int prev = EOF;
  char *strp = buf;
  *strp = '"';
  for(++strp;(strp-buf)<len-1;++strp){
    int c = fgetc(fp);
    switch(c){
    case '"':
      if(prev!='\\'){
	*strp = c;
	goto BreakLoop;
      }
      else{
	*strp = c;
	break;
      }
    case EOF:
      printError("End Of File ...");
      return NULL;
    default:
      *strp = c;
      prev = c;
      break;
    }
  }
 BreakLoop:
  *strp = '\0';
  return buf;
}

char* readToken(char *buf, int len, FILE* fp)
{
  char *token = buf;
  for(;(token-buf)<len-1;){
    int c = fgetc(fp);
    switch(c){
    case '(':
    case ')':
    case '\'':
      if(token-buf > 0){
	ungetc(c, fp);
      }
      else{
	*token = c;
	++token;
      }
      *token = '\0';
      return buf;
    case '"':
      if(token-buf > 0){
	ungetc(c, fp);
	*token = '\0';
	return buf;
      }
      return readTokenInDQuot(buf, len, fp);
    case ' ':
    case '\t':
    case '\n':
      if(token-buf > 0){
	*token = '\0';
	return buf;
      }
      break;
    case EOF:
      return NULL;
    default:
      *token = c;
      ++token;
      break;
    }
  }
  *token = '\0';
  return buf;
}

Cell readList(FILE* fp)
{
  Cell list = NIL;
  char c;
  char buf[LINESIZE];
  while(1){
    Cell exp = NIL;
    c = fgetc(fp);
    switch(c){
    case ')':
      return list;
    case '.':
      exp = readElem(fp);
      list = setAppendList(list, exp);
      readToken(buf, sizeof(buf), fp);
      if(strcmp(buf, ")")!=0){
	printError("unknown token '%s' after '.'", buf);
	return NULL;
      }
      return list;
    case ' ':
    case '\n':
    case '\t':
      continue;
    case EOF:
      printError("EOF");
      return NULL;
    default:
      ungetc(c, fp);
      PUSH_ARGS2(&list, &exp);
      // => [... list exp]
      gc_write_barrier_root(stack[stack_top-1]/*exp*/, readElem(fp));
      setAppendCell(list, exp);
      gc_write_barrier_root(stack[stack_top-2]/*list*/, getReturn());

      POP_ARGS2();
      break;
    }
  }
  return list;
}

Cell readQuot(FILE* fp)
{
  Cell elem = readElem(fp);
  pushArg(&elem);

  Cell symbol = symbolCell("quote");
  Cell symbolPair = pairCell(symbol, NIL);

  setAppendCell(symbolPair, elem);
  Cell quot = getReturn();
  popArg();

  return quot;
}

Cell tokenToCell(char* token)
{
  if(isdigitstr(token)){
    int digit = atoi(token);
    return intCell(digit);
  }
  else if(token[0] == '"'){
    return stringCell(token+1);
  }
  else if(token[0] == '#'){
    if(token[1] == '\\' && strlen(token)==3){
      return charCell(token[2]);
    }
    else{
      return symbolCell(token);
    }
  }
  else{
    return symbolCell(token);
  }
}

Cell readElem(FILE* fp)
{
  char buf[LINESIZE];
  char* token = readToken(buf, sizeof(buf), fp);
  Cell elem = NIL;
  if(token==NULL){
    return (Cell)AQ_EOF;
  }
  else if(token[0]=='('){
    return readList(fp);
  }
  else if(token[0]=='\''){
    return readQuot(fp);
  }
  else if(token[0]==')'){
    printError("extra close parensis");
    elem = (Cell)AQ_UNDEF;
  }
  else{
    elem = tokenToCell(token);
  }
#if defined( NOT_YET )
  if(elem==NULL){
    ErrorNo err = errorNumber;
    clearError();
    if(err==EOF_ERR){
      return (Cell)AQ_EOF;
    }
    else{
      return NULL;
    }
  }
#else
  return (Cell)AQ_EOF;
#endif
}

int hash(char* key)
{
  int val = 0;
  for(;*key!='\0';++key){
    val = val*256 + *key;
  }
  return val;
}

Cell getVar(char* name)
{
  int key = hash(name)%ENVSIZE;
  Cell chain = env[key];
  if(chain==NULL || nullp(chain)){
    printError("undefined symbol: %s\n", name);
    return (Cell)AQ_UNDEF;
  }
  while(strcmp(name, strvalue(caar(chain)))!=0){
    if(nullp(cdr(chain))){
      return (Cell)AQ_UNDEF;
    }
    chain = cdr(chain);
  }
  return cdar(chain);
}

void setVarCell(Cell strCell, Cell c)
{
  int key = 0;
  Cell chain = getChain(strvalue(strCell), &key);
  registerVar(strCell, chain, c, &env[key]);
}

void registerVar(Cell nameCell, Cell chain, Cell c, Cell* env)
{
  if(!nullp(chain)){
    pushArg(&chain);
    Cell pair = pairCell(nameCell, c);
    popArg();
    gc_write_barrier( chain, &car(chain), pair );
  }
  else{
    Cell entry = pairCell(nameCell, c);
    gc_write_barrier_root(env, pairCell(entry, *env));
  }
}

Cell* getStackTop()
{
  return stack[ stack_top-1 ];
}

Cell getChain(char* name, int* key)
{
  *key = hash(name)%ENVSIZE;
  Cell chain = env[*key];
  if(env[*key]==NULL){
    chain = NIL;
    env[*key] = NIL;
  }
  while(!nullp(chain) && strcmp(name, strvalue(caar(chain)))!=0){
    chain = cdr(chain);
  }
  return chain;
}

void setVar(char* name, Cell c)
{
  int key = 0;
  pushArg(&c);
  Cell nameCell = stringCell(name);
  Cell chain = getChain(name, &key);
  registerVar(nameCell, chain, c, &env[key]);
  popArg();
}

void dupArg()
{
  Cell* c = getStackTop();
  pushArg(c);
}

void clearArgs()
{
  stack_top = 0;
}

void callProc(char* name)
{
  Cell proc = getVar(name);
  if(isProc(proc)){
    opType op = procvalue(proc);
    op();
  }
  else{
    printError("unknown proc");
  }
}

Cell getReturn()
{
  return retReg;
}

void setReturn(Cell c)
{
    gc_write_barrier_root( &retReg, c );
}

void setParseError(char* str)
{
}

void init()
{
  gc_init_ptr( &NIL, noneCell() );
  gc_init_ptr( &T, noneCell() );
  gc_init_ptr( &F, noneCell() );
  
  gc_init_ptr( &retReg, NIL );

  memset(env, 0, ENVSIZE);
  
  memset(stack, 0, STACKSIZE);
  stack_top = 0;

  setVar("nil", NIL);
  setVar("#t", T);
  setVar("#f", F);
  
  setVar("=",       procCell(op_eqdigitp));
  setVar("<",       procCell(op_lessdigitp));
  setVar("car",     procCell(op_car));
  setVar("cdr",     procCell(op_cdr));
  setVar("cons",    procCell(op_cons));
  setVar("+",       procCell(op_add));
  setVar("-",       procCell(op_sub));
  setVar("eval",    procCell(op_eval));
  setVar("read",    procCell(op_read));
  setVar("print",   procCell(op_print));
  setVar("load",    procCell(op_load));
  setVar("eq?",     procCell(op_eqp));
  setVar("gc",      procCell(op_gc));
  setVar("gc-stress", procCell(op_gc_stress));
  
  setVar("define",  syntaxCell(syntax_define));
  setVar("if",      syntaxCell(syntax_ifelse));
  setVar("lambda",  syntaxCell(syntax_lambda));
  setVar("quote",   syntaxCell(syntax_quote));
  setVar("set!",    syntaxCell(syntax_set));
}

void term()
{
  gc_term();
}

void op_gc()
{
  popArg();
  gc_start();
  setReturn(T);
}

void op_gc_stress()
{
  popArg();
  g_GC_stress = TRUE;
  setReturn(T);
}

void set_gc(char* gc_char)
{
  GC_Init_Info gc_info;
  memset(&gc_info, 0, sizeof(GC_Init_Info));
  gc_init( gc_char, &gc_info );
  
  gc_malloc        = gc_info.gc_malloc;
  gc_start         = gc_info.gc_start;
  gc_write_barrier = gc_info.gc_write_barrier;
  gc_write_barrier_root = gc_info.gc_write_barrier_root;
  gc_init_ptr      = gc_info.gc_init_ptr;
  gc_memcpy        = gc_info.gc_memcpy;
  gc_term          = gc_info.gc_term;
  pushArg          = gc_info.gc_pushArg;
  popArg           = gc_info.gc_popArg;
  
  g_GC_stress      = FALSE;
}

//equal.
void op_eqdigitp()
{
  Cell* args = getStackTop();
  if( UNDEF_P( *args ) ){
    setReturn( (Cell)AQ_UNDEF );
    popArg();
    return;
  }
  int i1 = ivalue(evalExp(car(*args)));
  int i2 = ivalue(evalExp(cadr(*args)));
  if(i1==i2){
    setReturn(T);
  }
  else{
    setReturn(F);
  }

  popArg();
}

//less than.
void op_lessdigitp()
{
  Cell* args = getStackTop();
  int i1 = ivalue(evalExp(car(*args)));
  int i2 = ivalue(evalExp(cadr(*args)));
  if( i1 < i2 ){
    setReturn(T);
  }else{
    setReturn(F);
  }

  popArg();
}

void op_car()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);
  WRONG_NUMBER_ARGUMENTS_ERROR(1, *args, "op_car");

  Cell* c1 = &car(*args);
  if( type( *c1 ) != T_PAIR ){
    setParseError( "not a list given" );
    return;
  }
}

void op_cdr()
{
  Cell* args = popArg();
  WRONG_NUMBER_ARGUMENTS_ERROR(1, *args, "op_cdr");

  Cell* c1 = &car(*args);
  if( type( *c1 ) != T_PAIR ){
    setParseError( "not a list given" );
    return;
  }
}

void op_cons()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);
  WRONG_NUMBER_ARGUMENTS_ERROR(2, *args, "op_cons");

  Cell c1 = car(*args);
  Cell c2 = cadr(*args);
  if( UNDEF_P(c1) || UNDEF_P(c2) ){
    setReturn( (Cell)AQ_UNDEF );
    return;
  }
}

void op_add()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);

  int ans = 0;
  while( !nullp(*args) && CELL_P(*args)  ){
    if( UNDEF_P( car( *args ) ) ){
      setReturn((Cell)AQ_UNDEF);
      return;
    }else if( type( car( *args ) ) != T_INTEGER ){
      setParseError("not a number given\n");
      setReturn((Cell)AQ_UNDEF);
      return;
    }
    ans += ivalue(car(*args));
    args = &cdr(*args);
  }
  setReturn(intCell(ans));
}

void op_sub()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);
  int len = length ( * args );
  if( len <= 0 ){
    setParseError("procedure '-' requires at least one argument\n");
    setReturn((Cell)AQ_UNDEF);
    return;
  }

  int ans = 0;
  if( len == 1 ){
    //(- 1) => -1.
    ans = -ivalue(car(*args));
  }else{
    //(- 1 2 3) => -4.
    ans = ivalue(car(*args));
    args = &cdr(*args);
    while( !nullp(*args) && CELL_P(*args) ){
      if( UNDEF_P( car( *args ) ) ){
	setReturn((Cell)AQ_UNDEF);
	return;
      }else if( type( car( *args ) ) != T_INTEGER ){
	setParseError("not a number given\n");
	setReturn((Cell)AQ_UNDEF);
	return;
      }
      ans -= ivalue(car(*args));
      args = &cdr(*args);
    }
  }
  setReturn(intCell(ans));
}

void op_read()
{
  setReturn(readElem(stdin));
}

void op_eval()
{
#if defined( NOT_YET )
  Cell* args = getStackTop();
  setReturn(evalExp(car(*args)));
  if(errorNumber==PARSE_ERR){
    fprintf(stderr, "%s\n", errorString);
    setReturn((Cell)AQ_UNDEF);
  }
  clearError();
#endif
  popArg();
}

void op_print()
{
  Cell args = *popArg();
  while( !nullp(args) && CELL_P(args) ){
    Cell c = car(args);
    if(isString(c)){
      fputs(strvalue(c), stdout);
    }
    else{
      printCell(c);
    }

    if( CELL_P(args) ){
      args = cdr(args);
    }else{
      break;
    }
  }
  setReturn((Cell)AQ_UNDEF);
}

void load_file( const char* filename )
{
  FILE* fp = fopen(filename, "r");
  if( fp ){
    Cell cell = NULL;
    while( !EOF_P(cell = readElem(fp)) ){
      if(UNDEF_P(cell)){
	break;
      }
      evalExp(cell);
    }
    fclose(fp);
    setReturn(T);
  }else{
    printError("load: failed\n");
    setReturn((Cell)AQ_UNDEF);
  }
}

void op_load()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);
  int argNum = length(*args);
  if( argNum != 1 ){
    setParseError("wrong number of arguments for load");
    return;
  }

  Cell cell = car(*args);
  if( type(cell) == T_STRING ){
    load_file(strvalue(cell));
  }else{
    setParseError("string required.");
    setReturn((Cell)AQ_UNDEF);
  }
}

void op_eqp()
{
  Cell* args = popArg();  
  UNDEF_RETURN(*args);

  int argNum = length(*args);
  if( argNum != 2 ){
    setParseError("wrong number of arguments for eq?");
    return;
  }
  else if(car(*args) == cadr(*args)){
    setReturn(T);
  }
  else{
    setReturn(F);
  }
}

void syntax_define()
{
  Cell* args = popArg();

  int argNum = length(*args);
  if( argNum > 2 ){
    setParseError( "too many parameters for specital from DEFINE " );
    return;
  }else if( argNum < 2 ){
    setParseError( "too few parameter for special from DEFINE " );
    return;
  }
  Cell symbol = car(*args);
  if( type( symbol ) != T_SYMBOL ){
    setParseError( "not a symbol given" );
    return;
  }
  pushArg(&symbol);
  pushArg(args);
  Cell obj = cadr(*args);
  obj = evalExp(obj);
  if(!UNDEF_P(obj)){
    gc_write_barrier_root(stack[stack_top-2]/*symbol*/, car(*args));
    setVarCell(symbol, obj);
    setReturn(symbol);
  }else{
    setReturn((Cell)AQ_UNDEF);
  }

  POP_ARGS2();
}

void syntax_ifelse()
{
  Cell* args = getStackTop();

  int argNum = length(*args);
  if( argNum < 2 || 3 < argNum ){
    printError("wrong number of arguments for if");
    setReturn((Cell)AQ_UNDEF);
  }else{
    Cell cond = evalExp(car(*args));
    if(truep(cond)){
      Cell tpart = evalExp(cadr(*args));
      setReturn(tpart);
    }
    else{
      Cell fpart = cddr(*args);
      if(nullp(fpart)){
	setReturn(NIL);
      }
      else{
	fpart = evalExp(car(fpart));
	setReturn(fpart);
      }
    }
  }
  popArg();
}

void syntax_lambda()
{
  Cell args = *popArg();
  Cell params = car(args);
  Cell exps = cdr(args);
  Cell lambda = lambdaCell(params, exps);
  setReturn(lambda);
}

void syntax_quote()
{
  setReturn(car(*popArg()));
}

void syntax_set()
{  
  Cell* args = getStackTop();
  Cell c1 = car(*args);
  if( type(c1) != T_SYMBOL ){
    printError("not a variable given.");
    setReturn((Cell)AQ_UNDEF);
    return;
  }
  
  Cell dst = evalExp(cadr(*args));
  c1 = car(*args);

  pushArg(&dst);
  setVarCell(c1, dst);
  setReturn(dst);

  POP_ARGS2();
}

void printError(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[ERROR]");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

int repl()
{
  while(1){
    Cell ret;
    fputs("> ", stderr);
    clearArgs();
    callProc("read");
    ret = getReturn();
    if(EOF_P(ret)) break;
    if(UNDEF_P(ret)) continue;
    Cell pair = pairCell(ret, NIL);
    pushArg(&pair);

    callProc("eval");
    printLineCell(getReturn());
  }
  fputs("Good-bye\n", stdout);
  return 0;
}

int main(int argc, char *argv[])
{
  int i = 1;
  if( argc >= 3 && strcmp(argv[ 1 ], "-GC" ) == 0 ){
    set_gc(argv[ 2 ]);
    i += 2;
  }else{
    set_gc("");
  }
  init();
  if( i >= argc ){
    repl();
  }else{
    load_file( argv[ i ] );
  }
  term();
#if defined( _DEBUG )
  printf("Bye-bye\n");
#endif
  return 0;
}

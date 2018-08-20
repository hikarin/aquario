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

static Cell getChain(char* name, int* key);
static void registerVar(Cell nameCell, Cell chain, Cell c, Cell* env);
static Cell* getStackTop();

static void init();
static void term();

static int heap_size = HEAP_SIZE;

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
  switch( type(cell)){
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
  STRCPY(strvalue(c), str);
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
  STRCPY(symbolname(c), symbol);
  return c;
}

Cell lambdaCell(int addr, int paramNum)
{
  Cell l = newCell(T_LAMBDA, sizeof(struct cell));
  gc_init_ptr(&lambdaAddr(l), makeInteger(addr));
  gc_init_ptr(&lambdaParamNum(l), makeInteger(paramNum));

  return l;
}

Cell makeInteger(int val)
{
  long lval = val;  
  return (Cell)((lval << 1) | AQ_INTEGER_MASK);
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
#if false
  pushArg(&exp);
  Cell exps = pairCell( exp, (Cell)AQ_NIL);
  Cell params = (Cell)AQ_NIL;
  Cell proc = (Cell)AQ_NIL;
  PUSH_ARGS3(&proc, &params, &exps);
  // => [... exp proc params exps]

  Boolean is_loop = TRUE;
  for(;is_loop==TRUE;gc_write_barrier_root(stack[stack_top-1]/*exps*/, cdr(exps))){
    gc_write_barrier_root(stack[stack_top-4]/*exp*/, car(exps));
    if( nullp(cdr(exps) ) ){
      is_loop = FALSE;
    }
    if( isSymbol(exp) ){
      if( !is_loop ){
	setReturn( getVar(symbolname(exp)) );
      }
    }else if( isPair(exp) ){
      gc_write_barrier_root(stack[stack_top-2], evalExp(car(exp)));
      if( UNDEF_P( getReturn() ) ){
	break;
      }
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
#endif
  return getReturn();
}

Boolean evalPair(Cell* pExp,Cell* pProc, Cell* pParams, Cell* pExps, Boolean is_loop)
{
  // => [... exp proc params exps]
  gc_write_barrier_root(pProc, evalExp(car(*pExp)));
  Cell args = cdr(*pExp);
  opType operator;
  if( UNDEF_P( getReturn() ) ){
    return FALSE;
  }
  else if( !CELL_P(*pProc) ){
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
	  
	  if( NIL_P(*pParams) ) {
	    if( length(args) == 0 ) {
	      gc_write_barrier_root(pExps, pairCell((Cell)AQ_NIL, *pExps));
	      popArg();
	      // => [... exp proc params exps]
	      return is_loop;
	    }
	  }
	  else {
	    int paramNum = 1;
	    Cell tmpParams = cdr(*pParams);
	    while(isPair(tmpParams)) {
	      paramNum++;
	      tmpParams = cdr(tmpParams);
	    }
	    if(isSymbol(tmpParams)) {
	      // (lambda (a . b) (cons b a)) => cdr(*pParams): b
	      if(length(args)-1 >= paramNum) {
		return ApplyParams(args, stack_top, pExps, pParams, is_loop);
	      }
	    } else if( length(args) == paramNum ) {
	      return ApplyParams(args, stack_top, pExps, pParams, is_loop);
	    }
	  }
	  AQ_PRINTF("wrong number arguments\n");
	  setReturn((Cell)AQ_UNDEF);
	  is_loop = FALSE;
	} else {
	  if(length(args) != length(*pParams)) {
	    AQ_PRINTF("wrong number arguments\n");
	    setReturn((Cell)AQ_UNDEF);
	    is_loop = FALSE;
	  } else {
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
  while(!nullp(dummyParams) && !UNDEF_P(realParams)){
    if( isPair(dummyParams) ){
      char *key = strvalue(car(dummyParams));
      if(strcmp(var, key)==0){
	return car(realParams);
      }
      dummyParams = cdr(dummyParams);
      realParams = cdr(realParams);
    }else{
      char *key = strvalue(dummyParams);
      if(strcmp(var, key)==0){
	applyList(realParams);
	realParams = getReturn();
	Cell ret = pairCell(symbolCell("quote"),
		 pairCell(realParams, (Cell)AQ_NIL));
	return ret;
      }else{
	break;
      }
    }
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
  return NIL_P(c) ? TRUE:FALSE;
}

int truep(Cell c)
{
  return !( nullp(c) || notp(c) )?TRUE:FALSE;
}

int notp(Cell c)
{
  return FALSE_P(c)?TRUE:FALSE;
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
      setReturn(pairCell(c, (Cell)AQ_NIL));
      return;
    }
  }
  Cell cdr = ls;
  while(!nullp(cdr(cdr))){
    cdr = cdr(cdr);
  }

  PUSH_ARGS3(&c, &ls, &cdr)

  Cell tmp = pairCell(c, (Cell)AQ_NIL);
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
#if false
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
  Cell top = pairCell(c, (Cell)AQ_NIL);
  Cell last = top;

  PUSH_ARGS2(&top, &last);
  while( !nullp(cdr(ls)) ){
    if(!isPair(cdr(ls))){
      gc_write_barrier_root(stack[stack_top-2]/*top*/, (Cell)AQ_UNDEF);
      break;
    }
    Cell exp = evalExp(car(cdr(ls)));
    if(UNDEF_P(exp)){
      gc_write_barrier_root(stack[stack_top-2]/*top*/, (Cell)AQ_UNDEF);
      break;
    }
    Cell d = pairCell(exp, (Cell)AQ_NIL);
    gc_write_barrier(last, &cdr(last), d);
    gc_write_barrier_root(stack[stack_top-3]/*ls*/,   cdr(ls));
    gc_write_barrier_root(stack[stack_top-1]/*last*/, cdr(last));
  }

  setReturn(top);
  POP_ARGS3();
#endif
}

void printCons(Cell c)
{
  AQ_PRINTF("(");
  while(isPair(cdr(c))){
    printCell(car(c));
    c = cdr(c);
    if( isPair(c) && !nullp(car(c)) ){
      AQ_PRINTF(" ");
    }
  }

  printCell(car(c));
  if(!nullp(cdr(c))){
    AQ_PRINTF(" . ");
    printCell(cdr(c));
  }
  AQ_PRINTF(")");
}

void printLineCell(Cell c)
{
  printCell(c);
  AQ_PRINTF("\n");
}

void printCell(Cell c)
{
  if(!CELL_P(c)){
    if(UNDEF_P(c)){
      AQ_PRINTF("#undef");
    }
    else if(NIL_P(c)){
      AQ_PRINTF("()");
    }
    else if(EOF_P(c)){
      AQ_PRINTF("#<eof>");
    }
    else if(TRUE_P(c)){
      AQ_PRINTF("#t");
    }
    else if(FALSE_P(c)){
      AQ_PRINTF("#f");
    }
    else if(INTEGER_P(c)){
      AQ_PRINTF("%d", ivalue(c));
    }
  }else{
    switch(type(c)){
    case T_CHAR:
      AQ_PRINTF("#\\%c", chvalue(c));
      break;
    case T_STRING:
      AQ_PRINTF("\"%s\"", strvalue(c));
      break;
    case T_PROC:
      AQ_PRINTF("#proc");
      break;
    case T_SYNTAX:
      AQ_PRINTF("#syntax");
      break;
    case T_SYMBOL:
      AQ_PRINTF("%s", symbolname(c));
      break;
    case T_PAIR:
      printCons(c);
      break;
    case T_LAMBDA:
      AQ_PRINTF("#closure");
      break;
    default:
      AQ_FPRINTF(stderr, "\nunknown cell");
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
    case ';':
      while(c != '\n' && c != EOF ){
	c = fgetc(fp);
      }
      ungetc(c, fp);
      break;
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

int compileList(InstQueue* instQ, FILE* fp, Cell symbolList)
{
  char c;
  char buf[LINESIZE];
  int n = 0;
  while(1){
    c = fgetc(fp);
    switch(c){
    case ')':
      return n;
    case '.':
      readToken(buf, sizeof(buf), fp);
      if(strcmp(buf, ")")!=0){
	printError("unknown token '%s' after '.'", buf);
	return n;
      }
      return n;
    case ' ':
    case '\n':
    case '\t':
      continue;
    case EOF:
      printError("EOF");
      return n;
    default:
      {
	ungetc(c, fp);
	compileElem(instQ, fp, symbolList);
	n++;
      }
    }
  }
  return n;
}

Cell readList(FILE* fp)
{
  Cell list = (Cell)AQ_NIL;
  char c;
  char buf[LINESIZE];
  
  while(1){
    Cell exp = (Cell)AQ_NIL;
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

void compileQuot(InstQueue* instQ, FILE* fp)
{
  compileElem(instQ, fp, NULL);
}

Cell readQuot(FILE* fp)
{
  Cell elem = readElem(fp);
  pushArg(&elem);

  Cell symbol = symbolCell("quote");
  Cell symbolPair = pairCell(symbol, (Cell)AQ_NIL);

  setAppendCell(symbolPair, elem);
  Cell quot = getReturn();
  popArg();

  return quot;
}

Inst* createInst(OPCODE op, Cell operand, int size)
{
  Inst* result = (Inst*)malloc(sizeof(Inst));
  result->op = op;
  result->prev = NULL;
  result->next = NULL;
  result->operand = operand;
  result->operand2 = NULL;
  result->size = size;
  result->offset = 0;
  
  return result;
}

Inst* tokenToInst(char* token, Cell symbolList)
{
  if(isdigitstr(token)) {
    int digit = atoi(token);
    Inst* ret = createInst(PUSH, makeInteger(digit), 9);
    return ret;
  }
  else if(strcmp(token, "nil") == 0) {
    Inst* ret = createInst(PUSH_NIL, (Cell)AQ_NIL, 9);
    return ret;
  }  
  else if(token[0] == '"'){
    Inst* ret = createInst(PUSH, stringCell(token+1), 1);
    return ret;
  }
  else if(token[0] == '#'){
    if(token[1] == '\\' && strlen(token)==3){
      Inst* ret = createInst(PUSH, charCell(token[2]), 9);
      return ret;
    }
    else{
      Inst* ret = createInst(PUSH, symbolCell(token), 9);
      return ret;
    }
  } else {
    int index = 0;
    AQ_PRINTF("look for %s ... ", token);
    while(symbolList && !NIL_P(symbolList)) {
      char* symbol = symbolname(car(symbolList));
      if(strcmp(symbol, token) == 0) {
	AQ_PRINTF("found %s\n", symbol);
	Inst* ret = createInst(LOAD, makeInteger(index), 9);
	return ret;
      }
      
      symbolList = cdr(symbolList);
      index++;
    }
    AQ_PRINTF("not found\n");
    
    Inst* ret = createInst(REF, stringCell(token), 9);
    return ret;
  }
}

Cell tokenToCell(char* token)
{
  if(isdigitstr(token)){
    int digit = atoi(token);
    return makeInteger(digit);
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

void addInstHead(InstQueue* queue, Inst* inst)
{
  queue->head->prev = inst;
  inst->next = queue->head;

  queue->head = inst;
}

void addInstTail(InstQueue* queue, Inst* inst)
{
  inst->offset = queue->tail->offset + queue->tail->size;
  queue->tail->next = inst;
  inst->prev = queue->tail;

  queue->tail = inst;
}

void addPushTail(InstQueue* instQ, int num) {
  return addInstTail(instQ, createInst(PUSH, makeInteger(num), 9));
}

void addOneByteInstTail(InstQueue* instQ, OPCODE op)
{
  return addInstTail(instQ, createInst(op, (Cell)AQ_NIL, 1));
}

void compileProcedure(char* func, int num, InstQueue* instQ)
{
    if(strcmp(func, "+") == 0) {
      addPushTail(instQ, num);
      addOneByteInstTail(instQ, ADD);
    } else if(strcmp(func, "-") == 0) {
      addPushTail(instQ, num);
      addOneByteInstTail(instQ, SUB);
    } else if(strcmp(func, "*") == 0) {
      addPushTail(instQ, num);
      addOneByteInstTail(instQ, MUL);
    } else if(strcmp(func, "/") == 0) {
      addPushTail(instQ, num);
      addOneByteInstTail(instQ, DIV);
    } else if(strcmp(func, "print") == 0) {
      addOneByteInstTail(instQ, PRINT);
    } else if(strcmp(func, "cons") == 0) {
      addOneByteInstTail(instQ, CONS);
    } else if(strcmp(func, "car") == 0) {
      addOneByteInstTail(instQ, CAR);
    } else if(strcmp(func, "cdr") == 0) {
      addOneByteInstTail(instQ, CDR);
    } else if(strcmp(func, "quote") == 0) {
      addOneByteInstTail(instQ, QUOTE);
    } else if(strcmp(func, ">") == 0) {
      addOneByteInstTail(instQ, GT);
    } else if(strcmp(func, "<") == 0) {
      addOneByteInstTail(instQ, LT);
    } else if(strcmp(func, "=") == 0) {
      addOneByteInstTail(instQ, EQ);
    } else {
      addPushTail(instQ, num);
      int len = strlen(func)+1;
      addInstTail(instQ, createInst(FUNC, stringCell(func), len+1));
    }
}

void compileIf(InstQueue* instQ, FILE* fp)
{
  compileElem(instQ, fp, NULL);  // predicate
  
  Inst* jneqInst = createInst(JNEQ, makeInteger(0), 9);
  addInstTail(instQ, jneqInst);
  compileElem(instQ, fp, NULL);  // statement (TRUE)
  
  Inst* jmpInst = createInst(JMP, makeInteger(0), 9);
  addInstTail(instQ, jmpInst);
  jneqInst->operand = makeInteger(instQ->tail->offset + instQ->tail->size);
  
  int c = fgetc(fp);
  ungetc(c, fp);
  if(c ==')' ){
    addInstTail(instQ, createInst(PUSH_NIL, makeInteger(0), 1));
  } else {
    compileElem(instQ, fp, NULL);  // statement (FALSE)
  }
  
  jmpInst->operand = makeInteger(instQ->tail->offset + instQ->tail->size);

  char buf[LINESIZE];
  char* token = readToken(buf, sizeof(buf), fp);
  if(token[0] !=')' ){
    AQ_PRINTF("too many expressions\n");
  }
}

void compileLambda(InstQueue* instQ, FILE* fp)
{
  Inst* inst = createInst(FUND, (Cell)AQ_NIL, 17);
  addInstTail(instQ, inst);

  int c = fgetc(fp);
  if(c != '(') {
    AQ_PRINTF("symbol list is not goven\n");
    return;
  }
  
  int index = 0;
  Cell symbolList = (Cell)AQ_NIL;
  while((c = fgetc(fp)) != ')') {
    ungetc(c, fp);
    char buf[LINESIZE];
    char* var = readToken(buf, sizeof(buf), fp);
    Cell tmp  = pairCell(symbolCell(var), symbolList);
    symbolList = tmp;
    AQ_PRINTF("compileElem: %s\n", var);
    
    index++;
  }
  compileList(instQ, fp, symbolList);  // body
  addInstTail(instQ, createInst(RET, (Cell)AQ_NIL, 1));
  
  int addr = instQ->tail->offset + instQ->tail->size;
  inst->operand = makeInteger(addr);
  inst->operand2 = makeInteger(index);
}


void compileDefine(InstQueue* instQ, FILE* fp)
{
  char buf[LINESIZE];
  char* var = readToken(buf, sizeof(buf), fp);
  
  compileElem(instQ, fp, NULL);
  addInstTail(instQ, createInst(SET, stringCell(var), 1 + strlen(var) + 1));
  
  char* token = readToken(buf, sizeof(buf), fp);
  if(token[0] !=')' ){
    AQ_PRINTF("too many expressions\n");
  }
}

void compileElem(InstQueue* instQ, FILE* fp, Cell symbolList)
{
  char buf[LINESIZE];
  char* token = readToken(buf, sizeof(buf), fp);
  if(token==NULL){
    Inst* inst = createInst(HALT, (Cell)AQ_EOF, 1);
    addInstTail(instQ, inst);
    return;
  }
  else if(token[0]=='('){
    char* func = readToken(buf, sizeof(buf), fp);
    if(strcmp(func, ")") == 0) {
      addInstTail(instQ, createInst(PUSH_NIL, (Cell)AQ_EOF, 1));
    }else if(strcmp(func, "if") == 0) {
      compileIf(instQ, fp);
    } else if(strcmp(func, "define") == 0) {
      compileDefine(instQ, fp);
    } else if(strcmp(func, "lambda") == 0) {
      compileLambda(instQ, fp);
    } else {
      int num = compileList(instQ, fp, symbolList);
      compileProcedure(func, num, instQ);
    }
    
    return;
  }
  else if(token[0]=='\''){
    return;
  }
  else if(token[0]==')'){
    printError("extra close parensis");
    return;
  }
  else{
    Inst* inst = tokenToInst(token, symbolList);
    addInstTail(instQ, inst);
    return;
  }
}

Cell readElem(FILE* fp)
{
  char buf[LINESIZE];
  char* token = readToken(buf, sizeof(buf), fp);
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
    return (Cell)AQ_UNDEF;
  }
  else{
    return tokenToCell(token);
  }
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
    printError("undefined symbol: %s", name);
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
    //pushArg(&chain);
    Cell pair = pairCell(nameCell, c);
    //popArg();
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
    chain = (Cell)AQ_NIL;
    env[*key] = (Cell)AQ_NIL;
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

void updateOffsetReg()
{
  offsetReg = stack_top;
}

int getOffsetReg()
{
  return offsetReg;
}

void setParseError(char* str)
{
  printError(str);
}

void init()
{
  gc_init_ptr( &retReg, (Cell)AQ_NIL );

  memset(env, 0, ENVSIZE);
  
  memset(stack, 0, STACKSIZE);
  stack_top = 0;

  setVar("nil",     (Cell)AQ_NIL);
  setVar("#t",      (Cell)AQ_TRUE);
  setVar("#f",      (Cell)AQ_FALSE);
  setVar("=",       procCell(op_eqdigitp));
  setVar("<",       procCell(op_lessdigitp));
  setVar(">",       procCell(op_greaterdigitp));
  setVar("car",     procCell(op_car));
  setVar("cdr",     procCell(op_cdr));
  setVar("cons",    procCell(op_cons));
  setVar("+",       procCell(op_add));
  setVar("-",       procCell(op_sub));
  setVar("*",       procCell(op_mul));
  setVar("/",       procCell(op_div));
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
  gc_term_base();
}

void op_gc()
{
  popArg();
  gc_start();
  setReturn((Cell)AQ_TRUE);
}

void op_gc_stress()
{
  popArg();
  g_GC_stress = TRUE;
  setReturn((Cell)AQ_TRUE);
}

void set_gc(char* gc_char)
{
  GC_Init_Info gc_info;
  memset(&gc_info, 0, sizeof(GC_Init_Info));
  gc_init( gc_char, heap_size, &gc_info );
  g_GC_stress      = FALSE;
}

//equal.
void op_eqdigitp()
{
}

//less than.
void op_lessdigitp()
{
  Cell* args = getStackTop();
  UNDEF_RETURN(*args);
  WRONG_NUMBER_ARGUMENTS_ERROR(2, *args, "<");

  int i1 = ivalue(evalExp(car(*args)));
  int i2 = ivalue(evalExp(cadr(*args)));
  if( i1 < i2 ){
    setReturn((Cell)AQ_TRUE);
  }else{
    setReturn((Cell)AQ_FALSE);
  }

  popArg();
}

//greater than.
void op_greaterdigitp()
{
  Cell* args = getStackTop();
  UNDEF_RETURN(*args);
  WRONG_NUMBER_ARGUMENTS_ERROR(2, *args, ">");

  int i1 = ivalue(evalExp(car(*args)));
  int i2 = ivalue(evalExp(cadr(*args)));
  if( i1 > i2 ){
    setReturn((Cell)AQ_TRUE);
  }else{
    setReturn((Cell)AQ_FALSE);
  }

  popArg();
}

void op_car()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);
  WRONG_NUMBER_ARGUMENTS_ERROR(1, *args, "op_car");

  Cell* c1 = &car(*args);
  if( !isPair(*c1) ){
    setParseError( "not a list given" );
    setReturn( (Cell)AQ_UNDEF );
    return;
  }

  setReturn(car(*c1));
}

void op_cdr()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);
  WRONG_NUMBER_ARGUMENTS_ERROR(1, *args, "op_cdr");

  Cell* c1 = &car(*args);
  if( !isPair(*c1) ){
    setParseError( "not a list given" );
    setReturn( (Cell)AQ_UNDEF );
    return;
  }

  setReturn(cdr(*c1));
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

  setReturn( pairCell( c1, c2 ) );
}

void op_add()
{
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
    while( !nullp(*args) ){
      if( UNDEF_P( car( *args ) ) ){
	setReturn((Cell)AQ_UNDEF);
	return;
      }else if( !isInteger(car(*args)) ){
	setParseError("not a number given\n");
	setReturn((Cell)AQ_UNDEF);
	return;
      }
      ans -= ivalue(car(*args));
      args = &cdr(*args);
    }
  }
  setReturn(makeInteger(ans));
}

void op_mul()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);

  int ans = 1;
  while( !nullp(*args) ){
    if( UNDEF_P( car( *args ) ) ){
      setReturn((Cell)AQ_UNDEF);
      return;
    }else if( !isInteger(car(*args)) ){
      setParseError("not a number given\n");
      setReturn((Cell)AQ_UNDEF);
      return;
    }
    ans *= ivalue(car(*args));
    args = &cdr(*args);
  }
  setReturn(makeInteger(ans));
}

void op_div()
{
  Cell* args = popArg();
  UNDEF_RETURN(*args);
  int argNum = length( *args );
  if( argNum <= 0 ){
    setParseError("procedure '/' requires at least one argument\n");
    setReturn((Cell)AQ_UNDEF);
    return;
  }else if( argNum == 1 ){
    if( !isInteger(car(*args)) ){
      setParseError("not a number given\n");
      setReturn((Cell)AQ_UNDEF);
      return;
    }
    int ans = 1 / ivalue(car(*args));
    setReturn(makeInteger(ans));
  }else{
    int ans = ivalue(car(*args));
    args = &cdr(*args);
    while( !nullp(*args) ){
      if( UNDEF_P( car( *args ) ) ){
	setReturn((Cell)AQ_UNDEF);
	return;
      }else if( !isInteger(car(*args)) ){
	setParseError("not a number given\n");
	setReturn((Cell)AQ_UNDEF);
	return;
      }
      ans /= ivalue(car(*args));
      args = &cdr(*args);
    }
    setReturn(makeInteger(ans));
  }
}

void op_read()
{
  setReturn(readElem(stdin));
}

void op_eval()
{
  Cell* args = getStackTop();
  setReturn(evalExp(car(*args)));
  popArg();
}

void op_print()
{
  int ret = ivalue(getReturn());
  AQ_PRINTF("%x\n", ret);
}

void writeInst(InstQueue* instQ);

void load_file( const char* filename )
{
  FILE* fp = NULL;
#if defined( _WIN32 ) || defined( _WIN64 )
  fopen_s( &fp, filename, "r");
#else
  fp = fopen(filename, "r");
#endif
  InstQueue instQ;
  Inst inst;
  inst.op = NOP;
  inst.prev = NULL;
  inst.next = NULL;
  inst.offset = 0;
  inst.size = 1;
  instQ.head = &inst;
  instQ.tail = &inst;
  int c = 0;
  while((c = fgetc(fp)) != EOF) {
    ungetc(c, fp);
    compileElem(&instQ, fp, NULL);
  }
  execute(instQ.head);
  writeInst(&instQ);
}

void writeInst(InstQueue* instQ)
{
  FILE* fp = fopen("test.abc", "wb");
  char* buf = (char*)malloc(sizeof(char) * 1024);
  size_t size = 0;
  Inst* inst = instQ->head;
  while(inst) {
    buf[size] = (char)(inst->op);
    switch((char)inst->op) {
    case PUSH:
      {
	long val = ivalue(inst->operand);
	memcpy(&buf[++size], &val, sizeof(long));
	
	size += sizeof(long);
      }
      break;
    case JNEQ:
      {
	long val = ivalue(inst->operand);
	memcpy(&buf[++size], &val, sizeof(long));
	size += sizeof(long);
      }
      break;
    case JMP:
      {
	long val = ivalue(inst->operand);
	memcpy(&buf[++size], &val, sizeof(long));
	size += sizeof(long);
      }
      break;
    case SET:
      {
	char* str = strvalue(inst->operand);
	strcpy(&buf[++size], str);
	size += (strlen(str)+1);
      }
      break;
    case REF:
      {
	char* str = strvalue(inst->operand);
	strcpy(&buf[++size], str);
	size += (strlen(str)+1);
      }
      break;
    case FUNC:
      {
	char* str = strvalue(inst->operand);
	strcpy(&buf[++size], str);
	size += (strlen(str)+1);
      }
      break;
    case FUND:
      {
	int addr = ivalue(inst->operand);
	memcpy(&buf[++size], &addr, sizeof(long));
	size += sizeof(long);
	
	int paramNum = ivalue(inst->operand2);
	memcpy(&buf[size], &paramNum, sizeof(long));
	size += sizeof(long);
      }
      break;
    case LOAD:
      {
	int offset = ivalue(inst->operand);
	memcpy(&buf[++size], &offset, sizeof(long));
	size += sizeof(long);
      }
      break;
    case NOP:
    case ADD:
    case SUB:
    case MUL:
    case DIV:
    case PRINT:
    case POP:
    case CONS:
    case CAR:
    case CDR:
    case QUOTE:
    case PUSH_NIL:
    case PUSH_TRUE:
    case PUSH_FALSE:
    case GT:
    case LT:
    case HALT:
    case EQ:
    case RET:
      size += 1;
      break;
#if false
    case LT:
    case LTE:
    case GT:
    case GTE:
    case JEQ:
    case JEQB:
    case JNEQB:
    case JMPB:
      size += 1;
#endif
      break;
    }

    inst = inst->next;
  }
  fwrite(buf, size, 1, fp);
  fclose(fp);
}

void execute(Inst* top)
{
  Boolean exec = TRUE;
  stack_top = 0;
  Inst* inst = top;
  while(inst && exec != FALSE) {
    switch(inst->op) {
    case PUSH:
      {
	pushArg((Cell*)(inst->operand));
      }
      inst = inst->next;
      break;
    case PUSH_NIL:
      pushArg((Cell*)AQ_NIL);
      inst = inst->next;      
      break;
    case ADD:
      {
	Cell* numCell = popArg();
	Cell* val = NULL;
	int num = ivalue(numCell);
	long ans = 0;
	for(int i=0; i<num; i++) {
	  val = popArg();
	  ans += ivalue(val);
	}
	Cell* cellp = (Cell*)makeInteger(ans);
	pushArg(cellp);
      }
      inst = inst->next;
      break;
    case SUB:
      {
	int num = ivalue(popArg());
	if(num == 1) {
	  int ans = -ivalue(popArg());
	  Cell* cellp = (Cell*)makeInteger(ans);
	  pushArg(cellp);
	} else {
	  long ans = 0;
	  for(int i=0; i<num-1; i++) {
	    Cell* val = popArg();
	    ans -= ivalue(val);
	  }
	  ans += ivalue(popArg());
	  Cell* cellp = (Cell*)makeInteger(ans);
	  pushArg(cellp);
	}
      }
      inst = inst->next;
      break;
    case MUL:
      {
	int num = ivalue(popArg());
	long ans = 1;
	for(int i=0; i<num; i++) {
	  Cell* val = popArg();
	  ans *= ivalue(val);
	}
	Cell* cellp = (Cell*)makeInteger(ans);
	pushArg(cellp);
      }
      inst = inst->next;
      break;
    case DIV:
      {
	int num = ivalue(popArg());
	if(num == 1) {
	  long ans = 1/ivalue(popArg());
	  Cell* cellp = (Cell*)makeInteger(ans);
	  pushArg(cellp);	  
	} else {
	  int div = 1;
	  for(int i=0; i<num-1; i++) {
	    Cell* val = popArg();
	    div *= ivalue(val);
	  }
	  long ans = ivalue(popArg());
	  ans = ans/div;
	  Cell* cellp = (Cell*)makeInteger(ans);
	  pushArg(cellp);
	}
      }
      inst = inst->next;
      break;
    case RET:
      {
	Cell* val = popArg();
	int retAddr = ivalue(popArg());
	int argNum = ivalue(popArg());
	for(int i=0; i<argNum; i++) {
	  popArg();
	}
	pushArg(val);

	updateOffsetReg();
	inst = top;
	while(inst) {
	  if(inst->offset == retAddr) {
	    break;
	  }
	  inst = inst->next;
	}
      }
      break;
    case CONS:
      {
	Cell* cdrCell = popArg();
	Cell* carCell = popArg();
	Cell ret = pairCell((Cell)carCell, (Cell)cdrCell);
	pushArg((Cell*)ret);
      }
      inst = inst->next;
      break;
    case CAR:
      {
	Cell c = (Cell)popArg();
	Cell ca = car(c);
	pushArg((Cell*)ca);
      }
      inst = inst->next;
      break;
    case CDR:
      {
	Cell c = (Cell)popArg();
	Cell cd = cdr(c);
	pushArg((Cell*)cd);
      }
      inst = inst->next;
      break;
    case EQ:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 == num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
      }
      inst = inst->next;
      break;
    case GT:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 > num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
      }
      inst = inst->next;
      break;
    case LT:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 < num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
      }
      inst = inst->next;
      break;
    case QUOTE:
      {
	Cell c = (Cell)popArg();
	pushArg((Cell*)c);
      }
      inst = inst->next;
      break;
    case PRINT:
      {
	Cell* val = popArg();
	printCell((Cell)val);
	AQ_PRINTF("\n");
	pushArg((Cell*)AQ_UNDEF);
      }
      inst = inst->next;
      break;
    case HALT:
      AQ_PRINTF("[HALT]\n");
      exec = FALSE;
      break;
    case JNEQ:
      {
	Cell c = (Cell)popArg();
	int addr = ivalue(inst->operand);
	if(!truep(c)) {
	  while(inst) {
	    if(inst->offset == addr) {
	      break;
	    }
	    inst = inst->next;
	  }
	} else {
	  inst = inst->next;	  
	}
      }
      break;
    case JMP:
      {
	int addr = ivalue(inst->operand);
	inst = top;
	while(inst) {
	  if(inst->offset == addr) {
	    break;
	  }
	  inst = inst->next;
	}
      }
      break;
    case SET:
      {
	// TODO: different behavior between on-memory and bytecode.
	// this is for on-memory
	Cell val = (Cell)popArg();
	setVarCell(inst->operand, val);
	inst = inst->next;
	
	//AQ_PRINTF(" and SET: %s\n", strvalue(inst->operand));
      }
      break;
    case REF:
      {
	Cell val = inst->operand;
	Cell ret = getVar(strvalue(val));
	if(UNDEF_P(ret)) {
	  printError("[REF] undefined symbol: %s", strvalue(val));
	  exec = FALSE;
	} else {
	  pushArg((Cell*)ret);
	  inst = inst->next;
	}
      }
      break;
    case FUNC:
      {
	Cell val = inst->operand;
	Cell func = getVar(strvalue(val));
	if(UNDEF_P(func)) {
	  printError("undefined symbol: %s", strvalue(val));
	  exec = FALSE;
	} else {
	  int paramNum = ivalue(lambdaParamNum(func));
	  int argNum = ivalue(stack[stack_top-1]);
	  if(paramNum != argNum) {
	    AQ_PRINTF("param num is wrong: %d, %d\n", paramNum, argNum);
	    inst = inst->next;
	    break;
	  }
	  int retAddr  = inst->offset + inst->size;
	  pushArg((Cell*)makeInteger(retAddr));
	  updateOffsetReg();

	  // jump
	  int funcAddr = ivalue(lambdaAddr(func));
	  inst = top;
	  while(inst->offset != funcAddr) {
	    inst = inst->next;
	  }
	}
      }
      break;
    case FUND:
      {
	// jump
	int defEnd = ivalue(inst->operand);
	int defStart = inst->next->offset;
	int paramNum = ivalue(inst->operand2);
	Cell l = lambdaCell(defStart, paramNum);
	pushArg((Cell*)l);
	while(inst->offset != defEnd) {
	  inst = inst->next;
	}
      }
      break;
    case LOAD:
      {	
	int index = getOffsetReg() - ivalue(inst->operand) - 3;
	Cell* val = stack[index];
	pushArg(val);
	inst = inst->next;
      }
      break;
    case NOP:
      // do nothing
      inst = inst->next;
      break;
    default:
      AQ_PRINTF("DEFAULT: %d\n", inst->op);
      exec = FALSE;
      break;
    }
  }
}

void execBuf(FILE* fp)
{
  if( fp ){
    unsigned char buf[1000];
    size_t size = fread(buf, sizeof(unsigned char), 1000, fp);
    int pc = 0;
    while(pc < size) {
      unsigned char op = buf[pc];
      switch(op) {
      case ADD:
	{
	  Cell* val1 = popArg();
	  Cell* val2 = popArg();
	  long result = ivalue(val2) + ivalue(val1);
	  Cell* cellp = (Cell*)((result << 1) | AQ_INTEGER_MASK);
	  pushArg(cellp);
	  pc++;
	}
	break;
      case SUB:
	{
	  Cell* val1 = popArg();
	  Cell* val2 = popArg();
	  long result = ivalue(val2) - ivalue(val1);
	  Cell* cellp = (Cell*)((result << 1) | AQ_INTEGER_MASK);
	  pushArg(cellp);
	  pc++;
	}
	break;
      case MUL:
	{
	  Cell* val1 = popArg();
	  Cell* val2 = popArg();
	  long result = ivalue(val2) * ivalue(val1);
	  Cell* cellp = (Cell*)((result << 1) | AQ_INTEGER_MASK);
	  pushArg(cellp);
	  pc++;
	}
	break;
      case DIV:
	{
	  Cell* val1 = popArg();
	  Cell* val2 = popArg();
	  long result = ivalue(val2) / ivalue(val1);
	  Cell* cellp = (Cell*)((result << 1) | AQ_INTEGER_MASK);
	  pushArg(cellp);
	  pc++;
	}
	break;
      case PRINT:
	{
	  Cell* val = popArg();
	  printCell((Cell)val);
	  AQ_PRINTF("\n");
	  pc++;
	}
	break;
      case PUSH:
	{
	  unsigned char val = buf[++pc];
	  Cell* cellp = (Cell*)(((long)val << 1) | AQ_INTEGER_MASK);
	  pushArg(cellp);
	  pc++;
	}
	break;
      case POP:
	{
	  AQ_PRINTF("[POP]\n");
	}
	break;
      case EQ:
	{
	  Cell* val2 = popArg();
	  Cell* val1 = popArg();
	  Cell* result = (val1==val2) ? (Cell*)AQ_TRUE : (Cell*)AQ_FALSE;
	  pushArg(result);
	  pc++;
	}
	break;
      case LT:
	{
	  Cell* val2 = popArg();
	  Cell* val1 = popArg();
	  Cell* result = (val1<val2) ? (Cell*)AQ_TRUE : (Cell*)AQ_FALSE;
	  pushArg(result);
	  pc++;
	}
	break;
      case LTE:
	{
	  Cell* val2 = popArg();
	  Cell* val1 = popArg();
	  Cell* result = (val1<=val2) ? (Cell*)AQ_TRUE : (Cell*)AQ_FALSE;
	  pushArg(result);
	  pc++;
	}
	break;
      case GT:
	{
	  Cell* val2 = popArg();
	  Cell* val1 = popArg();
	  int i1 = ivalue(val1);
	  int i2 = ivalue(val2);
	  Cell* result = (i1>i2) ? (Cell*)AQ_TRUE : (Cell*)AQ_FALSE;
	  pushArg(result);
	  pc++;
	}
	break;
      case GTE:
	{
	  Cell* val2 = popArg();
	  Cell* val1 = popArg();
	  Cell* result = (val1>=val2) ? (Cell*)AQ_TRUE : (Cell*)AQ_FALSE;
	  pushArg(result);
	  pc++;
	}
	break;
      case JEQ:
      case JEQB:
	{
	  Cell val = (Cell)popArg();
	  int offset = buf[++pc];
	  if(op == JEQB) offset = -offset;
	  if(truep(val)){
	    pc = offset;
	  } else {
	    pc++;
	  }
	}
	break;
      case JNEQ:
      case JNEQB:
	{
	  Cell val = (Cell)popArg();
	  int offset = buf[++pc];
	  if(op == JNEQB) offset = -offset;
	  if(!truep(val)){
	    pc = offset;
	  } else {
	    pc++;
	  }
	}
	break;
      case JMP:
      case JMPB:
	{
	  int dst = buf[++pc];
	  int offset = buf[++pc];
	  if(op == JMPB) offset = -offset;
	  pc = dst;
	}
	break;
      case LOAD:
	{
	  int offset = -buf[++pc];
	  Cell* val = stack[stack_top+offset];
	  pushArg(val);
	  pc++;
	}
	break;
      case RET:
	{
	  Cell* value = popArg();
	  Cell* value2 = popArg();
	  int retAddr = ivalue(value2);
	  popArg();
	  pushArg(value);
	  pc = retAddr;
	}
	break;
      case CONS:
	{
	  Cell* carCell = popArg();
	  Cell* cdrCell = popArg();
	  Cell cell = pairCell((Cell)carCell, (Cell)cdrCell);
	  pushArg((Cell*)cell);
	  pc++;
	}
	break;
      case CAR:
	{
	  Cell c = (Cell)popArg();
	  Cell ca = car(c);
	  pushArg((Cell*)ca);
	  pc++;
	}
	break;
      case CDR:
	{
	  Cell c = (Cell)popArg();
	  Cell cd = cdr(c);
	  pushArg((Cell*)cd);
	  pc++;
	}
	break;
      case PUSH_NIL:
	{
	  pushArg((Cell*)AQ_NIL);
	  pc++;
	}
	break;
      case PUSH_TRUE:
	{
	  pushArg((Cell*)AQ_TRUE);
	  pc++;
	}
	break;
      case PUSH_FALSE:
	{
	  pushArg((Cell*)AQ_FALSE);
	  pc++;
	}
	break;
      case HALT:
	{
	  pc = size;
	  AQ_PRINTF("[HALT]\n");
	}
	break;
      default:
	AQ_PRINTF("DEFAULT: %d\n", op);
	pc = size; //just in case.
	break;
      }
    }
    fclose(fp);

    setReturn((Cell)AQ_TRUE);
  }else{
    //printError("load failed: %s\n", filename);
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
  if( isString(cell) ){
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
    setReturn((Cell)AQ_TRUE);
  }
  else{
    setReturn((Cell)AQ_FALSE);
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
  if( !isSymbol(symbol) ){
    setParseError( "not a symbol given" );
    return;
  }
  PUSH_ARGS2(&symbol, args);
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
	setReturn((Cell)AQ_NIL);
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
#if false
  Cell args = *popArg();
  Cell params = car(args);
  Cell exps = cdr(args);
  Cell lambda = lambdaCell(params, exps);
  setReturn(lambda);
#endif
}

void syntax_quote()
{
  setReturn(car(*popArg()));
}

void syntax_set()
{  
  Cell* args = getStackTop();
  Cell c1 = car(*args);
  if( !isSymbol(c1) ){
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
    //Cell ret;
    AQ_PRINTF_GUIDE(">");
    //clearArgs();
    callProc("read");
    Cell ret = getReturn();
    if(EOF_P(ret)) break;
    if(UNDEF_P(ret)) continue;
    Cell pair = pairCell(ret, (Cell)AQ_NIL);
    pushArg(&pair);

    callProc("eval");
    printLineCell(getReturn());
  }
  return 0;
}

int handle_option(int argc, char *argv[])
{
  int i = 1;
  if( argc >= 3 && strcmp(argv[ 1 ], "-GC" ) == 0 ){
    set_gc(argv[ 2 ]);
    i += 2;
  }else{
    set_gc("");
  }

  return i;
}

Boolean ApplyParams(Cell args, int stack_top, Cell* pExps, Cell* pParams, Boolean is_loop)
{
    cloneTree(args);
    gc_write_barrier_root(stack[stack_top-1]/*args*/, getReturn());
    cloneSymbolTree(*pExps);
    gc_write_barrier_root(pExps, getReturn());
    letParam(*pExps, *pParams, args);
    if( UNDEF_P(args) ){
	setReturn((Cell)AQ_UNDEF);
	return FALSE;
    }
    gc_write_barrier_root(pExps, pairCell((Cell)AQ_NIL, *pExps));
    popArg();
    return is_loop;
}

int main(int argc, char *argv[])
{
  int i = handle_option(argc, argv);
  init();
  if( i >= argc ){
    repl();
  }else{
    load_file( argv[ i ] );
  }
  term();
  return 0;
}

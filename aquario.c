#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "aquario.h"
#include "gc/base.h"
#include "gc/copy.h"
#include "gc/markcompact.h"

static void set_gc(char*);

Boolean g_GC_stress;

static Cell getChain(char* name, int* key);
static void registerVar(Cell nameCell, Cell chain, Cell c, Cell* env);

static void init();
static void term();

static int heap_size = HEAP_SIZE;

#define FUNCTION_STACK_SIZE  (1024)
static int functionStack[FUNCTION_STACK_SIZE];
static int functionStackTop = 0;
static void pushFunctionStack(int f);
static int popFunctionStack();
static int getFunctionStackTop();

static ErrorType errType = ERR_TYPE_NONE;

#define ERR_WRONG_NUMBER_ARGS(required, given)			\
  pushArg(makeInteger(required));				\
  pushArg(makeInteger(given));					\
  errType = ERR_TYPE_WRONG_NUMBER_ARG;				\
  return 0;							\
  
#define CHECK_ERR_PAIR_NOT_GIVEN()			\
  if(!isPair(stack[stack_top-1])) {			\
    errType = ERR_TYPE_PAIR_NOT_GIVEN;			\
    return 0;						\
  }							\

#define CHECK_ERR_INT_NOT_GIVEN(num)			\
  if(!isInteger(num)) {					\
    pushArg(num);					\
    errType = ERR_TYPE_INT_NOT_GIVEN;			\
    return 0;						\
  } 							\

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

Cell pairCell(Cell* a, Cell* d)
{
  Cell cons = newCell(T_PAIR, sizeof(struct cell));

  gc_init_ptr(&cdr(cons), *d);
  gc_init_ptr(&car(cons), *a);
  return cons;
}

Cell symbolCell(char* symbol)
{
  int obj_size = sizeof(struct cell) + sizeof(char)*strlen(symbol)-sizeof(CellUnion)+1;
  Cell c = newCell(T_SYMBOL, obj_size);
  STRCPY(symbolname(c), symbol);
  return c;
}

Cell lambdaCell(int addr, int paramNum, Boolean isParamDList)
{
  Cell l = newCell(T_LAMBDA, sizeof(struct cell));
  lambdaAddr(l) = makeInteger(addr);
  lambdaParamNum(l) = makeInteger(paramNum);
  lambdaFlag(l) = isParamDList;
  return l;
}

Cell makeInteger(int val)
{
  long lval = val;  
  return (Cell)((lval << 1) | AQ_INTEGER_MASK);
}

Boolean isdigitstr(char* str)
{
  int i;
  int len = strlen(str);
  for(i=0;i<len;++i){
    if(!isdigit(str[i])){
      if(len < 2 || i!=0 || (str[0] != '-' && str[0] != '+')){
	return FALSE;
      }
    }
  }
  return TRUE;
}

Boolean nullp(Cell c)
{
  return NIL_P(c) ? TRUE:FALSE;
}

Boolean truep(Cell c)
{
  return !( nullp(c) || notp(c) )?TRUE:FALSE;
}

Boolean notp(Cell c)
{
  return FALSE_P(c)?TRUE:FALSE;
}

void printCons(Cell c)
{
  if(CELL_P(car(c)) && type(car(c)) == T_SYMBOL &&
     strcmp(symbolname(car(c)), "quote") == 0) {
    AQ_PRINTF("'");
    printCell(cadr(c));
    return;
  }
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
      if(token == buf) {
	break;
      } else {
	*token = '\0';
	return buf;
      }
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

size_t compile(FILE* fp, char* buf)
{
  InstQueue instQ;
  Inst inst;
  inst.op = NOP;
  inst.prev = NULL;
  inst.next = NULL;
  inst.offset = 0;
  inst.size = 1;
  instQ.head = &inst;
  instQ.tail = &inst;
  char chr = 0;
  while((chr = fgetc(fp)) != EOF && !isError()) {
    ungetc(chr, fp);
    compileElem(&instQ, fp, NULL);
  }

  if(isError()) {
    return 0;
  }

  return writeInst(instQ.head, buf);
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

void compileQuotedAtom(InstQueue* instQ, char* symbol, FILE* fp)
{
  if(isdigitstr(symbol)) {
    int digit = atoi(symbol);
    addPushTail(instQ, digit);
  }else{
    Inst* inst = createInstStr(PUSH_SYM, symbol);
    addInstTail(instQ, inst);
  }
}

void compileQuotedList(InstQueue* instQ, FILE* fp)
{
  char buf[LINESIZE];
  char* token = readToken(buf, sizeof(buf), fp);

  if(strcmp(token, "(") == 0) {
    compileQuotedList(instQ, fp);
    compileQuotedList(instQ, fp);
    addOneByteInstTail(instQ, CONS);
  }else if(strcmp(token, ")") == 0) {
    addOneByteInstTail(instQ, PUSH_NIL);
  }else if(strcmp(token, "'") == 0) {
    compileQuote(instQ, fp);
    compileQuotedList(instQ, fp);
    addOneByteInstTail(instQ, CONS);
  }else if(strcmp(token, ".") == 0) {
    token = readToken(buf, sizeof(buf), fp);
    if(strcmp(token, "(") == 0) {
      compileQuotedList(instQ, fp);
    }
    else if(strcmp(token, "'") == 0) {
      compileQuote(instQ, fp);
    } else {
      compileQuotedAtom(instQ, token, fp);
    }
    token = readToken(buf, sizeof(buf), fp);
    if(strcmp(token, ")") != 0) {
      printError("broken dot list");
    }
  }
  else {
    compileQuotedAtom(instQ, token, fp);
  
    compileQuotedList(instQ, fp);
    addOneByteInstTail(instQ, CONS);
  }
}
  
void compileQuote(InstQueue* instQ, FILE* fp)
{
  char buf[LINESIZE];
  char* token = readToken(buf, sizeof(buf), fp);

  Inst* inst = createInstStr(PUSH_SYM, "quote");
  addInstTail(instQ, inst);
  
  if(token[0]=='('){
    compileQuotedList(instQ, fp);
  }
  else if(strcmp(token, "'") == 0) {
    compileQuote(instQ, fp);
  } else {
    compileQuotedAtom(instQ, token, fp);
  }
  
  addOneByteInstTail(instQ, PUSH_NIL);
  addOneByteInstTail(instQ, CONS);
  addOneByteInstTail(instQ, CONS);
}
 
Inst* createInstChar(OPCODE op, char c)
{
  Inst* result = createInst(op, 3);
  result->operand._char = c;
  
  return result;
}

Inst* createInstStr(OPCODE op, char* str)
{
  int len = strlen(str)+1;
  Inst* result = createInst(op, len+1);
  result->operand._string = malloc(sizeof(char)*len);
  strcpy(result->operand._string, str);

  return result;
}

Inst* createInstNum(OPCODE op, int num)
{
  Inst* result = createInst(op, 1+sizeof(Cell));
  result->operand._num = makeInteger(num);
  
  return result;
}

Inst* createInst(OPCODE op, int size)
{
  Inst* result = (Inst*)malloc(sizeof(Inst));
  result->op = op;
  result->prev = NULL;
  result->next = NULL;
  result->operand._num  = (Cell)AQ_NIL;
  result->operand2._num = (Cell)AQ_NIL;
  result->size = size;
  result->offset = 0;
  
  return result;
}

void compileToken(InstQueue* instQ, char* token, Cell symbolList)
{
  if(isdigitstr(token)) {
    int digit = atoi(token);
    addPushTail(instQ, digit);
  }
  else if(strcmp(token, "nil") == 0) {
    addOneByteInstTail(instQ, PUSH_NIL);
  }  
  else if(token[0] == '"'){
    Inst* inst = createInstStr(PUSHS, &token[1]);
    addInstTail(instQ, inst);
  }
  else if(token[0] == '#'){
    if(token[1] == '\\' && strlen(token)==3){
      addInstTail(instQ, createInstChar(PUSH, token[2])); // TODO: PUSHC
    }
    else if(strcmp(&token[1], "t") == 0){
      addOneByteInstTail(instQ, PUSH_TRUE);
    }
    else if(strcmp(&token[1], "f") == 0){
      addOneByteInstTail(instQ, PUSH_FALSE);
    }
    else{
      Inst* inst = createInstStr(PUSHS, token);
      addInstTail(instQ, inst);
    }
  } else {
    int index = 0;
    while(symbolList && !NIL_P(symbolList)) {
      char* symbol = (char*)car(symbolList);
      if(strcmp(symbol, token) == 0) {
	Inst* inst = createInstNum(LOAD, index);
	addInstTail(instQ, inst);
	return;
      }
      
      symbolList = cdr(symbolList);
      index++;
    }
    Inst* inst = createInstStr(REF, token);
    addInstTail(instQ, inst);
  }
}

void addInstTail(InstQueue* queue, Inst* inst)
{
  inst->offset = queue->tail->offset + queue->tail->size;
  queue->tail->next = inst;
  inst->prev = queue->tail;
  queue->tail = inst;
}

void addPushTail(InstQueue* instQ, int num)
{
  return addInstTail(instQ, createInstNum(PUSH, num));
}

void addOneByteInstTail(InstQueue* instQ, OPCODE op)
{
  Inst* ret = createInst(op, 1);
  return addInstTail(instQ, ret);
}

void compileProcedure(char* func, int num, InstQueue* instQ)
{
    if(strcmp(func, "+") == 0) {
      compileAdd(instQ, num);
    } else if(strcmp(func, "-") == 0) {
      compileSub(instQ, num);
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
    } else if(strcmp(func, ">") == 0) {
      addOneByteInstTail(instQ, GT);
    } else if(strcmp(func, "<") == 0) {
      addOneByteInstTail(instQ, LT);
    } else if(strcmp(func, "<=") == 0) {
      addOneByteInstTail(instQ, LTE);
    } else if(strcmp(func, ">=") == 0) {
      addOneByteInstTail(instQ, GTE);
    } else if(strcmp(func, "=") == 0) {
      addOneByteInstTail(instQ, EQUAL);
    } else if(strcmp(func, "eq?") == 0) {
      addOneByteInstTail(instQ, EQ);
    } else {
      addPushTail(instQ, num);
      addInstTail(instQ, createInstStr(FUNC, func));
    }
}

void compileAdd(InstQueue* instQ, int num)
{
  Inst* lastInst = instQ->tail;
  if(lastInst && lastInst->op == PUSH && num == 2 ) {
    if(lastInst->operand._num == makeInteger(1)) {
      lastInst->op = ADD1;
      lastInst->offset = lastInst->prev->offset + lastInst->prev->size;
      lastInst->size = 1;
      return;
    } else if(lastInst->operand._num == makeInteger(2)) {
      lastInst->op = ADD2;
      lastInst->offset = lastInst->prev->offset + lastInst->prev->size;
      lastInst->size = 1;
      return;
    }
  }
  addPushTail(instQ, num);
  addOneByteInstTail(instQ, ADD);
}

void compileSub(InstQueue* instQ, int num)
{
  Inst* lastInst = instQ->tail;
  if(lastInst && lastInst->op == PUSH && num == 2) {
    if(lastInst->operand._num == makeInteger(1)) {
      lastInst->op = SUB1;
      lastInst->offset = lastInst->prev->offset + lastInst->prev->size;
      lastInst->size = 1;
      return;
    } else if(lastInst->operand._num == makeInteger(2)) {
      lastInst->op = SUB2;
      lastInst->offset = lastInst->prev->offset + lastInst->prev->size;
      lastInst->size = 1;
      return;
    }
  }
  addPushTail(instQ, num);
  addOneByteInstTail(instQ, SUB);
}

void compileIf(InstQueue* instQ, FILE* fp, Cell symbolList)
{
  compileElem(instQ, fp, symbolList);  // predicate
  
  Inst* jneqInst = createInstNum(JNEQ, 0 /* placeholder */);
  addInstTail(instQ, jneqInst);
  compileElem(instQ, fp, symbolList);  // statement (TRUE)
  
  Inst* jmpInst = createInstNum(JMP, 0/* placeholder */);
  addInstTail(instQ, jmpInst);
  jneqInst->operand._num = makeInteger(instQ->tail->offset + instQ->tail->size);
  
  int c = fgetc(fp);
  ungetc(c, fp);
  if(c ==')' ){
    addOneByteInstTail(instQ, PUSH_NIL);
  } else {
    compileElem(instQ, fp, symbolList);  // statement (FALSE)
  }
  
  jmpInst->operand._num = makeInteger(instQ->tail->offset + instQ->tail->size);

  char buf[LINESIZE];
  char* token = readToken(buf, sizeof(buf), fp);
  if(token[0] !=')' ){
    AQ_PRINTF("too many expressions\n");
  }
}

void compileLambda(InstQueue* instQ, FILE* fp)
{
  Inst* inst = createInst(FUND, 1 + sizeof(Cell)*2);
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

    if (strcmp(var, ".") == 0) {
      var = readToken(buf, sizeof(buf), fp);
      compileSymbolList(var, &symbolList);

      var = readToken(buf, sizeof(buf), fp);
      if (strcmp(var, ")") != 0) {
	AQ_PRINTF("Too much elements are given to dot list.\n");
	return;
      }
      
      index++;
      inst->op = FUNDD;
      break;
    } else {
      compileSymbolList(var, &symbolList);
      
      index++;
    }
  }
  compileList(instQ, fp, symbolList);  // body
  while(symbolList != (Cell)AQ_NIL) {
    Cell tmp = symbolList;
    symbolList = cdr(symbolList);
    free(car(tmp));
    free(tmp);
  }
  addOneByteInstTail(instQ, RET);
  
  int addr = instQ->tail->offset + instQ->tail->size;
  inst->operand._num = makeInteger(addr);
  inst->operand2._num = makeInteger(index);
}

void compileDefine(InstQueue* instQ, FILE* fp, Cell symbolList)
{
  char buf[LINESIZE];
  char* var = readToken(buf, sizeof(buf), fp);
  
  compileElem(instQ, fp, symbolList);
  addInstTail(instQ, createInstStr(SET, var));
  
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
    addOneByteInstTail(instQ, HALT);
  }
  else if(token[0]=='('){
    char* func = readToken(buf, sizeof(buf), fp);
    if(strcmp(func, "(") == 0) {
      ungetc('(', fp);
      compileElem(instQ, fp, symbolList);
      int num = compileList(instQ, fp, symbolList);
      addPushTail(instQ, num);
      addInstTail(instQ, createInstNum(SROT, num+1));
      addOneByteInstTail(instQ, FUNCS);
    }
    else if(strcmp(func, ")") == 0) {
      addOneByteInstTail(instQ, PUSH_NIL);
    } else if(strcmp(func, "quote") == 0) {
      compileQuote(instQ, fp);
      addOneByteInstTail(instQ, CDR);
      addOneByteInstTail(instQ, CAR);
      
      token = readToken(buf, sizeof(buf), fp);
      if(token[0]!=')'){
	printError("broken quote");
      }
    }else if(strcmp(func, "if") == 0) {
      compileIf(instQ, fp, symbolList);
    } else if(strcmp(func, "define") == 0) {
      compileDefine(instQ, fp, symbolList);
    } else if(strcmp(func, "lambda") == 0) {
      compileLambda(instQ, fp);
    } else {
      int num = compileList(instQ, fp, symbolList);
      compileProcedure(func, num, instQ);
    }
  }
  else if(token[0]=='\''){
    compileQuote(instQ, fp);

    addOneByteInstTail(instQ, CDR);
    addOneByteInstTail(instQ, CAR);
  }
  else if(token[0]==')'){
    printError("extra close parensis");
  }
  else{
    compileToken(instQ, token, symbolList);
  }
}

void compileSymbolList(char* var, Cell* symbolList)
{
  Cell tmp = malloc(sizeof(struct cell));
  size_t len = strlen(var) + 1;
  char* sym = malloc(len);
  
  strcpy(sym, var);
  car(tmp) = (Cell)sym;
  cdr(tmp) = *symbolList;
  *symbolList = tmp;
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
  int key = 0;
  Cell chain = getChain(name, &key);
  if(UNDEF_P(chain)) {
    return (Cell)AQ_UNDEF;
  } else {
    return cdar(chain);
  }
}

void setVar(char* name, Cell c)
{
  int key = 0;
  pushArg(c);
  Cell nameCell = stringCell(name);
  c = popArg();
  Cell chain = getChain(name, &key);
  registerVar(nameCell, chain, c, &env[key]);
}

void registerVar(Cell nameCell, Cell chain, Cell c, Cell* env)

{
  if(!UNDEF_P(chain)){
    gc_write_barrier(chain, &cdr(car(chain)), c);
  } else{
    pushArg(nameCell);
    pushArg(c);
    Cell entry = pairCell(&stack[stack_top-2], &stack[stack_top-1]);
    popArg();
    popArg();

    pushArg(entry);
    pushArg(*env);
    Cell p = pairCell(&stack[stack_top-2], &stack[stack_top-1]);
    popArg();
    popArg();
  
    gc_write_barrier_root(env, p);
  }
}

Cell getChain(char* name, int* key)
{
  *key = hash(name)%ENVSIZE;
  Cell chain = env[*key];
  while(isPair(chain) && strcmp(name, strvalue(caar(chain)))!=0){
    chain = cdr(chain);
  }
  return chain;
}

void init()
{
  int i;
  for(i=0; i<ENVSIZE; ++i) {
    env[i] = (Cell)AQ_UNDEF;
  }
  memset(stack, 0, STACKSIZE);
  stack_top = 0;
}

void term()
{
  gc_term();
  gc_term_base();
}

void set_gc(char* gc_char)
{
  GC_Init_Info gc_info;
  memset(&gc_info, 0, sizeof(GC_Init_Info));
  gc_init( gc_char, heap_size, &gc_info );
}

void load_file( const char* filename )
{
  FILE* fp = NULL;
#if defined( _WIN32 ) || defined( _WIN64 )
  fopen_s( &fp, filename, "r");
#else
  int len = strlen(filename);
  char* abcFileName = malloc(sizeof(char*) * (len + 3));
  strcpy(abcFileName, filename);
  char* ext = strrchr(abcFileName, '.');
  if(ext) {
    *ext = '\0';
  }
  strcat(abcFileName, ".abc");
  
  struct stat abcInfo;
  struct stat lspInfo;
  Boolean noCompile = (stat(abcFileName, &abcInfo) == 0 &&
		       stat(filename, &lspInfo) == 0 &&
		       abcInfo.st_ctime > lspInfo.st_ctime);
  
  char* buf = (char*)malloc(sizeof(char) * 1024 * 1024);
  if(noCompile) {
    fp = fopen(abcFileName, "rb");
    if(fp) {
      fread(buf, abcInfo.st_size, 1, fp);
      execute(buf, 0, abcInfo.st_size);
      fclose(fp);
    } else {
      AQ_PRINTF("[ERROR] %s: cannot open.\n", abcFileName);
    }
  } else {
    fp = fopen(filename, "r");
    if(fp) {
      size_t fileSize = compile(fp, buf);
      if(fileSize > 0) {
	execute(buf, 0, fileSize);
	fclose(fp);
      
	FILE* outputFile = fopen(abcFileName, "wb");
	fwrite(buf, fileSize, 1, outputFile);
	fclose(outputFile);
      } else {
	fclose(fp);
      }
    } else {
      AQ_PRINTF("[ERROR] %s: not found\n", filename);
    }
  }

  free(abcFileName);
  free(buf);
#endif
}

size_t writeInst(Inst* inst, char* buf)
{
  size_t size = 0;
  while(inst) {
    OPCODE op = inst->op;
    buf[size] = (char)op;
    switch(op) {
    case PUSH:
    case JNEQ:
    case JMP:
    case SROT:
    case LOAD:
      {
	long val = ivalue(inst->operand._num);
	memcpy(&buf[++size], &val, sizeof(long));
	size += sizeof(long);
      }
      break;
    case SET:
    case REF:
    case FUNC:
    case PUSHS:
    case PUSH_SYM:
      {
	char* str = inst->operand._string;
	strcpy(&buf[++size], str);
	size += (strlen(str)+1);
	free(inst->operand._string);
      }
      break;
    case FUND:
    case FUNDD:
      {
	int addr = ivalue(inst->operand._num);
	memcpy(&buf[++size], &addr, sizeof(long));
	size += sizeof(long);
	
	int paramNum = ivalue(inst->operand2._num);
	memcpy(&buf[size], &paramNum, sizeof(long));
	size += sizeof(long);
      }
      break;
    case NOP:
    case ADD:
    case ADD1:
    case ADD2:
    case SUB:
    case SUB1:
    case SUB2:
    case MUL:
    case DIV:
    case PRINT:
    case POP:
    case CONS:
    case CAR:
    case CDR:
    case PUSH_NIL:
    case PUSH_TRUE:
    case PUSH_FALSE:
    case GT:
    case LT:
    case GTE:
    case LTE:
    case HALT:
    case EQUAL:
    case EQ:
    case RET:
    case FUNCS:
      buf[size] = (char)inst->op;
      size += 1;
      break;
    }

    inst = inst->next;
  }

  return size;
}

Cell getOperand(char* buf, int pc)
{
  return (Cell)(*(Cell*)&buf[pc]);
}

int execute(char* buf, int start, int end)
{
  Boolean exec = TRUE;
  stack_top = 0;
  int pc = start;
  int i = 0;
  while(exec != FALSE && pc < end) {
    OPCODE op = buf[pc];
    switch(op) {
    case PUSH:
      {
	int value = (int)getOperand(buf, ++pc);
	pushArg(makeInteger(value));
	pc += sizeof(Cell);
      }
      break;
    case PUSH_NIL:
      pushArg((Cell)AQ_NIL);
      ++pc;
      break;
    case PUSH_TRUE:
      pushArg((Cell)AQ_TRUE);
      ++pc;
      break;
    case PUSH_FALSE:
      pushArg((Cell)AQ_FALSE);
      ++pc;
      break;
    case ADD:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int num = ivalue(stack[stack_top-1]);
	popArg();
	long ans = 0;
	for(i=0; i<num; i++) {
	  CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	  ans += ivalue(stack[stack_top-1]);
	  popArg();
	}
	pushArg(makeInteger(ans));
	++pc;
      }
      break;
    case ADD1:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int ans = ivalue(stack[stack_top-1]) + 1;
	popArg();
	pushArg(makeInteger(ans));
	pc++;
      }
      break;
    case ADD2:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int ans = ivalue(stack[stack_top-1]) + 2;
	popArg();
	pushArg(makeInteger(ans));
	pc++;
      }
      break;
    case SUB:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int num = ivalue(stack[stack_top-1]);
	popArg();
	if(num == 1) {
	  CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	  int ans = -ivalue(stack[stack_top-1]);
	  popArg();
	  pushArg(makeInteger(ans));
	} else {
	  long ans = 0;
	  for(i=0; i<num-1; i++) {
	    CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	    ans -= ivalue(stack[stack_top-1]);
	    popArg();
	  }
	  CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	  ans += ivalue(stack[stack_top-1]);
	  popArg();
	  pushArg(makeInteger(ans));
	}
	++pc;
      }
      break;
    case SUB1:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int ans = ivalue(stack[stack_top-1]) - 1;
	popArg();
	pushArg(makeInteger(ans));
	pc++;
      }
      break;
    case SUB2:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int ans = ivalue(stack[stack_top-1]) - 2;
	popArg();
	pushArg(makeInteger(ans));
	pc++;
      }
      break;
    case MUL:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int num = ivalue(stack[stack_top-1]);
	popArg();
	long ans = 1;
	for(i=0; i<num; i++) {
	  CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	  ans *= ivalue(stack[stack_top-1]);
	  popArg();
	}
	pushArg(makeInteger(ans));
	++pc;
      }
      break;
    case DIV:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	int num = ivalue(stack[stack_top-1]);
	popArg();
	if(num == 1) {
	  CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	  long ans = 1/ivalue(stack[stack_top-1]);
	  popArg();
	  pushArg(makeInteger(ans));
	} else {
	  int div = 1;
	  for(i=0; i<num-1; i++) {
	    CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	    div *= ivalue(stack[stack_top-1]);
	    popArg();
	  }
	  CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	  long ans = ivalue(stack[stack_top-1]);
	  popArg();
	  ans = ans/div;
	  pushArg(makeInteger(ans));
	}
	++pc;
      }
      break;
    case RET:
      {
	Cell val = stack[--stack_top];
	while(!SFRAME_P(popArg())){}
	int retAddr = ivalue(popArg());
	int argNum = ivalue(popArg());
	for(i=0; i<argNum; ++i) {
	  popArg();
	}
	popFunctionStack();
	stack[stack_top++] = val;
	pc = retAddr;
      }
      break;
    case CONS:
      {
	Cell ret = pairCell(&stack[stack_top-2], &stack[stack_top-1]);
	popArg();
	popArg();

	pushArg(ret);
	++pc;
      }
      break;
    case CAR:
      {
	CHECK_ERR_PAIR_NOT_GIVEN();
	gc_write_barrier_root(&stack[stack_top-1], car(stack[stack_top-1]));
	++pc;
      }
      break;
    case CDR:
      {
	CHECK_ERR_PAIR_NOT_GIVEN();
	gc_write_barrier_root(&stack[stack_top-1], cdr(stack[stack_top-1]));
	++pc;
      }
      break;
    case EQUAL:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-2]);
	Cell num2 = stack[stack_top-1];
	Cell num1 = stack[stack_top-2];
	Cell ret = (num1 == num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	popArg();
	popArg();
	pushArg(ret);
	++pc;
      }
      break;
    case GT:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-2]);
	int num2 = ivalue(stack[stack_top-1]);
	int num1 = ivalue(stack[stack_top-2]);
	Cell ret = (num1 > num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	popArg();
	popArg();
	pushArg(ret);
	++pc;
      }
      break;
    case GTE:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-2]);
	int num2 = ivalue(stack[stack_top-1]);
	int num1 = ivalue(stack[stack_top-2]);
	Cell ret = (num1 >= num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	popArg();
	popArg();
	pushArg(ret);
	++pc;
      }
      break;
    case LT:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-2]);
	int num2 = ivalue(stack[stack_top-1]);
	int num1 = ivalue(stack[stack_top-2]);
	Cell ret = (num1 < num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	popArg();
	popArg();
	pushArg(ret);
	++pc;
      }
      break;
    case LTE:
      {
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-1]);
	CHECK_ERR_INT_NOT_GIVEN(stack[stack_top-2]);
	int num2 = ivalue(stack[stack_top-1]);
	int num1 = ivalue(stack[stack_top-2]);
	Cell ret = (num1 <= num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	popArg();
	popArg();
	pushArg(ret);
	++pc;
      }
      break;
    case EQ:
      {
	Cell p1 = popArg();
	Cell p2 = popArg();
	Cell ret = (p1 == p2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg(ret);
	++pc;
      }
      break;
    case PRINT:
      {
	printCell(stack[stack_top-1]);
	AQ_PRINTF("\n");
	popArg();
	pushArg((Cell)AQ_UNDEF);
	++pc;
      }
      break;
    case JNEQ:
      {
	Cell c = stack[stack_top-1];
	popArg();
	++pc;
	if(!truep(c)) {
	  int addr = (int)getOperand(buf, pc);
	  pc = addr;
	} else {
	  pc += sizeof(Cell);
	}
      }
      break;
    case JMP:
      {
	int addr = (int)getOperand(buf, ++pc);
	pc = addr;
      }
      break;
    case SET:
      {
	// this is for on-memory
	Cell val = stack[stack_top-1];
	char* str = &buf[++pc];
	setVar(str, val);
	popArg();
	pushArg(symbolCell(str));
	pc += (strlen(str)+1);
      }
      break;
    case PUSHS:
      {
	char* str = &buf[++pc];
	Cell strCell = stringCell(str);
	pushArg(strCell);
	pc += (strlen(str)+1);
      }
      break;
    case PUSH_SYM:
      {
	char* sym = &buf[++pc];
	Cell symCell = symbolCell(sym);
	pushArg(symCell);
	pc += (strlen(sym)+1);
      }
      break;
    case REF:
      {
	char* str = &buf[++pc];
	Cell ret = getVar(str);
	if(UNDEF_P(ret)) {
	  printError("[REF] undefined symbol: %s", str);
	  pushArg((Cell)AQ_UNDEF);
	  exec = FALSE;
	} else {
	  pushArg(ret);
	  pc += (strlen(str)+1);
	}
      }
      break;
    case FUNC:
      {
	char* str = &buf[++pc];
	Cell func = getVar(str);
	if(UNDEF_P(func)) {
	  printError("[FUNC] undefined function: %s", str);
	  int num = ivalue(popArg());
	  for(i=0; i<num; i++) {
	    popArg();
	  }
	  pushArg((Cell)AQ_UNDEF);
	  exec = FALSE;
	} else {
	  int paramNum = ivalue(lambdaParamNum(func));
	  int argNum = ivalue(stack[stack_top-1]);
	  int funcAddr = ivalue(lambdaAddr(func));
	  Boolean isParamDList = lambdaFlag(func);
	  if(isParamDList) {
	    if(paramNum > argNum) {
	      ERR_WRONG_NUMBER_ARGS(paramNum, argNum);
	    }
	    popArg();
	    int num = argNum - paramNum + 1;
	    Cell lst = (Cell)AQ_NIL;
	    for(i=0; i<num; i++) {
	      pushArg(lst);
	      lst = pairCell(&stack[stack_top-2], &stack[stack_top-1]);
	      popArg();
	      popArg();
	    }
	    pushArg(lst);
	    pushArg(makeInteger(paramNum));
	  } else {
	    if(paramNum != argNum) {
	      ERR_WRONG_NUMBER_ARGS(paramNum, argNum);
	    }
	  }
	  int retAddr  = pc + strlen(str) + 1;
	  pushArg(makeInteger(retAddr));
	  pushArg((Cell)AQ_SFRAME);
	  pushFunctionStack(stack_top);
	  
	  // jump
	  pc = funcAddr;
	}
      }
      break;
    case FUND:
    case FUNDD:
      {
	// jump
	int defEnd = (int)getOperand(buf, ++pc);
	int defStart = pc + sizeof(Cell) * 2;
	pc += sizeof(Cell);
	int paramNum = (int)getOperand(buf, pc);
	Cell l = lambdaCell(defStart, paramNum, (op == FUNDD) ? TRUE : FALSE);
	pushArg(l);
	pc = defEnd;
      }
      break;
    case FUNCS:
      {
	Cell func = popArg();
	int argNum = ivalue(stack[stack_top-1]);
	int paramNum = ivalue(lambdaParamNum(func));
	int funcAddr = ivalue(lambdaAddr(func));
	Boolean isParamDList = lambdaFlag(func);
	if(isParamDList) {
	  if (paramNum > argNum) {
	    ERR_WRONG_NUMBER_ARGS(paramNum, argNum);
	  }
	  popArg();
	  int num = argNum - paramNum + 1;
	  Cell lst = (Cell)AQ_NIL;
	  for(i=0; i<num; i++) {
	    pushArg(lst);
	    lst = pairCell(&stack[stack_top-2], &stack[stack_top-1]);
	    popArg();
	    popArg();
	  }
	  pushArg(lst);
	  pushArg(makeInteger(paramNum));
	} else {
	  if(paramNum != argNum) {
	    ERR_WRONG_NUMBER_ARGS(paramNum, argNum);
	  }
	}
	
	int retAddr  = pc + 1;
	pushArg(makeInteger(retAddr));
	pushArg((Cell)AQ_SFRAME);
	pushFunctionStack(stack_top);
	
	// jump
	pc = funcAddr;
      }
      break;
    case SROT:
      {
	int n = (int)getOperand(buf, ++pc);
	Cell val = stack[stack_top-(n+1)];
	for(i=n; i>0; i--) {
	  stack[stack_top-(i+1)] = stack[stack_top-i];
	}
	stack[stack_top-1] = val;
	pc += sizeof(Cell);
      }
      break;
    case LOAD:
      {
	int offset = (int)getOperand(buf, ++pc);
	int index = getFunctionStackTop() - offset - 4;
	Cell val = stack[index];
	pushArg(val);
	pc += sizeof(Cell);
      }
      break;
    case NOP:
      // do nothing
      ++pc;
      break;
    case HALT:
      exec = FALSE;
      ++pc;
      break;
    default:
      AQ_PRINTF("Unknown opcode: %d\n", op);
      exec = FALSE;
      break;
    }
  }

  return pc;
}

void printError(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[ERROR]");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

Boolean isError() {
  return (errType != ERR_TYPE_NONE);
}

void handleError()
{
  switch(errType) {
  case ERR_TYPE_WRONG_NUMBER_ARG:
    AQ_PRINTF("wrong number of argnuments: required ");
    printCell(stack[stack_top-2]);
    AQ_PRINTF(", but given ");
    printLineCell(stack[stack_top-1]);
    break;
  case ERR_TYPE_PAIR_NOT_GIVEN:
    AQ_PRINTF("pair required, but given ");
      printLineCell(stack[stack_top-1]);
    break;
  case ERR_TYPE_INT_NOT_GIVEN:
    {
      AQ_PRINTF("number required, but given ");
      printLineCell(stack[stack_top-1]);
    }
    break;
  case ERR_TYPE_NONE:
    return;
  }

  while(stack_top > 0) {
    popArg();
  }
  errType = ERR_TYPE_NONE;
}

void pushFunctionStack(int f)
{
  if(functionStackTop >= FUNCTION_STACK_SIZE) {
    printError( "Function Stack Overflow" );
    return;
  }
  functionStack[functionStackTop++] = f;
}

int popFunctionStack()
{
  if(functionStackTop <= 0) {
    printError( "Function Stack Unferflow" );
    return -1;
  }
  return functionStack[--functionStackTop];
}

int getFunctionStackTop()
{
  return functionStack[functionStackTop-1];
}

Boolean isEndInput(int c)
{
#if defined( _TEST )
  if(c == EOF || c == '\n') return TRUE;
#else
  if(c == EOF) return TRUE;
 #endif
  return FALSE;
}

int repl()
{
  char* buf = (char*)malloc(sizeof(char) * 1024 * 1024);
  int pc = 0;
  while(1) {
    AQ_PRINTF_GUIDE(">");
    int c = fgetc(stdin);
    if(isEndInput(c)) break;
    ungetc(c, stdin);
    
    InstQueue instQ;
    Inst inst;
    inst.op = NOP;
    inst.prev = NULL;
    inst.next = NULL;
    inst.offset = pc;
    inst.size = 1;
    instQ.head = &inst;
    instQ.tail = &inst;
    
    compileElem(&instQ, stdin, NULL);
    if(isError()) {
      handleError();
      pc = 0;
      continue;
    }
    
    size_t bufSize = writeInst(instQ.head, &buf[pc]);
    
    pc = execute(buf, pc, pc+bufSize);
    if(isError()) {
      handleError();
    } else {
      printLineCell(stack[stack_top-1]);
      popArg();
    }
  }

  return 0;
}

int handle_option(int argc, char *argv[])
{
  int i = 1;
  for(; i<argc-1; i++) {
    if(strcmp(argv[ i ], "-GC" ) == 0 ){
      set_gc(argv[ ++i ]);
    }else if(strcmp(argv[ i ], "-GC_STRESS" ) == 0 ){
      g_GC_stress = TRUE;
    }
  }
  return i;
}

int main(int argc, char *argv[])
{
  set_gc("");
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

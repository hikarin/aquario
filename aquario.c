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

static void init();
static void term();

static int heap_size = HEAP_SIZE;

#define UNDEF_RETURN(x)         \
  if( UNDEF_P(x) ){             \
    pushArg((Cell*)AQ_UNDEF);	\
    return;                     \
  }

#define WRONG_NUMBER_ARGUMENTS_ERROR(num, args, proc)	   \
  int argNum = length( args );                             \
  if(argNum != num){                                       \
    printError("wrong number of arguments for %s: should be %d but giben %d", proc, num, argNum); \
    pushArg((Cell*)AQ_UNDEF);						\
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

void compileQuot(InstQueue* instQ, FILE* fp, Cell symbolList)
{
  compileElem(instQ, fp, symbolList);
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
    Inst* ret = createInst(PUSHS, stringCell(&token[1]), 9);
    return ret;
  }
  else if(token[0] == '#'){
    if(token[1] == '\\' && strlen(token)==3){
      Inst* ret = createInst(PUSH, charCell(token[2]), 9);
      return ret;
    }
    else if(strcmp(&token[1], "t") == 0){
      Inst* ret = createInst(PUSH_TRUE, (Cell)AQ_NIL, 1);
      return ret;
    }
    else if(strcmp(&token[1], "f") == 0){
      Inst* ret = createInst(PUSH_FALSE, (Cell)AQ_NIL, 1);
      return ret;
    }
    else{
      Inst* ret = createInst(PUSHS, stringCell(token), 9);
      return ret;
    }
  } else {
    int index = 0;
    while(symbolList && !NIL_P(symbolList)) {
      char* symbol = symbolname(car(symbolList));
      if(strcmp(symbol, token) == 0) {
	Inst* ret = createInst(LOAD, makeInteger(index), 9);
	return ret;
      }
      
      symbolList = cdr(symbolList);
      index++;
    }
    Inst* ret = createInst(REF, stringCell(token), 9);
    return ret;
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
    } else if(strcmp(func, "<=") == 0) {
      addOneByteInstTail(instQ, LTE);
    } else if(strcmp(func, ">=") == 0) {
      addOneByteInstTail(instQ, GTE);
    } else if(strcmp(func, "=") == 0) {
      addOneByteInstTail(instQ, EQ);
    } else {
      addPushTail(instQ, num);
      int len = strlen(func)+1;
      addInstTail(instQ, createInst(FUNC, stringCell(func), len+1));
    }
}

void compileIf(InstQueue* instQ, FILE* fp, Cell symbolList)
{
  compileElem(instQ, fp, symbolList);  // predicate
  
  Inst* jneqInst = createInst(JNEQ, makeInteger(0), 9);
  addInstTail(instQ, jneqInst);
  compileElem(instQ, fp, symbolList);  // statement (TRUE)
  
  Inst* jmpInst = createInst(JMP, makeInteger(0), 9);
  addInstTail(instQ, jmpInst);
  jneqInst->operand = makeInteger(instQ->tail->offset + instQ->tail->size);
  
  int c = fgetc(fp);
  ungetc(c, fp);
  if(c ==')' ){
    addInstTail(instQ, createInst(PUSH_NIL, makeInteger(0), 1));
  } else {
    compileElem(instQ, fp, symbolList);  // statement (FALSE)
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
    symbolList = pairCell(symbolCell(var), symbolList);
    
    index++;
  }
  compileList(instQ, fp, symbolList);  // body
  addInstTail(instQ, createInst(RET, (Cell)AQ_NIL, 1));
  
  int addr = instQ->tail->offset + instQ->tail->size;
  inst->operand = makeInteger(addr);
  inst->operand2 = makeInteger(index);
}


void compileDefine(InstQueue* instQ, FILE* fp, Cell symbolList)
{
  char buf[LINESIZE];
  char* var = readToken(buf, sizeof(buf), fp);
  
  compileElem(instQ, fp, symbolList);
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
    if(strcmp(func, "(") == 0) {
      ungetc('(', fp);
      compileElem(instQ, fp, symbolList);
      int num = compileList(instQ, fp, symbolList);
      addPushTail(instQ, num);
      addInstTail(instQ, createInst(SROT, makeInteger(num+1), 1));
      addInstTail(instQ, createInst(FUNCS, (Cell)AQ_EOF, 1));
    }
    else if(strcmp(func, ")") == 0) {
      addInstTail(instQ, createInst(PUSH_NIL, (Cell)AQ_EOF, 1));
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
    
    return;
  }
  else if(token[0]=='\''){
    char* token2 = readToken(buf, sizeof(buf), fp);
    addInstTail(instQ, createInst(PUSH_SYM, symbolCell(token2), 9));
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
  Cell nameCell = stringCell(name);
  Cell chain = getChain(name, &key);
  registerVar(nameCell, chain, c, &env[key]);
  popArg();
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
  memset(env, 0, ENVSIZE);
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
  g_GC_stress      = FALSE;
}

void load_file( const char* filename )
{
  FILE* fp = NULL;
#if defined( _WIN32 ) || defined( _WIN64 )
  fopen_s( &fp, filename, "r");
#else
  fp = fopen(filename, "rb");
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
  char* buf = (char*)malloc(sizeof(char) * 1024);
  size_t fileSize = writeInst(&instQ, buf);
  execute(buf);

  FILE* outputFile = fopen("test.abc", "wb");
  fwrite(buf, fileSize, 1, outputFile);
  fclose(outputFile);
}

size_t writeInst(InstQueue* instQ, char* buf)
{
  size_t size = 0;
  Inst* inst = instQ->head;
  while(inst) {
    OPCODE op = inst->op;
    buf[size] = (char)op;
    switch(op) {
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
    case SROT:
      {
	int num = ivalue(inst->operand);
	memcpy(&buf[++size], &num, sizeof(long));
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
    case PUSHS:
      {
	char* str = strvalue(inst->operand);
	strcpy(&buf[++size], str);
	size += (strlen(str)+1);
      }
      break;
    case PUSH_SYM:
      {
	char* sym = symbolname(inst->operand);
	strcpy(&buf[++size], sym);
	size += (strlen(sym)+1);
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
    case GTE:
    case LTE:
    case HALT:
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

void execute(char* buf)
{
  Boolean exec = TRUE;
  int pc = 0;
  stack_top = 0;
  while(exec != FALSE) {
    OPCODE op = buf[pc];
    switch(op) {
    case PUSH:
      {
	int value = (int)getOperand(buf, ++pc);
	pushArg((Cell*)makeInteger(value));
	pc += sizeof(Cell);
      }
      break;
    case PUSH_NIL:
      pushArg((Cell*)AQ_NIL);
      ++pc;
      break;
    case PUSH_TRUE:
      pushArg((Cell*)AQ_TRUE);
      ++pc;
      break;
    case PUSH_FALSE:
      pushArg((Cell*)AQ_FALSE);
      ++pc;
      break;
    case ADD:
      {
	Cell* numCell = popArg();
	int num = ivalue(numCell);
	long ans = 0;
	for(int i=0; i<num; i++) {
	  Cell* val = popArg();
	  ans += ivalue(val);
	}
	Cell* cellp = (Cell*)makeInteger(ans);
	pushArg(cellp);
	++pc;
      }
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
	++pc;
      }
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
	++pc;
      }
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
	++pc;
      }
      break;
    case RET:
      {
	Cell* val = popArg();
	int retAddr = ivalue(popArg());
	int argNum = ivalue(popArg());
	for(int i=0; i<argNum; i++) {
	  popArg();
	}
	updateOffsetReg();
	pushArg(val);
	pc = retAddr;
      }
      break;
    case CONS:
      {
	Cell* cdrCell = popArg();
	Cell* carCell = popArg();
	Cell ret = pairCell((Cell)carCell, (Cell)cdrCell);
	pushArg((Cell*)ret);
	++pc;
      }
      break;
    case CAR:
      {
	Cell c = (Cell)popArg();
	Cell ca = car(c);
	pushArg((Cell*)ca);
	++pc;
      }
      break;
    case CDR:
      {
	Cell c = (Cell)popArg();
	Cell cd = cdr(c);
	pushArg((Cell*)cd);
	++pc;
      }
      break;
    case EQ:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 == num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
	++pc;
      }
      break;
    case GT:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 > num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
	++pc;
      }
      break;
    case GTE:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 >= num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
	++pc;
      }
      break;
    case LT:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 < num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
      }
      pc++;
      break;
    case LTE:
      {
	int num2 = ivalue(popArg());
	int num1 = ivalue(popArg());
	Cell ret = (num1 <= num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
	pushArg((Cell*)ret);
      }
      pc++;
      break;
    case QUOTE:
      {
	Cell c = (Cell)popArg();
	pushArg((Cell*)c);
	++pc;
      }
      break;
    case PRINT:
      {
	Cell* val = popArg();
	printCell((Cell)val);
	AQ_PRINTF("\n");
	pushArg((Cell*)AQ_UNDEF);
	++pc;
      }
      break;
    case JNEQ:
      {
	Cell c = (Cell)popArg();
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
	// TODO: different behavior between on-memory and bytecode.
	// this is for on-memory
	Cell val = (Cell)popArg();
	char* str = &buf[++pc];
	Cell strCell = stringCell(str);
	setVarCell(strCell, val);
	pc += (strlen(str)+1);
      }
      break;
    case PUSHS:
      {
	char* str = &buf[++pc];
	Cell strCell = stringCell(str);
	pushArg((Cell*)strCell);
	pc += (strlen(str)+1);
      }
      break;
    case PUSH_SYM:
      {
	char* sym = &buf[++pc];
	Cell symCell = symbolCell(sym);
	pushArg((Cell*)symCell);
	pc += (strlen(sym)+1);
      }
      break;
    case REF:
      {
	char* str = &buf[++pc];
	Cell ret = getVar(str);
	if(UNDEF_P(ret)) {
	  printError("[REF] undefined symbol: %s", str);
	  exec = FALSE;
	} else {
	  pushArg((Cell*)ret);
	  pc += (strlen(str)+1);
	}
      }
      break;
    case FUNC:
      {
	char* str = &buf[++pc];
	Cell func = getVar(str);
	if(UNDEF_P(func)) {
	  printError("undefined symbol: %s", str);
	  exec = FALSE;
	} else {
	  int paramNum = ivalue(lambdaParamNum(func));
	  int argNum = ivalue(stack[stack_top-1]);
	  if(paramNum != argNum) {
	    AQ_PRINTF("param num is wrong: %d, %d\n", paramNum, argNum);
	    ++pc;
	    break;
	  }
	  int retAddr  = pc + strlen(str) + 1;
	  pushArg((Cell*)makeInteger(retAddr));
	  updateOffsetReg();
	  
	  // jump
	  int funcAddr = ivalue(lambdaAddr(func));
	  pc = funcAddr;
	}
      }
      break;
    case FUND:
      {
	// jump
	int defEnd = (int)getOperand(buf, ++pc);
	int defStart = pc + sizeof(Cell) * 2;
	pc += sizeof(Cell);
	int paramNum = (int)getOperand(buf, pc);
	Cell l = lambdaCell(defStart, paramNum);
	pushArg((Cell*)l);
	pc = defEnd;
      }
      break;
    case FUNCS:
      {
	Cell l = (Cell)popArg();
	int argNum = ivalue(stack[stack_top-1]);
	int paramNum = ivalue(lambdaParamNum(l));
	if(paramNum != argNum) {
	  AQ_PRINTF("param num is wrong: %d, %d\n", paramNum, argNum);
	  ++pc;
	  break;
	}
	
	int retAddr  = pc + 1;
	pushArg((Cell*)makeInteger(retAddr));
	updateOffsetReg();
	
	// jump
	int funcAddr = ivalue(lambdaAddr(l));
	pc = funcAddr;
      }
      break;
    case SROT:
      {
	Cell operand = getOperand(buf, ++pc);
	int n = ivalue(operand);
	Cell* val = stack[stack_top-(n+1)];
	
	for(int i=n; i>0; i--) {
	  stack[stack_top-(i+1)] = stack[stack_top-i];
	}
	
	stack[stack_top-1]  = val;
	pc += sizeof(Cell);
      }
      break;
    case LOAD:
      {
	int offset = (int)getOperand(buf, ++pc);
	int index = getOffsetReg() - offset - 3;
	Cell* val = stack[index];
	pushArg(val);
	pc += sizeof(Cell);
      }
      break;
    case NOP:
      // do nothing
      pc++;
      break;
    case HALT:
      AQ_PRINTF("[HALT]\n");
      exec = FALSE;
      break;
    default:
      AQ_PRINTF("Unknown opcode: %d\n", op);
      exec = FALSE;
      break;
    }
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
    pushArg((Cell*)AQ_UNDEF);
  }
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
    Cell ret = (Cell)popArg();
    if(EOF_P(ret)) break;
    if(UNDEF_P(ret)) continue;
    Cell pair = pairCell(ret, (Cell)AQ_NIL);
    pushArg(&pair);

    callProc("eval");
    printLineCell((Cell)popArg());
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

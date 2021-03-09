#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "aquario.h"
#include "gc/base.h"

static void set_gc(char *);

aq_bool g_GC_stress;

static Cell get_chain(char *name, int *key);
static void register_var(Cell name_cell, Cell chain, Cell c, Cell *env);

static void init();
static void term();

static int heap_size = HEAP_SIZE;

#define FUNCTION_STACK_SIZE (1024)
static int max_stack_top = 0;
static int function_stack[FUNCTION_STACK_SIZE];
static int function_stack_top = 0;
static void push_function_stack(int f);
static int pop_function_stack();
static int get_function_stack_top();

#define STACK_OFFSET(offset) (stack[stack_top - ((offset)+1)])
#define STACK_TOP (STACK_OFFSET(0))
#define STACK_TOP_NEXT (STACK_OFFSET(1))

static aq_error_type err_type = ERR_TYPE_NONE;

#define SET_ERROR_WITH_STR(err, str) \
  err_type = err;                    \
  push_arg(string_cell(str));        \
  return;

#define ERR_WRONG_NUMBER_ARGS_BASE(required, given, str) \
  err_type = ERR_TYPE_WRONG_NUMBER_ARG;                  \
  push_arg(make_integer(required));                      \
  push_arg(make_integer(given));                         \
  push_arg(string_cell(str));                            \
  return;

#define ERR_WRONG_NUMBER_ARGS(required, given, str)   \
  if (required != given)                              \
  {                                                   \
    ERR_WRONG_NUMBER_ARGS_BASE(required, given, str); \
  }

#define ERR_WRONG_NUMBER_ARGS_DLIST(required, given, str) \
  if (required > given)                                   \
  {                                                       \
    ERR_WRONG_NUMBER_ARGS_BASE(required, given, str);     \
  }

#define ERR_PAIR_NOT_GIVEN(str)         \
  if (!PAIR_P(STACK_TOP))               \
  {                                     \
    err_type = ERR_TYPE_PAIR_NOT_GIVEN; \
    push_arg(string_cell(str));         \
    return;                             \
  }

#define ERR_INT_NOT_GIVEN(num, str)    \
  if (!INTEGER_P(num))                 \
  {                                    \
    err_type = ERR_TYPE_INT_NOT_GIVEN; \
    push_arg(num);                     \
    push_arg(string_cell(str));        \
    return;                            \
  }

#define EXECUTE_INT_COMPARISON(op_name, _op)                     \
  {                                                              \
    ERR_INT_NOT_GIVEN(STACK_TOP, op_name);                       \
    ERR_INT_NOT_GIVEN(STACK_TOP_NEXT, op_name);                  \
    int num2 = INT_VALUE(STACK_TOP);                             \
    int num1 = INT_VALUE(STACK_TOP_NEXT);                        \
    Cell ret = (num1 _op num2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE; \
    pop_arg();                                                   \
    pop_arg();                                                   \
    push_arg(ret);                                               \
    ++(*pc);                                                     \
  }

#define EXECUTE_PUSH_IMMEDIATE_VALUE(value) \
  push_arg((Cell)value);                    \
  ++(*pc);

#define EXECUTE_MATH_OPERATOR_WITH_CONSTANT(op_name, _op, num) \
  {                                                            \
    ERR_INT_NOT_GIVEN(STACK_TOP, op_name);                     \
    int ans = num _op INT_VALUE(STACK_TOP);                    \
    pop_arg();                                                 \
    push_arg(make_integer(ans));                               \
    ++(*pc);                                                   \
  }

#define SKIP_CLOSE_PARENTHESIS() \
  while (AQ_FGETC(fp) != ')')    \
  {                              \
  }

#define EXECUTE_MATH_OPERATION(op_name, _op, initial) \
  {                                                   \
    ERR_INT_NOT_GIVEN(STACK_TOP, op_name);            \
    int num = INT_VALUE(STACK_TOP);                   \
    pop_arg();                                        \
    long ans = initial;                               \
    for (i = 0; i < num; i++)                         \
    {                                                 \
      ERR_INT_NOT_GIVEN(STACK_TOP, op_name);          \
      ans _op INT_VALUE(STACK_TOP);                   \
      pop_arg();                                      \
    }                                                 \
    push_arg(make_integer(ans));                      \
    ++(*pc);                                          \
  }

#if defined(_TEST)
static char outbuf[1024 * 1024];
static int outbuf_index = 0;
static char inbuf[1024 * 1024];
static int inbuf_index = 0;

int aq_fgetc()
{
  return inbuf[inbuf_index++];
}

void aq_ungetc(int c, FILE *fp)
{
  inbuf[--inbuf_index] = c;
}
#endif

inline Cell new_cell(aq_type t, size_t size)
{
  Cell new_cell = (Cell)gc_malloc(size);
  new_cell->_type = t;

  return new_cell;
}

Cell char_cell(char ch)
{
  Cell c = new_cell(T_CHAR, sizeof(struct cell));
  CHAR_VALUE(c) = ch;
  return c;
}

Cell string_cell(char *str)
{
  int obj_size = sizeof(struct cell) + sizeof(char) * strlen(str) - sizeof(cell_union) + 1;
  Cell c = new_cell(T_STRING, obj_size);
  STRCPY(STR_VALUE(c), str);
  return c;
}

Cell pair_cell(Cell *a, Cell *d)
{
  Cell cons = new_cell(T_PAIR, sizeof(struct cell));

  gc_init_ptr(&CDR(cons), *d);
  gc_init_ptr(&CAR(cons), *a);
  return cons;
}

Cell symbol_cell(char *symbol)
{
  int obj_size = sizeof(struct cell) + sizeof(char) * strlen(symbol) - sizeof(cell_union) + 1;
  Cell c = new_cell(T_SYMBOL, obj_size);
  STRCPY(SYMBOL_VALUE(c), symbol);
  return c;
}

Cell lambda_cell(int addr, int param_num, aq_bool is_param_dlist)
{
  Cell l = new_cell(T_LAMBDA, sizeof(struct cell));
  LAMBDA_ADDR(l) = make_integer(addr);
  LAMBDA_PARAM_NUM(l) = make_integer(param_num);
  LAMBDA_FLAG(l) = is_param_dlist;
  return l;
}

Cell make_integer(int val)
{
  long lval = val;
  return (Cell)((lval << 1) | AQ_INTEGER_MASK);
}

aq_bool is_digit_str(char *str)
{
  int len = strlen(str);
  for (int i = 0; i < len; ++i)
  {
    if (!isdigit(str[i]))
    {
      if (len < 2 || i != 0 || (str[0] != '-' && str[0] != '+'))
      {
        return FALSE;
      }
    }
  }
  return TRUE;
}

void print_cons(FILE *fp, Cell c)
{
  if (CELL_P(CAR(c)) && TYPE(CAR(c)) == T_SYMBOL &&
      strcmp(SYMBOL_VALUE(CAR(c)), "quote") == 0)
  {
    AQ_FPRINTF(fp, "'");
    print_cell(fp, CADR(c));
    return;
  }
  AQ_FPRINTF(fp, "(");
  while (PAIR_P(CDR(c)))
  {
    print_cell(fp, CAR(c));
    c = CDR(c);
    if (PAIR_P(c) && !NIL_P(CAR(c)))
    {
      AQ_FPRINTF(fp, " ");
    }
  }

  print_cell(fp, CAR(c));
  if (!NIL_P(CDR(c)))
  {
    AQ_FPRINTF(fp, " . ");
    print_cell(fp, CDR(c));
  }
  AQ_FPRINTF(fp, ")");
}

void print_line_cell(FILE *fp, Cell c)
{
  print_cell(fp, c);
  AQ_FPRINTF(fp, "\n");
}

void print_cell(FILE *fp, Cell c)
{
  if (!CELL_P(c))
  {
    if (UNDEF_P(c))
    {
      AQ_FPRINTF(fp, "#undef");
    }
    else if (NIL_P(c))
    {
      AQ_FPRINTF(fp, "()");
    }
    else if (TRUE_P(c))
    {
      AQ_FPRINTF(fp, "#t");
    }
    else if (FALSE_P(c))
    {
      AQ_FPRINTF(fp, "#f");
    }
    else if (INTEGER_P(c))
    {
      AQ_FPRINTF(fp, "%d", INT_VALUE(c));
    }
  }
  else
  {
    switch (TYPE(c))
    {
    case T_CHAR:
      AQ_FPRINTF(fp, "#\\%c", CHAR_VALUE(c));
      break;
    case T_STRING:
      AQ_FPRINTF(fp, "\"%s\"", STR_VALUE(c));
      break;
    case T_SYMBOL:
      AQ_FPRINTF(fp, "%s", SYMBOL_VALUE(c));
      break;
    case T_PAIR:
      print_cons(fp, c);
      break;
    case T_LAMBDA:
      AQ_FPRINTF(fp, "#closure");
      break;
    default:
      AQ_FPRINTF(fp, "\nunknown cell");
      break;
    }
  }
}

char *read_double_quoted_token(char *buf, int len, FILE *fp)
{
  int prev = EOF;
  char *strp = buf;
  *strp = '"';
  for (++strp; (strp - buf) < len - 1; ++strp)
  {
    int c = AQ_FGETC(fp);
    switch (c)
    {
    case '"':
      *strp = c;
      if (prev != '\\')
      {
        *strp = '\0';
        return buf;
      }
      break;
    case EOF:
      set_error(ERR_TYPE_UNEXPECTED_TOKEN);
      push_arg(string_cell("EOF"));
      return NULL;
    default:
      *strp = c;
      prev = c;
      break;
    }
  }
  *strp = '\0';
  return buf;
}

char *read_token(char *buf, int len, FILE *fp)
{
  char *token = buf;
  for (; (token - buf) < len - 1;)
  {
    int c = AQ_FGETC(fp);
    switch (c)
    {
    case ';':
      while (c != '\n' && c != EOF)
      {
        c = AQ_FGETC(fp);
      }
      if (token == buf)
      {
        break;
      }
      else
      {
        *token = '\0';
        return buf;
      }
    case '(':
    case ')':
    case '\'':
      if (token - buf > 0)
      {
        AQ_UNGETC(c, fp);
      }
      else
      {
        *token = c;
        ++token;
      }
      *token = '\0';
      return buf;
    case '"':
      if (token - buf > 0)
      {
        AQ_UNGETC(c, fp);
        *token = '\0';
        return buf;
      }
      return read_double_quoted_token(buf, len, fp);
    case ' ':
    case '\t':
    case '\n':
      if (token - buf > 0)
      {
        *token = '\0';
        return buf;
      }
      break;
    case EOF:
      if (token - buf > 0)
      {
        *token = '\0';
        AQ_UNGETC(EOF, fp);
        return buf;
      }
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

size_t compile(FILE *fp, char *buf, int offset)
{
  int c = AQ_FGETC(fp);
  if (c == EOF)
    return 0;
  AQ_UNGETC(c, fp);

  aq_inst *inst = create_inst(OP_NOP, 1);
  inst_queue queue;
  queue.head = inst;
  queue.tail = inst;
  inst->offset = offset;
  compile_elem(&queue, fp, NULL);

  if (is_error())
  {
    return 0;
  }
  return write_inst(queue.head, buf);
}

int compile_list(inst_queue *queue, FILE *fp, Cell symbol_list)
{
  char c;
  int n = 0;
  while (1)
  {
    c = AQ_FGETC(fp);
    switch (c)
    {
    case ')':
      return n;
    case '.':
      set_error(ERR_TYPE_UNEXPECTED_TOKEN);
      push_arg(string_cell("."));
      return n;
    case ' ':
    case '\n':
    case '\t':
      continue;
    case EOF:
      set_error(ERR_TYPE_UNEXPECTED_TOKEN);
      push_arg(string_cell("EOF"));
      return n;
    default:
    {
      AQ_UNGETC(c, fp);
      compile_elem(queue, fp, symbol_list);
      n++;
    }
    }
  }
  return n;
}

void compile_quoted_atom(inst_queue *queue, char *symbol, FILE *fp)
{
  aq_inst *inst = create_inst_token(queue, symbol);
  if (inst)
  {
    add_inst_tail(queue, inst);
  }
  else
  {
    aq_inst *inst = create_inst_str(OP_PUSH_SYM, symbol);
    add_inst_tail(queue, inst);
  }
}

void compile_quoted_list(inst_queue *queue, FILE *fp)
{
  char buf[LINESIZE];
  char *token = read_token(buf, sizeof(buf), fp);

  if (strcmp(token, "(") == 0)
  {
    compile_quoted_list(queue, fp);
    compile_quoted_list(queue, fp);
    add_one_byte_inst_tail(queue, OP_CONS);
  }
  else if (strcmp(token, ")") == 0)
  {
    add_one_byte_inst_tail(queue, OP_PUSH_NIL);
  }
  else if (strcmp(token, "'") == 0)
  {
    compile_quote(queue, fp);
    compile_quoted_list(queue, fp);
    add_one_byte_inst_tail(queue, OP_CONS);
  }
  else if (strcmp(token, ".") == 0)
  {
    token = read_token(buf, sizeof(buf), fp);
    if (strcmp(token, "(") == 0)
    {
      compile_quoted_list(queue, fp);
    }
    else if (strcmp(token, "'") == 0)
    {
      compile_quote(queue, fp);
    }
    else
    {
      compile_quoted_atom(queue, token, fp);
    }
    token = read_token(buf, sizeof(buf), fp);
    if (strcmp(token, ")") != 0)
    {
      compile_list(queue, fp, NULL);
      SET_ERROR_WITH_STR(ERR_TYPE_MALFORMED_DOT_LIST, "");
    }
  }
  else
  {
    compile_quoted_atom(queue, token, fp);
    compile_quoted_list(queue, fp);
    add_one_byte_inst_tail(queue, OP_CONS);
  }
}

void compile_quote(inst_queue *queue, FILE *fp)
{
  char buf[LINESIZE];
  char *token = read_token(buf, sizeof(buf), fp);

  aq_inst *inst = create_inst_str(OP_PUSH_SYM, "quote");
  add_inst_tail(queue, inst);

  if (token[0] == '(')
  {
    compile_quoted_list(queue, fp);
  }
  else if (strcmp(token, "'") == 0)
  {
    compile_quote(queue, fp);
  }
  else
  {
    compile_quoted_atom(queue, token, fp);
  }

  add_one_byte_inst_tail(queue, OP_PUSH_NIL);
  add_one_byte_inst_tail(queue, OP_CONS);
  add_one_byte_inst_tail(queue, OP_CONS);
}

aq_inst *create_inst_char(aq_opcode op, char c)
{
  aq_inst *result = create_inst(op, 3);
  result->operand1._char = c;

  return result;
}

aq_inst *create_inst_str(aq_opcode op, char *str)
{
  int len = strlen(str) + 1;
  aq_inst *result = create_inst(op, len + 1);
  result->operand1._string = malloc(sizeof(char) * len);
  STRCPY(result->operand1._string, str);

  return result;
}

aq_inst *create_inst_num(aq_opcode op, int num)
{
  aq_inst *result = create_inst(op, 1 + sizeof(Cell));
  result->operand1._num = make_integer(num);

  return result;
}

aq_inst *create_inst(aq_opcode op, int size)
{
  aq_inst *result = (aq_inst *)malloc(sizeof(aq_inst));
  result->op = op;
  result->prev = NULL;
  result->next = NULL;
  result->operand1._num = (Cell)AQ_NIL;
  result->operand2._num = (Cell)AQ_NIL;
  result->size = size;
  result->offset = 0;

  return result;
}

aq_inst *create_inst_token(inst_queue *queue, char *token)
{
  if (is_digit_str(token))
  {
    int digit = atoi(token);
    return create_inst_num(OP_PUSH, digit);
  }
  else if (strcmp(token, "nil") == 0)
  {
    return create_inst(OP_PUSH_NIL, 1);
  }
  else if (token[0] == '"')
  {
    return create_inst_str(OP_PUSH_STR, &token[1]);
  }
  else if (token[0] == '#')
  {
    if (token[1] == '\\' && strlen(token) == 3)
    {
      return create_inst_char(OP_PUSH, token[2]); // TODO: PUSHC
    }
    else if (strcmp(&token[1], "t") == 0)
    {
      return create_inst(OP_PUSH_TRUE, 1);
    }
    else if (strcmp(&token[1], "f") == 0)
    {
      return create_inst(OP_PUSH_FALSE, 1);
    }
    else
    {
      return create_inst_str(OP_PUSH_STR, token);
    }
  }
  else
  {
    return NULL;
  }
}

void compile_token(inst_queue *queue, char *token, Cell symbol_list)
{
  aq_inst *inst = create_inst_token(queue, token);
  if (inst)
  {
    return add_inst_tail(queue, inst);
  }
  else
  {
    int index = 0;
    while (symbol_list && !NIL_P(symbol_list))
    {
      char *symbol = (char *)CAR(symbol_list);
      if (strcmp(symbol, token) == 0)
      {
        aq_inst *inst = create_inst_num(OP_LOAD, index);
        add_inst_tail(queue, inst);
        return;
      }

      symbol_list = CDR(symbol_list);
      index++;
    }
    aq_inst *inst = create_inst_str(OP_REF, token);
    add_inst_tail(queue, inst);
  }
}

void add_inst_tail(inst_queue *queue, aq_inst *inst)
{
  inst->offset = queue->tail->offset + queue->tail->size;
  queue->tail->next = inst;
  inst->prev = queue->tail;
  queue->tail = inst;
}

void add_push_tail(inst_queue *queue, int num)
{
  return add_inst_tail(queue, create_inst_num(OP_PUSH, num));
}

void add_one_byte_inst_tail(inst_queue *queue, aq_opcode op)
{
  aq_inst *ret = create_inst(op, 1);
  return add_inst_tail(queue, ret);
}

void compile_procedure(char *func, int num, inst_queue *queue)
{
  if (strcmp(func, "+") == 0)
  {
    compile_add(queue, num);
  }
  else if (strcmp(func, "-") == 0)
  {
    compile_sub(queue, num);
  }
  else if (strcmp(func, "*") == 0)
  {
    add_push_tail(queue, num);
    add_one_byte_inst_tail(queue, OP_MUL);
  }
  else if (strcmp(func, "/") == 0)
  {
    add_push_tail(queue, num - 1);
    add_one_byte_inst_tail(queue, OP_DIV);
  }
  else if (strcmp(func, "print") == 0)
  {
    add_push_tail(queue, num);
    add_one_byte_inst_tail(queue, OP_PRINT);
  }
  else if (strcmp(func, "cons") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(2, num, "cons");
    add_one_byte_inst_tail(queue, OP_CONS);
  }
  else if (strcmp(func, "car") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(1, num, "car");
    add_one_byte_inst_tail(queue, OP_CAR);
  }
  else if (strcmp(func, "cdr") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(1, num, "cdr");
    add_one_byte_inst_tail(queue, OP_CDR);
  }
  else if (strcmp(func, ">") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(2, num, ">");
    add_one_byte_inst_tail(queue, OP_GT);
  }
  else if (strcmp(func, "<") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(2, num, "<");
    add_one_byte_inst_tail(queue, OP_LT);
  }
  else if (strcmp(func, "<=") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(2, num, "<=");
    add_one_byte_inst_tail(queue, OP_LTE);
  }
  else if (strcmp(func, ">=") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(2, num, ">=");
    add_one_byte_inst_tail(queue, OP_GTE);
  }
  else if (strcmp(func, "=") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(2, num, "=");
    add_one_byte_inst_tail(queue, OP_EQUAL);
  }
  else if (strcmp(func, "eq?") == 0)
  {
    ERR_WRONG_NUMBER_ARGS(2, num, "eq?");
    add_one_byte_inst_tail(queue, OP_EQ);
  }
  else
  {
    add_push_tail(queue, num);
    add_inst_tail(queue, create_inst_str(OP_FUNC, func));
  }
}

void compile_add(inst_queue *queue, int num)
{
  aq_inst *last_inst = queue->tail;
  if (last_inst && last_inst->op == OP_PUSH && num == 2)
  {
    if (last_inst->operand1._num == make_integer(1))
    {
      last_inst->op = OP_ADD1;
      last_inst->offset = last_inst->prev->offset + last_inst->prev->size;
      last_inst->size = 1;
      return;
    }
    else if (last_inst->operand1._num == make_integer(2))
    {
      last_inst->op = OP_ADD2;
      last_inst->offset = last_inst->prev->offset + last_inst->prev->size;
      last_inst->size = 1;
      return;
    }
  }
  add_push_tail(queue, num);
  add_one_byte_inst_tail(queue, OP_ADD);
}

void compile_sub(inst_queue *queue, int num)
{
  aq_inst *last_inst = queue->tail;
  if (last_inst && last_inst->op == OP_PUSH && num == 2)
  {
    if (last_inst->operand1._num == make_integer(1))
    {
      last_inst->op = OP_SUB1;
      last_inst->offset = last_inst->prev->offset + last_inst->prev->size;
      last_inst->size = 1;
      return;
    }
    else if (last_inst->operand1._num == make_integer(2))
    {
      last_inst->op = OP_SUB2;
      last_inst->offset = last_inst->prev->offset + last_inst->prev->size;
      last_inst->size = 1;
      return;
    }
  }
  add_push_tail(queue, num - 1);
  add_one_byte_inst_tail(queue, OP_SUB);
}

void compile_if(inst_queue *queue, FILE *fp, Cell symbol_list)
{
  compile_elem(queue, fp, symbol_list); // predicate

  aq_inst *jneq_inst = create_inst_num(OP_JNEQ, 0 /* placeholder */);
  add_inst_tail(queue, jneq_inst);
  compile_elem(queue, fp, symbol_list); // statement (TRUE)
  if (is_error())
  {
    SET_ERROR_WITH_STR(ERR_TYPE_MALFORMED_IF, "if");
  }

  aq_inst *jmp_inst = create_inst_num(OP_JMP, 0 /* placeholder */);
  add_inst_tail(queue, jmp_inst);
  jneq_inst->operand1._num = make_integer(queue->tail->offset + queue->tail->size);

  int c = AQ_FGETC(fp);
  AQ_UNGETC(c, fp);
  if (c == ')')
  {
    add_one_byte_inst_tail(queue, OP_PUSH_NIL);
  }
  else
  {
    compile_elem(queue, fp, symbol_list); // statement (FALSE)
  }

  jmp_inst->operand1._num = make_integer(queue->tail->offset + queue->tail->size);

  int num = compile_list(queue, fp, symbol_list);
  if (num > 0)
  {
    SET_ERROR_WITH_STR(ERR_TYPE_MALFORMED_IF, "if");
  }
}

void compile_lambda(inst_queue *queue, FILE *fp)
{
  int c = AQ_FGETC(fp);
  if (c == ')')
  {
    SET_ERROR_WITH_STR(ERR_TYPE_SYNTAX_ERROR, "lambda");
  }
  else if (c != '(')
  {
    SKIP_CLOSE_PARENTHESIS();
    SET_ERROR_WITH_STR(ERR_TYPE_SYMBOL_LIST_NOT_GIVEN, "lambda");
  }

  aq_inst *inst = create_inst(OP_FUND, 1 + sizeof(Cell) * 2);
  add_inst_tail(queue, inst);

  int index = 0;
  Cell symbol_list = (Cell)AQ_NIL;
  while ((c = AQ_FGETC(fp)) != ')')
  {
    AQ_UNGETC(c, fp);
    char buf[LINESIZE];
    char *var = read_token(buf, sizeof(buf), fp);

    if (strcmp(var, ".") == 0)
    {
      var = read_token(buf, sizeof(buf), fp);
      compile_symbol_list(var, &symbol_list);

      var = read_token(buf, sizeof(buf), fp);
      if (strcmp(var, ")") != 0)
      {
        compile_list(queue, fp, NULL);
        compile_list(queue, fp, NULL);
        SET_ERROR_WITH_STR(ERR_TYPE_MALFORMED_DOT_LIST, "lambda");
      }

      index++;
      inst->op = OP_FUNDD;
      break;
    }
    else
    {
      compile_symbol_list(var, &symbol_list);

      index++;
    }
  }

  compile_list(queue, fp, symbol_list); // body
  while (symbol_list != (Cell)AQ_NIL)
  {
    Cell tmp = symbol_list;
    symbol_list = CDR(symbol_list);
    free(CAR(tmp));
    free(tmp);
  }
  add_one_byte_inst_tail(queue, OP_RET);

  int addr = queue->tail->offset + queue->tail->size;
  inst->operand1._num = make_integer(addr);
  inst->operand2._num = make_integer(index);
}

void compile_define(inst_queue *queue, FILE *fp, Cell symbol_list)
{
  aq_inst *last_inst = queue->tail;
  int c = AQ_FGETC(fp);
  if (c == ')')
  {
    SET_ERROR_WITH_STR(ERR_TYPE_SYMBOL_NOT_GIVEN, "define");
  }
  AQ_UNGETC(c, fp);

  compile_elem(queue, fp, NULL);
  if (queue->tail->op != OP_REF)
  {
    SKIP_CLOSE_PARENTHESIS();
    SET_ERROR_WITH_STR(ERR_TYPE_SYMBOL_NOT_GIVEN, "define");
  }

  c = AQ_FGETC(fp);
  if (c == ')')
  {
    SET_ERROR_WITH_STR(ERR_TYPE_SYNTAX_ERROR, "define");
  }
  AQ_UNGETC(c, fp);

  queue->tail = last_inst;
  last_inst = last_inst->next;
  compile_elem(queue, fp, symbol_list);
  if (is_error())
  {
    SKIP_CLOSE_PARENTHESIS();
    return;
  }

  last_inst->op = OP_SET;
  add_inst_tail(queue, last_inst);

  char buf[LINESIZE];
  char *token = read_token(buf, sizeof(buf), fp);
  if (strcmp(token, ")") != 0)
  {
    SKIP_CLOSE_PARENTHESIS();
    SET_ERROR_WITH_STR(ERR_TYPE_TOO_MANY_EXPRESSIONS, "define");
  }
}

void compile_elem(inst_queue *queue, FILE *fp, Cell symbol_list)
{
  char buf[LINESIZE];
  char *token = read_token(buf, sizeof(buf), fp);
  if (token == NULL)
  {
    add_one_byte_inst_tail(queue, OP_HALT);
  }
  else if (token[0] == '(')
  {
    char *func = read_token(buf, sizeof(buf), fp);
    if (strcmp(func, "(") == 0)
    {
      AQ_UNGETC('(', fp);
      compile_elem(queue, fp, symbol_list);
      int num = compile_list(queue, fp, symbol_list);
      add_push_tail(queue, num);
      add_inst_tail(queue, create_inst_num(OP_SROT, num + 1));
      add_one_byte_inst_tail(queue, OP_FUNCS);
    }
    else if (strcmp(func, ")") == 0)
    {
      add_one_byte_inst_tail(queue, OP_PUSH_NIL);
    }
    else if (strcmp(func, "quote") == 0)
    {
      compile_quote(queue, fp);
      add_one_byte_inst_tail(queue, OP_CDR);
      add_one_byte_inst_tail(queue, OP_CAR);

      token = read_token(buf, sizeof(buf), fp);
      if (strcmp(token, ")") != 0)
      {
        int num = compile_list(queue, fp, NULL);
        ERR_WRONG_NUMBER_ARGS(1, num + 2, "quote");
      }
    }
    else if (strcmp(func, "if") == 0)
    {
      compile_if(queue, fp, symbol_list);
    }
    else if (strcmp(func, "define") == 0)
    {
      compile_define(queue, fp, symbol_list);
    }
    else if (strcmp(func, "lambda") == 0)
    {
      compile_lambda(queue, fp);
    }
    else
    {
      int num = compile_list(queue, fp, symbol_list);
      compile_procedure(func, num, queue);
    }
  }
  else if (token[0] == '\'')
  {
    compile_quote(queue, fp);

    add_one_byte_inst_tail(queue, OP_CDR);
    add_one_byte_inst_tail(queue, OP_CAR);
  }
  else if (token[0] == ')')
  {
    SET_ERROR_WITH_STR(ERR_TYPE_EXTRA_CLOSE_PARENTHESIS, "");
  }
  else
  {
    compile_token(queue, token, symbol_list);
  }
}

void compile_symbol_list(char *var, Cell *symbol_list)
{
  Cell tmp = malloc(sizeof(struct cell));
  size_t len = strlen(var) + 1;
  char *sym = malloc(len);

  STRCPY(sym, var);
  CAR(tmp) = (Cell)sym;
  CDR(tmp) = *symbol_list;
  *symbol_list = tmp;
}

int hash(char *key)
{
  int val = 0;
  for (; *key != '\0'; ++key)
  {
    val = val * 256 + *key;
  }
  return val;
}

Cell get_var(char *name)
{
  int key = 0;
  Cell chain = get_chain(name, &key);
  if (UNDEF_P(chain))
  {
    return (Cell)AQ_UNDEF;
  }
  else
  {
    return CDAR(chain);
  }
}

void set_var(char *name, Cell c)
{
  int key = 0;
  push_arg(c);
  Cell name_cell = string_cell(name);
  c = pop_arg();
  Cell chain = get_chain(name, &key);
  register_var(name_cell, chain, c, &env[key]);
}

void register_var(Cell name_cell, Cell chain, Cell c, Cell *env)
{
  if (!UNDEF_P(chain))
  {
    gc_write_barrier(chain, &CDAR(chain), c);
  }
  else
  {
    push_arg(name_cell);
    push_arg(c);
    Cell entry = pair_cell(&STACK_TOP_NEXT, &STACK_TOP);
    pop_arg();
    pop_arg();

    push_arg(entry);
    push_arg(*env);
    Cell p = pair_cell(&STACK_TOP_NEXT, &STACK_TOP);
    pop_arg();
    pop_arg();

    gc_write_barrier_root(env, p);
  }
}

Cell get_chain(char *name, int *key)
{
  *key = hash(name) % ENVSIZE;
  Cell chain = env[*key];
  while (PAIR_P(chain) && strcmp(name, STR_VALUE(CAAR(chain))) != 0)
  {
    chain = CDR(chain);
  }
  return chain;
}

void init()
{
  int i;
  for (i = 0; i < ENVSIZE; ++i)
  {
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

void set_gc(char *gc_char)
{
  aq_gc_info gc_info;
  memset(&gc_info, 0, sizeof(aq_gc_info));
  gc_init(gc_char, heap_size, &gc_info);
}

void load_file(char *filename)
{
  FILE *fp = NULL;
#if defined(_WIN32) || defined(_WIN64)
  fopen_s(&fp, filename, "r");
#else
  int len = strlen(filename);
  char *abc_file_name = malloc(sizeof(char *) * (len + 3));
  STRCPY(abc_file_name, filename);
  char *ext = strrchr(abc_file_name, '.');
  if (ext)
  {
    *ext = '\0';
  }
  strcat(abc_file_name, ".abc");

  struct stat abc_info;
  struct stat lsp_info;
  aq_bool compiled = (stat(abc_file_name, &abc_info) == 0 &&
                      stat(filename, &lsp_info) == 0 &&
                      abc_info.st_ctime > lsp_info.st_ctime);

  char *buf = (char *)malloc(sizeof(char) * 1024 * 1024);
  int pc = 0;
  size_t file_size = 0;
  if (compiled)
  {
    fp = fopen(abc_file_name, "rb");
    if (fp)
    {
      fread(buf, abc_info.st_size, 1, fp);
      fclose(fp);
      file_size = abc_info.st_size;
    }
    else
    {
      set_error(ERR_FILE_NOT_FOUND);
      push_arg(string_cell(abc_file_name));
    }
  }
  else
  {
    fp = fopen(filename, "r");
    if (fp)
    {
      size_t delta = 0;
      while ((delta = compile(fp, &buf[file_size], file_size)) > 0)
      {
        file_size += delta;
      }
      fclose(fp);

      FILE *output_file = fopen(abc_file_name, "wb");
      fwrite(buf, file_size, 1, output_file);
      fclose(output_file);
    }
    else
    {
      set_error(ERR_FILE_NOT_FOUND);
      push_arg(string_cell(filename));
    }
  }
  if (!is_error())
  {
    execute(buf, &pc, file_size);
    handle_error();
  }
  else
  {
    handle_error();
  }

  free(abc_file_name);
  free(buf);
#endif
}

#if defined(_TEST)
int do_test(char *input, char *correct_output)
{
  int out_length = strlen(correct_output);
  int i = out_length - 1;
  while (i > 0)
  {
    if (correct_output[i] == 'n' && correct_output[i - 1] == '\\')
    {
      correct_output[i - 1] = '\n';
      memcpy(&correct_output[i], &correct_output[i + 1], out_length - i);
      out_length--;
    }
    i--;
  }

  int in_length = strlen(input);
  STRCPY(inbuf, input);
  inbuf[in_length] = EOF;
  char *buf = (char *)malloc(sizeof(char) * 1024 * 1024);

  size_t buf_size = 0;
  size_t delta = 0;
  int pc = 0;
  while ((buf_size = compile(stdin, &buf[pc], pc)) > 0)
  {
    execute(buf, &pc, pc + buf_size);
    if (is_error())
    {
      handle_error();
    }
    else
    {
      print_cell(stdout, STACK_TOP);
      pop_arg();
    }
  }

  return strcmp(outbuf, correct_output);
}
#endif

size_t write_inst(aq_inst *inst, char *buf)
{
  size_t size = 0;
  while (inst)
  {
    aq_opcode op = inst->op;
    buf[size] = (char)op;
    switch (op)
    {
    case OP_PUSH:
    case OP_JNEQ:
    case OP_JMP:
    case OP_SROT:
    case OP_LOAD:
    {
      long val = INT_VALUE(inst->operand1._num);
      memcpy(&buf[++size], &val, sizeof(Cell));
      size += sizeof(Cell);
      break;
    }
    case OP_SET:
    case OP_REF:
    case OP_FUNC:
    case OP_PUSH_STR:
    case OP_PUSH_SYM:
    {
      char *str = inst->operand1._string;
      STRCPY(&buf[++size], str);
      size += (strlen(str) + 1);
      free(inst->operand1._string);
      break;
    }
    case OP_FUND:
    case OP_FUNDD:
    {
      int addr = INT_VALUE(inst->operand1._num);
      memcpy(&buf[++size], &addr, sizeof(Cell));
      size += sizeof(Cell);

      int param_num = INT_VALUE(inst->operand2._num);
      memcpy(&buf[size], &param_num, sizeof(Cell));
      size += sizeof(Cell);
      break;
    }
    case OP_NOP:
    case OP_ADD:
    case OP_ADD1:
    case OP_ADD2:
    case OP_SUB:
    case OP_SUB1:
    case OP_SUB2:
    case OP_MUL:
    case OP_DIV:
    case OP_PRINT:
    case OP_POP:
    case OP_CONS:
    case OP_CAR:
    case OP_CDR:
    case OP_PUSH_NIL:
    case OP_PUSH_TRUE:
    case OP_PUSH_FALSE:
    case OP_GT:
    case OP_LT:
    case OP_GTE:
    case OP_LTE:
    case OP_HALT:
    case OP_EQUAL:
    case OP_EQ:
    case OP_RET:
    case OP_FUNCS:
      buf[size] = (char)inst->op;
      size += 1;
      break;
    }

    inst = inst->next;
  }

  return size;
}

int get_operand(char *buf, int pc)
{
  return (int)(*(Cell *)&buf[pc]);
}

void execute(char *buf, int *pc, int end)
{
  aq_bool exec = TRUE;
  stack_top = 0;
  int i = 0;
  while (exec != FALSE && (*pc) < end && !is_error())
  {
    aq_opcode op = buf[*pc];
    switch (op)
    {
    case OP_PUSH:
    {
      int value = get_operand(buf, ++(*pc));
      push_arg(make_integer(value));
      *pc += sizeof(Cell);
      break;
    }
    case OP_PUSH_NIL:
      EXECUTE_PUSH_IMMEDIATE_VALUE(AQ_NIL);
      break;
    case OP_PUSH_TRUE:
      EXECUTE_PUSH_IMMEDIATE_VALUE(AQ_TRUE);
      break;
    case OP_PUSH_FALSE:
      EXECUTE_PUSH_IMMEDIATE_VALUE(AQ_FALSE);
      break;
    case OP_ADD1:
      EXECUTE_MATH_OPERATOR_WITH_CONSTANT("+", +, 1);
      break;
    case OP_ADD2:
      EXECUTE_MATH_OPERATOR_WITH_CONSTANT("+", +, 2);
      break;
    case OP_SUB1:
      EXECUTE_MATH_OPERATOR_WITH_CONSTANT("-", +, -1);
      break;
    case OP_SUB2:
      EXECUTE_MATH_OPERATOR_WITH_CONSTANT("-", +, -2);
      break;
    case OP_ADD:
      EXECUTE_MATH_OPERATION("+", +=, 0);
      break;
    case OP_SUB:
    {
      ERR_INT_NOT_GIVEN(STACK_TOP, "-");
      int num = INT_VALUE(STACK_TOP);
      if (num == 0)
      {
        pop_arg();
        EXECUTE_MATH_OPERATOR_WITH_CONSTANT("-", *, -1)
      }
      else
      {
        EXECUTE_MATH_OPERATION("-", +=, 0);
        int result = INT_VALUE(pop_arg());
        push_arg(make_integer(INT_VALUE(pop_arg()) - result));
      }
      break;
    }
    case OP_MUL:
      EXECUTE_MATH_OPERATION("*", *=, 1);
      break;
    case OP_DIV:
    {
      ERR_INT_NOT_GIVEN(STACK_TOP, "/");
      int num = INT_VALUE(STACK_TOP);
      if (num == 0)
      {
        pop_arg();
        EXECUTE_MATH_OPERATOR_WITH_CONSTANT("/", /, 1);
      }
      else
      {
        EXECUTE_MATH_OPERATION("/", *=, 1);
        int result = INT_VALUE(pop_arg());
        push_arg(make_integer(INT_VALUE(pop_arg()) / result));
      }
      break;
    }
    case OP_RET:
    {
      Cell val = stack[--stack_top];
      while (!SFRAME_P(pop_arg()))
      {
      }
      int ret_addr = INT_VALUE(pop_arg());
      int arg_num = INT_VALUE(pop_arg());
      for (i = 0; i < arg_num; ++i)
      {
        pop_arg();
      }
      pop_function_stack();
      stack[stack_top++] = val;
      *pc = ret_addr;
      break;
    }
    case OP_CONS:
    {
      Cell ret = pair_cell(&STACK_TOP_NEXT, &STACK_TOP);
      pop_arg();
      pop_arg();

      push_arg(ret);
      ++(*pc);
      break;
    }
    case OP_CAR:
    {
      ERR_PAIR_NOT_GIVEN("car");
      gc_write_barrier_root(&STACK_TOP, CAR(STACK_TOP));
      ++(*pc);
      break;
    }
    case OP_CDR:
    {
      ERR_PAIR_NOT_GIVEN("cdr");
      gc_write_barrier_root(&STACK_TOP, CDR(STACK_TOP));
      ++(*pc);
      break;
    }
    case OP_EQUAL:
      EXECUTE_INT_COMPARISON("=", ==);
      break;
    case OP_GT:
      EXECUTE_INT_COMPARISON(">", >);
      break;
    case OP_GTE:
      EXECUTE_INT_COMPARISON(">=", >=);
      break;
    case OP_LT:
      EXECUTE_INT_COMPARISON("<", <);
      break;
    case OP_LTE:
      EXECUTE_INT_COMPARISON("<=", <=);
      break;
    case OP_EQ:
    {
      Cell p1 = pop_arg();
      Cell p2 = pop_arg();
      Cell ret = (p1 == p2) ? (Cell)AQ_TRUE : (Cell)AQ_FALSE;
      push_arg(ret);
      ++(*pc);
      break;
    }
    case OP_PRINT:
    {
      int num = INT_VALUE(pop_arg());
      for (int i = num - 1; i >= 0; i--)
      {
        print_cell(stdout, STACK_OFFSET(i));
      }
      for (int i = 0; i < num; i++)
      {
        pop_arg();
      }
      AQ_PRINTF("\n");
      push_arg((Cell)AQ_UNDEF);
      ++(*pc);
      break;
    }
    case OP_JNEQ:
    {
      Cell c = STACK_TOP;
      pop_arg();
      ++(*pc);
      if (!TRUE_P(c))
      {
        int addr = get_operand(buf, *pc);
        *pc = addr;
      }
      else
      {
        *pc += sizeof(Cell);
      }
      break;
    }
    case OP_JMP:
    {
      int addr = get_operand(buf, ++(*pc));
      *pc = addr;
      break;
    }
    case OP_SET:
    {
      // this is for on-memory
      Cell val = STACK_TOP;
      char *str = &buf[++(*pc)];
      set_var(str, val);
      pop_arg();
      push_arg(symbol_cell(str));
      *pc += (strlen(str) + 1);
      break;
    }
    case OP_PUSH_STR:
    {
      char *str = &buf[++(*pc)];
      Cell str_cell = string_cell(str);
      push_arg(str_cell);
      *pc += (strlen(str) + 1);
      break;
    }
    case OP_PUSH_SYM:
    {
      char *sym = &buf[++(*pc)];
      Cell symCell = symbol_cell(sym);
      push_arg(symCell);
      *pc += (strlen(sym) + 1);
      break;
    }
    case OP_REF:
    {
      char *str = &buf[++(*pc)];
      Cell ret = get_var(str);
      if (UNDEF_P(ret))
      {
        SET_ERROR_WITH_STR(ERR_UNDEFINED_SYMBOL, str);
        push_arg((Cell)AQ_UNDEF);
        exec = FALSE;
      }
      else
      {
        push_arg(ret);
        *pc += (strlen(str) + 1);
      }
      break;
    }
    case OP_FUNC:
    {
      char *str = &buf[++(*pc)];
      Cell func = get_var(str);
      if (UNDEF_P(func))
      {
        SET_ERROR_WITH_STR(ERR_UNDEFINED_SYMBOL, str);
        int num = INT_VALUE(pop_arg());
        for (i = 0; i < num; i++)
        {
          pop_arg();
        }
        push_arg((Cell)AQ_UNDEF);
        exec = FALSE;
      }
      else
      {
        int param_num = INT_VALUE(LAMBDA_PARAM_NUM(func));
        int arg_num = INT_VALUE(STACK_TOP);
        int func_addr = INT_VALUE(LAMBDA_ADDR(func));
        aq_bool is_param_dlist = LAMBDA_FLAG(func);
        if (is_param_dlist)
        {
          ERR_WRONG_NUMBER_ARGS_DLIST(param_num, arg_num, "str");
          pop_arg();
          int num = arg_num - param_num + 1;
          Cell lst = (Cell)AQ_NIL;
          for (i = 0; i < num; i++)
          {
            push_arg(lst);
            lst = pair_cell(&STACK_TOP_NEXT, &STACK_TOP);
            pop_arg();
            pop_arg();
          }
          push_arg(lst);
          push_arg(make_integer(param_num));
        }
        else
        {
          ERR_WRONG_NUMBER_ARGS(param_num, arg_num, "str");
        }
        int ret_addr = *pc + strlen(str) + 1;
        push_arg(make_integer(ret_addr));
        push_arg((Cell)AQ_SFRAME);
        push_function_stack(stack_top);

        // jump
        *pc = func_addr;
      }
      break;
    }
    case OP_FUND:
    case OP_FUNDD:
    {
      // jump
      int def_end = get_operand(buf, ++(*pc));
      int def_start = *pc + sizeof(Cell) * 2;
      *pc += sizeof(Cell);
      int param_num = get_operand(buf, *pc);
      Cell l = lambda_cell(def_start, param_num, (op == OP_FUNDD) ? TRUE : FALSE);
      push_arg(l);
      *pc = def_end;
      break;
    }
    case OP_FUNCS:
    {
      Cell func = STACK_TOP;
      int param_num = INT_VALUE(LAMBDA_PARAM_NUM(func));
      int func_addr = INT_VALUE(LAMBDA_ADDR(func));
      aq_bool is_param_dlist = LAMBDA_FLAG(func);
      pop_arg();
      int arg_num = INT_VALUE(STACK_TOP);
      if (is_param_dlist)
      {
        ERR_WRONG_NUMBER_ARGS_DLIST(param_num, arg_num, "lambda");
        pop_arg();
        int num = arg_num - param_num + 1;
        Cell lst = (Cell)AQ_NIL;
        for (i = 0; i < num; i++)
        {
          push_arg(lst);
          lst = pair_cell(&STACK_TOP_NEXT, &STACK_TOP);
          pop_arg();
          pop_arg();
        }
        push_arg(lst);
        push_arg(make_integer(param_num));
      }
      else
      {
        ERR_WRONG_NUMBER_ARGS(param_num, arg_num, "lambda");
      }

      int ret_addr = *pc + 1;
      push_arg(make_integer(ret_addr));
      push_arg((Cell)AQ_SFRAME);
      push_function_stack(stack_top);

      // jump
      *pc = func_addr;
      break;
    }
    case OP_SROT:
    {
      int n = get_operand(buf, ++(*pc));
      Cell val = STACK_OFFSET(n);
      for (i = n; i > 0; i--)
      {
        STACK_OFFSET(i) = STACK_OFFSET(i);
      }
      STACK_TOP = val;
      *pc += sizeof(Cell);
      break;
    }
    case OP_LOAD:
    {
      int offset = get_operand(buf, ++(*pc));
      int index = get_function_stack_top() - offset - 4;
      Cell val = stack[index];
      push_arg(val);
      *pc += sizeof(Cell);
      break;
    }
    case OP_NOP:
      // do nothing
      ++(*pc);
      break;
    case OP_HALT:
      exec = FALSE;
      ++(*pc);
      break;
    default:
      AQ_PRINTF("Unknown opcode: %d\n", op);
      exec = FALSE;
      break;
    }
  }
}

aq_bool is_error()
{
  return (err_type != ERR_TYPE_NONE);
}

void set_error(aq_error_type e)
{
  err_type = e;
}

void handle_error()
{
  if (!is_error())
    return;

  FILE *fp = stdout; // TODO
  AQ_FPRINTF(fp, "[ERROR] ");

  switch (err_type)
  {
  case ERR_TYPE_WRONG_NUMBER_ARG:
    AQ_FPRINTF(fp, "%s: wrong number of argnuments: required ", STR_VALUE(pop_arg()));
    print_cell(fp, STACK_TOP_NEXT);
    AQ_FPRINTF(fp, ", but given ");
    print_line_cell(fp, STACK_TOP);
    break;
  case ERR_TYPE_PAIR_NOT_GIVEN:
    AQ_FPRINTF(fp, "%s: pair required, but given ", STR_VALUE(pop_arg()));
    print_line_cell(fp, STACK_TOP);
    break;
  case ERR_TYPE_INT_NOT_GIVEN:
    AQ_FPRINTF(fp, "%s: number required, but given ", STR_VALUE(pop_arg()));
    print_line_cell(fp, STACK_TOP);
    break;
  case ERR_TYPE_MALFORMED_IF:
    AQ_FPRINTF(fp, "malformed if\n");
    break;
  case ERR_TYPE_SYMBOL_LIST_NOT_GIVEN:
    AQ_FPRINTF(fp, "%s: symbol list not goven\n", STR_VALUE(pop_arg()));
    break;
  case ERR_TYPE_MALFORMED_DOT_LIST:
    AQ_FPRINTF(fp, "%s: malformed dot list\n", STR_VALUE(pop_arg()));
    break;
  case ERR_TYPE_TOO_MANY_EXPRESSIONS:
    AQ_FPRINTF(fp, "%s: too many expressions given\n", STR_VALUE(pop_arg()));
    break;
  case ERR_TYPE_EXTRA_CLOSE_PARENTHESIS:
    AQ_FPRINTF(fp, "extra close parenthesis\n");
    break;
  case ERR_TYPE_SYMBOL_NOT_GIVEN:
    AQ_FPRINTF(fp, "%s: symbol not given\n", STR_VALUE(pop_arg()));
    break;
  case ERR_TYPE_SYNTAX_ERROR:
    AQ_FPRINTF(fp, "%s: syntax error\n", STR_VALUE(pop_arg()));
    break;
  case ERR_TYPE_GENERAL_ERROR:
    // Not expected to reach here.
    AQ_FPRINTF(fp, "error\n");
    break;
  case ERR_STACK_OVERFLOW:
    AQ_FPRINTF(fp, "stack overflow\n");
    break;
  case ERR_STACK_UNDERFLOW:
    AQ_FPRINTF(fp, "stack underflow\n");
    break;
  case ERR_TYPE_UNEXPECTED_TOKEN:
    AQ_FPRINTF(fp, "unexpected token: %s\n", STR_VALUE(pop_arg()));
    break;
  case ERR_UNDEFINED_SYMBOL:
    AQ_FPRINTF(fp, "undefined symbol: %s\n", STR_VALUE(pop_arg()));
    break;
  case ERR_HEAP_EXHAUSTED:
    AQ_FPRINTF(fp, "heap exhausted\n");
    break;
  case ERR_FILE_NOT_FOUND:
    AQ_FPRINTF(fp, "cannot open file: %s\n", STR_VALUE(pop_arg()));
    break;
  case ERR_TYPE_NONE:
    return;
  }
  while (stack_top > 0)
  {
    pop_arg();
  }
  err_type = ERR_TYPE_NONE;
}

void push_function_stack(int f)
{
  if (function_stack_top >= FUNCTION_STACK_SIZE)
  {
    err_type = ERR_STACK_OVERFLOW;
    return;
  }
  function_stack[function_stack_top++] = f;
}

int pop_function_stack()
{
  if (function_stack_top <= 0)
  {
    err_type = ERR_STACK_UNDERFLOW;
    return -1;
  }
  return function_stack[--function_stack_top];
}

int get_function_stack_top()
{
  return function_stack[function_stack_top - 1];
}

void repl()
{
  char *buf = (char *)malloc(sizeof(char) * 1024 * 1024);
  int pc = 0;
  while (1)
  {
    AQ_PRINTF(">");
    size_t buf_size = compile(stdin, &buf[pc], pc);
    execute(buf, &pc, pc + buf_size);
    if (is_error())
    {
      handle_error();
    }
    else
    {
      print_line_cell(stdout, STACK_TOP);
      pop_arg();
    }
  }
  free(buf);
}

int handle_option(int argc, char *argv[])
{
  int i = 1;
  for (; i < argc - 1; i++)
  {
    if (strcmp(argv[i], "-GC") == 0)
    {
      set_gc(argv[++i]);
    }
    else if (strcmp(argv[i], "-GC_STRESS") == 0)
    {
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
#if defined(_TEST)
  return do_test(argv[i - 1], argv[i]);
#else
  if (i >= argc)
  {
    repl();
  }
  else
  {
    load_file(argv[i]);
  }
#endif
  term();
  return is_error();
}
#if !defined( __AQUARIO_H__ )
#define __AQUARIO_H__

#include <stdio.h>
#include <stddef.h>

typedef void (*aq_func)();

enum _bool{
  FALSE  = 0,
  TRUE   = 1,
};
typedef enum _bool aq_bool;

enum _type
{
  T_CHAR,	//0.
  T_STRING,	//1.
  T_PAIR,	//2.
  T_PROC,	//3.
  T_SYNTAX,	//4.
  T_SYMBOL,	//5.
  T_LAMBDA,	//6.
};
typedef enum _type aq_type;

enum _opcode
{
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
};
typedef enum _opcode aq_opcode;

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
#define SFRAME_P(v)   ((VALUE)(v) == AQ_SFRAME)
#define INTEGER_P(v)  ((VALUE)(v) & AQ_INTEGER_MASK)
#define CELL_P(v)     ((v) != NULL && (((VALUE)(v) & AQ_IMMEDIATE_MASK) == 0))
#define PAIR_P(p)     (CELL_P(p) && (p)->_type==T_PAIR)

typedef struct cell *Cell;

union _cell_union
{
  char   _char;
  char   _string[1];
  struct{
    Cell  _car;
    Cell  _cdr;
  }       _cons;
  aq_func  _proc;
};
typedef union _cell_union cell_union;

struct cell{
  aq_type _type;
  aq_bool _flag;
  cell_union _object;
};

union _operand
{
  char _char;
  char* _string;
  Cell _num;
};
typedef union _operand aq_operand;

struct _inst
{
  aq_opcode op;
  aq_operand operand1;
  aq_operand operand2;
  int offset;
  int size;
  struct _inst* prev;
  struct _inst* next;
};
typedef struct _inst aq_inst;

struct _inst_queue
{
  aq_inst* head;
  aq_inst* tail;
};
typedef struct _inst_queue inst_queue;

struct _gc_info
{
  void* (*gc_malloc) (size_t);                   //malloc function;
  void  (*gc_start) ();                          //gc function;
  void  (*gc_write_barrier) (Cell, Cell*, Cell); //write barrier;
  void  (*gc_write_barrier_root) (Cell*, Cell);  //write barrier root;
  void  (*gc_init_ptr) (Cell*, Cell);            //init pointer;
  void  (*gc_memcpy) (char*, char*, size_t);     //memcpy;
  void  (*gc_term) ();                           //terminate;
  void  (*gc_push_arg) (Cell c);
  Cell  (*gc_pop_arg) ();
};
typedef struct _gc_info aq_gc_info;

enum _error_type{
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
};
typedef enum _error_type aq_error_type;

aq_bool is_error();
void handle_error();
void set_error(aq_error_type e);

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

#define TYPE(p)         ((p)->_type)
#define CAR(p)          ((p)->_object._cons._car)
#define CDR(p)          ((p)->_object._cons._cdr)
#define CAAR(p)         CAR(CAR(p))
#define CADR(p)         CAR(CDR(p))
#define CDAR(p)         CDR(CAR(p))

#define CHAR_VALUE(p)       ((p)->_object._char)
#define STR_VALUE(p)         ((p)->_object._string)
#define INT_VALUE(p)        (((int)(p))>>1)
#define PROC_VALUE(p)       ((p)->_object._proc)
#define SYNTAX_VALUE(p)     ((p)->_object._proc)
#define SYMBOL_VALUE(p)     STR_VALUE(p)
#define LAMBDA_PARAM(p)     CAR(p)
#define LAMBDA_EXP(p)       CDR(p)
#define LAMBDA_ADDR(p)      (CAR(p))
#define LAMBDA_PARAM_NUM(p) (CDR(p))
#define LAMBDA_FLAG(p)      ((p)->_flag)

Cell new_cell(aq_type t, size_t size);

Cell char_cell(char ch);
Cell string_cell(char* str);
Cell pair_cell(Cell* a, Cell* d);
Cell symbol_cell(char* name);
Cell lambda_cell(int addr, int param_num, aq_bool is_dot_list);
Cell make_integer(int val);
  
aq_bool is_digit_str(char* str);

void print_cell(FILE* fp, Cell c);
void print_line_cell(FILE* fp, Cell c);

// Compile
aq_inst* create_inst(aq_opcode op, int size);
aq_inst* create_inst_char(aq_opcode op, char c);
aq_inst* create_inst_str(aq_opcode op, char* str);
aq_inst* create_inst_num(aq_opcode op, int num);
aq_inst* create_inst_token(inst_queue* queue, char* token);

void add_inst_tail(inst_queue* queue, aq_inst* inst);
size_t write_inst(aq_inst* inst, char* buf);
void add_push_tail(inst_queue* queue, int num);
void add_one_byte_inst_tail(inst_queue* queue, aq_opcode op);

size_t compile(FILE* fp, char* buf, int offset);
void compile_token(inst_queue* queue, char* token, Cell symbol_list);
int compile_list(inst_queue* queue, FILE* fp, Cell symbol_list);
void compile_elem(inst_queue* queue, FILE* fp, Cell symbol_list);
void compile_quote(inst_queue* queue, FILE* fp);
void compile_quoted_atom(inst_queue* queue, char* symbol, FILE* fp);
void compile_quoted_list(inst_queue* queue, FILE* fp);
void compile_add(inst_queue* queue, int num);
void compile_sub(inst_queue* queue, int num);
void compile_if(inst_queue* queue, FILE* fp, Cell symbol_list);
void compile_define(inst_queue* queue, FILE* fp, Cell symbol_list);
void compile_lambda(inst_queue* queue, FILE* fp);
void compile_procedure(char* func, int num, inst_queue* queue);
void compile_symbol_list(char* var, Cell* symbol_list);

void execute(char* buf, int* start, int end);

#define ENVSIZE (3000)
Cell env[ENVSIZE];
#define LINESIZE (1024)

#define STACKSIZE (1024 * 1024)
Cell stack[ STACKSIZE ];
int stack_top;

int hash(char* key);
Cell get_var(char* name);
void set_var(char* name, Cell c);

void repl();

#if defined( _WIN32 ) || defined( _WIN64 )
#define STRCPY(mem, str)	strcpy_s(mem, sizeof(char) * (strlen(str)+1), str)
#else
#define STRCPY(mem, str)	strcpy(mem, str)
#endif
#endif //defined( __AQUARIO_H__ )

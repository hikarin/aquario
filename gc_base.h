#include "types.h"

typedef struct gc_header GC_Header;

#if !defined( _CUT )
  #define HEAP_SIZE (1*1024*1024)
#else
  #define HEAP_SIZE (200*1024*1024)
#endif //_CUT

void trace_roots(void (*trace) (Cell* cellp));
void trace_object( Cell cell, void (*trace) (Cell* cellp) );

#if defined( _DEBUG )
void print_env();
#endif //defined( _DEBUG )

extern Boolean g_GC_stress;
extern void gc_init(const char* gc_char, GC_Init_Info* gc_init);

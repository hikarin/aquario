#include "types.h"

//#define HEAP_SIZE (260*1024)
//#define HEAP_SIZE (1*1024*1024)
#define HEAP_SIZE (10*1024*1024)

void trace_roots(void (*trace) (Cell* cellp));
void trace_object( Cell cell, void (*trace) (Cell* cellp) );

#if defined( _DEBUG )
void print_env();
#endif //defined( _DEBUG )

extern Boolean g_GC_stress;
extern void gc_init(const char* gc_char, GC_Init_Info* gc_init);

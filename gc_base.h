#include "types.h"

//#define HEAP_SIZE (260*1024)
//#define HEAP_SIZE (1*1024*1024)
#define HEAP_SIZE (10*1024*1024)

void trace_roots(void (*trace) (Cell* cellp));
void trace_object( Cell cell, void (*trace) (Cell* cellp) );
Boolean trace_object_bool( Cell cell, Boolean (*trace) (Cell* cellp) );

Cell* popArg_default();
void pushArg_default(Cell* cellp);

void* aq_malloc(size_t size);
void  aq_free(void* p);

#if defined( _DEBUG )
size_t get_total_malloc_size();
#endif //defined( _DEBUG )

extern Boolean g_GC_stress;
extern void gc_init(const char* gc_char, GC_Init_Info* gc_init);

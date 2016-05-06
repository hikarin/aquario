#include "../types.h"
#include <stdlib.h>

#if defined( _TEST )
#define HEAP_SIZE (1*1024*1024)
#else
#define HEAP_SIZE (500*1024*1024)
#endif
#define AQ_MALLOC  malloc
#define AQ_FREE    free

struct free_chunk;
typedef struct free_chunk{
  int chunk_size;
  struct free_chunk* next;
}Free_Chunk;

void trace_roots(void (*trace) (Cell* cellp));
void trace_object( Cell cell, void (*trace) (Cell* cellp) );
Boolean trace_object_bool( Cell cell, Boolean (*trace) (Cell* cellp) );

Cell* popArg_default();
void pushArg_default(Cell* cellp);

Free_Chunk* aq_get_free_chunk( Free_Chunk** freelistp, size_t size );
void put_chunk_to_freelist( Free_Chunk** freelistp, Free_Chunk* chunk, size_t size );
void heap_exhausted_error();

#if defined( _DEBUG )
size_t get_total_malloc_size();
#endif //defined( _DEBUG )

extern Boolean g_GC_stress;
extern void gc_init(const char* gc_char, GC_Init_Info* gc_init);

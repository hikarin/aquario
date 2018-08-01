#include "../types.h"
#include <stdlib.h>

#define HEAP_SIZE (800*1024*1024)
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

void gc_term_base();

Free_Chunk* aq_get_free_chunk( Free_Chunk** freelistp, size_t size );
void put_chunk_to_freelist( Free_Chunk** freelistp, Free_Chunk* chunk, size_t size );
void heap_exhausted_error();

#if defined( _DEBUG )
size_t get_total_malloc_size();
#endif //defined( _DEBUG )

#define MEASURE_START()               \
  {                                   \
    struct rusage usage;              \
    struct timeval ut1;               \
    struct timeval ut2;               \
                                      \
    getrusage(RUSAGE_SELF, &usage );  \
    ut1 = usage.ru_utime;

#define MEASURE_END(lval)             \
    getrusage(RUSAGE_SELF, &usage );  \
    ut2 = usage.ru_utime;             \
    lval += (ut2.tv_sec - ut1.tv_sec)+(double)(ut2.tv_usec-ut1.tv_usec)*1e-6; \
  }

int get_heap_size();

char* aq_heap;

extern Boolean g_GC_stress;
extern void gc_init(char* gc_char, int heap_size, GC_Init_Info* gc_init);

extern void* gc_malloc(size_t size);
extern void gc_start ();
extern void gc_write_barrier (Cell cell, Cell* cellp, Cell newcell);
extern void gc_write_barrier_root (Cell* srcp, Cell dst);
extern void gc_init_ptr (Cell* cellp, Cell newcell);
extern void gc_memcpy (char* dst, char* src, size_t size);
extern void gc_term ();
extern void pushArg (Cell* cellp);
extern Cell* popArg ();

extern GC_Measure_Info* get_measure_info();
extern void increase_live_object(int size_delta, int count_delta);

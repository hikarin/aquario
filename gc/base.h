#include "../aquario.h"
#include <stdlib.h>

#define HEAP_SIZE (16 * 1024)
#define AQ_MALLOC  malloc
#define AQ_FREE    free

struct _free_chunk {
  int chunk_size;
  struct _free_chunk* next;
};
typedef struct _free_chunk free_chunk;

void trace_roots(void (*trace) (Cell* cellp));
void trace_object( Cell cell, void (*trace) (Cell* cellp) );
aq_bool trace_object_bool( Cell cell, aq_bool (*trace) (Cell* cellp) );

Cell pop_arg_default();
void push_arg_default(Cell c);

void gc_term_base();

free_chunk* aq_get_free_chunk( free_chunk** freelistp, size_t size );
void put_chunk_to_freelist( free_chunk** freelistp, free_chunk* chunk, size_t size );
void heap_exhausted_error();

#if defined( _DEBUG )
size_t get_total_malloc_size();
#endif //defined( _DEBUG )

int get_heap_size();

char* aq_heap;

extern aq_bool g_GC_stress;
extern void gc_init(char* gc_char, int heap_size, aq_gc_info* gc_init);

extern void* gc_malloc(size_t size);
extern void gc_start ();
extern void gc_write_barrier (Cell cell, Cell* cellp, Cell newcell);
extern void gc_write_barrier_root (Cell* srcp, Cell dst);
extern void gc_init_ptr (Cell* cellp, Cell newcell);
extern void gc_memcpy (char* dst, char* src, size_t size);
extern void gc_term ();
extern void push_arg (Cell c);
extern Cell pop_arg ();

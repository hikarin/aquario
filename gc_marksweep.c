#include "gc_base.h"
#include "gc_marksweep.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct marksweep_gc_header{
  int obj_size;
  int seg_index;
}MarkSweep_GC_Header;

//mark table: a bit per WORD
#define BIT_WIDTH          (32)
#define MEMORY_ALIGNMENT   (4)

//#define MULTI_THREAD
#if defined( MULTI_THREAD )
//for multi-threading.
#include <pthread.h>
#define THREAD_NUM        (4)
#define SEGMENT_SIZE      ((HEAP_SIZE/THREAD_NUM+(MEMORY_ALIGNMENT-1)) / MEMORY_ALIGNMENT * MEMORY_ALIGNMENT)

static Free_Chunk*    freelists[THREAD_NUM];
static char*          heaps[THREAD_NUM];
static int            mark_tbl[THREAD_NUM][SEGMENT_SIZE/BIT_WIDTH+1];

static void*          sweep_thread(void* pArg);

#define seg_index(obj)           (((MarkSweep_GC_Header*)(obj)-1)->seg_index)
#define is_marked(obj)           (mark_tbl[seg_index(obj)][(((char*)(obj)-heaps[seg_index(obj)])/BIT_WIDTH )] & (1 << (((char*)(obj)-heaps[seg_index(obj)])%BIT_WIDTH)))
#define set_mark(obj)            (mark_tbl[seg_index(obj)][(((char*)(obj)-heaps[seg_index(obj)])/BIT_WIDTH )] |= (1 << (((char*)(obj)-heaps[seg_index(obj)])%BIT_WIDTH)))
#define is_marked_seg(seg, obj)  (mark_tbl[seg][(((char*)(obj)-heaps[seg])/BIT_WIDTH )] & (1 << (((char*)(obj)-heaps[seg])%BIT_WIDTH)))
Free_Chunk* get_free_chunk(int* index, size_t size);

#else

static Free_Chunk*    freelist = NULL;
static char*          heap = NULL;
static int            mark_tbl[HEAP_SIZE/BIT_WIDTH+1];

#define is_marked(obj)   (mark_tbl[(((char*)(obj)-heap)/BIT_WIDTH )] & (1 << (((char*)(obj)-heap)%BIT_WIDTH)))
#define set_mark(obj)    (mark_tbl[(((char*)(obj)-heap)/BIT_WIDTH )] |= (1 << (((char*)(obj)-heap)%BIT_WIDTH)))
Free_Chunk* get_free_chunk(size_t size);
#endif

static void gc_start_marksweep();
static inline void* gc_malloc_marksweep(size_t size);
static void gc_term_marksweep();

static int get_obj_size( size_t size );

#define GET_OBJECT_SIZE(obj) (((MarkSweep_GC_Header*)(obj)-1)->obj_size)

#define MARK_STACK_SIZE 1000
static int mark_stack_top;
static Cell mark_stack[MARK_STACK_SIZE];

static void mark_object(Cell* objp);
static void mark();
static void sweep();

void mark_object(Cell* objp)
{
  Cell obj = *objp;
  if( obj && !is_marked(obj) ){
    set_mark(obj);
    if(mark_stack_top >= MARK_STACK_SIZE){
      printf("[GC] mark stack overflow.\n");
      exit(-1);
    }
    mark_stack[mark_stack_top++] = obj;
  }
}

//Initialization.
void gc_init_marksweep(GC_Init_Info* gc_info)
{
#if defined( MULTI_THREAD )
  int i;
  for(i=0; i<THREAD_NUM; i++){
    heaps[i]                 = (char*)aq_malloc( SEGMENT_SIZE );
    freelists[i]             = (Free_Chunk*)heaps[i];
    freelists[i]->chunk_size = SEGMENT_SIZE;
    freelists[i]->next       = NULL;
  }
#else
  heap                 = (char*)aq_malloc( HEAP_SIZE );
  freelist             = (Free_Chunk*)heap;
  freelist->chunk_size = HEAP_SIZE;
  freelist->next       = NULL;
#endif

  gc_info->gc_malloc        = gc_malloc_marksweep;
  gc_info->gc_start         = gc_start_marksweep;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_marksweep;

  printf("GC: Mark Sweep\n");
}

//Allocation.
void* gc_malloc_marksweep( size_t size )
{
  int allocate_size = (get_obj_size(size) + MEMORY_ALIGNMENT-1) / MEMORY_ALIGNMENT * MEMORY_ALIGNMENT;
  if( g_GC_stress ){
    gc_start_marksweep();
  }
  Free_Chunk* chunk = NULL;
#if defined(MULTI_THREAD)
  int index = 0;
  chunk = get_free_chunk(&index, allocate_size);
#else
  chunk = get_free_chunk(allocate_size);
#endif
  if( !chunk ){
    gc_start_marksweep();
#if defined(MULTI_THREAD)
    chunk = get_free_chunk(&index, allocate_size);
#else
    chunk = get_free_chunk(allocate_size);
#endif
    if( !chunk ){
      printf("Heap Exhausted.\n");
      exit(-1);
    }
  }
  if(chunk->chunk_size > allocate_size){
    allocate_size = chunk->chunk_size;
  }

  MarkSweep_GC_Header* new_header = (MarkSweep_GC_Header*)chunk;
  Cell ret                 = (Cell)(new_header+1);
  new_header->obj_size     = allocate_size;
#if defined(MULTI_THREAD)
  new_header->seg_index    = index;
#endif

  return ret;
}

void mark()
{
  mark_stack_top = 0;
  memset(mark_tbl, 0, sizeof(mark_tbl));

  //mark root objects.
  trace_roots(mark_object);

  Cell obj = NULL;
  while(mark_stack_top > 0){
    obj = mark_stack[--mark_stack_top];
    trace_object(obj, mark_object);
  }
}

#if defined(MULTI_THREAD)
Free_Chunk* get_free_chunk(int* index, size_t size)
#else
Free_Chunk* get_free_chunk(size_t size)
#endif
{
#if defined( MULTI_THREAD )
  int i;
  Free_Chunk* chunk = NULL;
  for(i=0; i<THREAD_NUM; i++){
    chunk = aq_get_free_chunk(&freelists[i], size);
    if( chunk ){
      *index = i;
      break;
    }
  }
#else
  Free_Chunk* chunk = aq_get_free_chunk(&freelist, size);
#endif  
  return chunk;
}

int get_obj_size( size_t size ){
  return sizeof( MarkSweep_GC_Header ) + size;
}

#if defined( MULTI_THREAD )
void* sweep_thread(void* pArg)
{
  int index        = *(int*)pArg;
  char* scan_start = heaps[index];
  char* scan_end   = scan_start + SEGMENT_SIZE;
  char* scan       = scan_start;

  size_t chunk_size      = 0;
  Free_Chunk* chunk_top  = NULL;
  Free_Chunk* chunk_last = NULL;
  freelists[index] = NULL;

  while(scan < scan_end){
    Cell obj = (Cell)((MarkSweep_GC_Header*)scan+1);
    if(is_marked_seg(index, obj)){
      if( chunk_size > 0){
	//reclaim.
	if( !freelists[index] ){
	  freelists[index] = chunk_top;
	  freelists[index]->next = NULL;
	  freelists[index]->chunk_size = chunk_size;

	  chunk_last = freelists[index];
	}else{
	  if( (char*)chunk_last + chunk_last->chunk_size == (char*)chunk_top ){
	    //coalesce.
	    chunk_last->chunk_size += chunk_size;
	    chunk_last->next = NULL;
	  }else{
	    chunk_top->chunk_size = chunk_size;
	    chunk_top->next = NULL;
	    chunk_last->next = chunk_top;
	    chunk_last = chunk_top;
	  }
	}
      }
      size_t obj_size = GET_OBJECT_SIZE(obj);
      scan += obj_size;
      chunk_size = 0;
      chunk_top = NULL;
    }else{
      //skip chunk.
      if(!chunk_top){
	chunk_top = (Free_Chunk*)scan;
      }
      Free_Chunk* tmp = (Free_Chunk*)scan;
      scan += tmp->chunk_size;
      chunk_size += tmp->chunk_size;
    }    
  }

  return NULL;
}

void sweep()
{
  memset(freelists, 0, sizeof(freelists));

  pthread_t tHandles[THREAD_NUM];
  int tNum[THREAD_NUM];
  
  int i=0;
  for(i=0; i<THREAD_NUM; i++){
    tNum[i] = i;
    pthread_create(&tHandles[i], NULL, sweep_thread, (void*)&tNum[i]);
  }

  int j=0;
  for(j=0; j<THREAD_NUM; ++j){
    pthread_join(tHandles[j], NULL);
  }
}

#else

void sweep()
{
  char* scan = heap;
  size_t chunk_size = 0;
  Free_Chunk* chunk_top = NULL;
  Free_Chunk* chunk_last = NULL;
  freelist = NULL;

  while(scan < heap + HEAP_SIZE){
    Cell obj = (Cell)((MarkSweep_GC_Header*)scan+1);
    size_t obj_size = GET_OBJECT_SIZE(obj);
    if( obj_size <= 0 ){
      printf("size 0\n");
    }
    if(is_marked(obj)){
      if( chunk_size > 0){
	//reclaim.
	if( !freelist ){
	  freelist = chunk_top;
	  freelist->next = NULL;
	  freelist->chunk_size = chunk_size;

	  chunk_last = freelist;
	}else{
	  if( (char*)chunk_last + chunk_last->chunk_size == (char*)chunk_top ){
	    //coalesce.
	    chunk_last->chunk_size += chunk_size;
	    chunk_last->next = NULL;
	  }else{
	    chunk_top->chunk_size = chunk_size;
	    chunk_top->next = NULL;
	    chunk_last->next = chunk_top;
	    chunk_last = chunk_top;
	  }
	}
      }
      scan += obj_size;
      chunk_size = 0;
      chunk_top = NULL;
    }else{
      //skip chunk.
      if(!chunk_top){
	chunk_top = (Free_Chunk*)scan;
      }
      Free_Chunk* tmp = (Free_Chunk*)scan;
      scan += tmp->chunk_size;
      chunk_size += tmp->chunk_size;
    }    
  }
}

#endif

//Start Garbage Collection.
void gc_start_marksweep()
{
  mark();
  sweep();
}

//term.
void gc_term_marksweep()
{
#if defined( MULTI_THREAD )
  int i;
  for(i=0; i<THREAD_NUM; i++){
    aq_free( heaps[i] );
  }
#else
  aq_free( heap );
#endif

#if defined( _DEBUG )
  printf("used memory: %ld\n", get_total_malloc_size());
#endif
}

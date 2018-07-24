#include "base.h"
#include "marksweep.h"
#include "../aquario.h"
#include <stdio.h>
#include <stdint.h>

#if defined( _WIN32 ) || defined( _WIN64 )
#else
#define MULTI_THREADING
#endif

typedef struct marksweep_gc_header{
  int obj_size;
  int seg_index;
}MarkSweep_GC_Header;

//mark table: a bit per WORD
#define BIT_WIDTH          (32)
#define MEMORY_ALIGNMENT   (4)

#if defined( MULTI_THREADING )
#include <pthread.h>

static pthread_mutex_t  g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_cond;

#define THREAD_NUM			(3)
#define SEGMENT_NUM			(4)
typedef enum {
    SWEEP_WAIT = 0,
    SWEEP_DOING,
    SWEEP_DONE,
    SWEEP_FINISHED,
}SWEEP_STATE;
static SWEEP_STATE g_state[THREAD_NUM];

Free_Chunk* get_free_chunk(int* index, size_t size);

#define seg_index(obj)           (((MarkSweep_GC_Header*)(obj)-1)->seg_index)
#define is_marked(obj)           (mark_tbl[seg_index(obj)][(((char*)(obj)-heaps[seg_index(obj)])/BIT_WIDTH )] & (1 << (((char*)(obj)-heaps[seg_index(obj)])%BIT_WIDTH)))
#define set_mark(obj)            (mark_tbl[seg_index(obj)][(((char*)(obj)-heaps[seg_index(obj)])/BIT_WIDTH )] |= (1 << (((char*)(obj)-heaps[seg_index(obj)])%BIT_WIDTH)))
#define is_marked_seg(seg, obj)  (mark_tbl[seg][(((char*)(obj)-heaps[seg])/BIT_WIDTH )] & (1 << (((char*)(obj)-heaps[seg])%BIT_WIDTH)))

static pthread_t tHandles[THREAD_NUM];
static int       tNum[THREAD_NUM];
#else
#define THREAD_NUM			(0)
#define SEGMENT_NUM			(1)
#endif

#define SEGMENT_SIZE      ((HEAP_SIZE/SEGMENT_NUM+(MEMORY_ALIGNMENT-1)) / MEMORY_ALIGNMENT * MEMORY_ALIGNMENT)

static Free_Chunk*		freelists[SEGMENT_NUM];
static char*			heaps[SEGMENT_NUM];
static int				mark_tbl[SEGMENT_NUM][SEGMENT_SIZE / BIT_WIDTH + 1];
static void*          sweep_thread(void* pArg);
static void				sweep_segment(int index);

#define seg_index(obj)           (((MarkSweep_GC_Header*)(obj)-1)->seg_index)
#define is_marked(obj)           (mark_tbl[seg_index(obj)][(((char*)(obj)-heaps[seg_index(obj)])/BIT_WIDTH )] & (1 << (((char*)(obj)-heaps[seg_index(obj)])%BIT_WIDTH)))
#define set_mark(obj)            (mark_tbl[seg_index(obj)][(((char*)(obj)-heaps[seg_index(obj)])/BIT_WIDTH )] |= (1 << (((char*)(obj)-heaps[seg_index(obj)])%BIT_WIDTH)))
#define is_marked_seg(seg, obj)  (mark_tbl[seg][(((char*)(obj)-heaps[seg])/BIT_WIDTH )] & (1 << (((char*)(obj)-heaps[seg])%BIT_WIDTH)))

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

typedef struct ms_measure
{
  double mark_elapsed_time;
  double sweep_elapsed_time;
}MS_Measure;

static MS_Measure measure;
static void printMeasureMS();

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

    increase_live_object(GET_OBJECT_SIZE(obj), 1);
  }
}

//Initialization.
void gc_init_marksweep(GC_Init_Info* gc_info)
{
  int i;
  for(i=0; i<SEGMENT_NUM; i++){
    heaps[i]                 = (char*)AQ_MALLOC( SEGMENT_SIZE );
    freelists[i]             = (Free_Chunk*)heaps[i];
    freelists[i]->chunk_size = SEGMENT_SIZE;
    freelists[i]->next       = NULL;
  }

  gc_info->gc_malloc        = gc_malloc_marksweep;
  gc_info->gc_start         = gc_start_marksweep;
  gc_info->gc_write_barrier = NULL;
  gc_info->gc_init_ptr      = NULL;
  gc_info->gc_memcpy        = NULL;
  gc_info->gc_term          = gc_term_marksweep;

  gc_info->printMeasure     = printMeasureMS;
  measure.mark_elapsed_time  = 0.0;
  measure.sweep_elapsed_time = 0.0;
  
#if defined( MULTI_THREADING )
  for(i=0; i<THREAD_NUM; i++){
    tNum[i]                 = i;
    g_state[i]              = SWEEP_WAIT;
  }
  pthread_cond_init( &g_cond, NULL );

  for(i=0; i<THREAD_NUM; i++){
      pthread_create(&tHandles[i], NULL, sweep_thread, (void*)&tNum[i]);
  }
#endif
}

//Allocation.
void* gc_malloc_marksweep( size_t size )
{
  int allocate_size = (get_obj_size(size) + MEMORY_ALIGNMENT-1) / MEMORY_ALIGNMENT * MEMORY_ALIGNMENT;
  if( g_GC_stress ){
    gc_start();
  }
  Free_Chunk* chunk = NULL;
  int index = 0;

  chunk = get_free_chunk(&index, allocate_size);
  if( !chunk ){
    gc_start();
    chunk = get_free_chunk(&index, allocate_size);
    if( !chunk ){
      heap_exhausted_error();
    }
  }
  if(chunk->chunk_size > allocate_size){
    allocate_size = chunk->chunk_size;
  }

  MarkSweep_GC_Header* new_header = (MarkSweep_GC_Header*)chunk;
  Cell ret                 = (Cell)(new_header+1);
  new_header->obj_size     = allocate_size;
  new_header->seg_index    = index;
  
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

Free_Chunk* get_free_chunk(int* index, size_t size)
{
  int i;
  Free_Chunk* chunk = NULL;
  for(i=0; i<SEGMENT_NUM; i++){
    chunk = aq_get_free_chunk(&freelists[i], size);
    if( chunk ){
      *index = i;
      break;
    }
  }
  return chunk;
}

int get_obj_size( size_t size ){
  return sizeof( MarkSweep_GC_Header ) + size;
}

void sweep_segment(int index)
{
  char* scan       = heaps[index];
  char* scan_end   = heaps[index] + SEGMENT_SIZE;

  size_t chunk_size      = 0;
  Free_Chunk* chunk_top  = NULL;
  freelists[index]       = NULL;
  Free_Chunk* last_chunk = NULL;

  while(scan < scan_end){
    Cell obj = (Cell)((MarkSweep_GC_Header*)scan+1);
    if(is_marked_seg(index, obj)){
      if( chunk_size > 0){
	//reclaim.
	if( freelists[index] == NULL ){
	  freelists[index]      = chunk_top;
	  chunk_top->next       = NULL;
	  chunk_top->chunk_size = chunk_size;
	  last_chunk            = chunk_top;
	}
	else if( (char*)last_chunk + last_chunk->chunk_size == (char*)chunk_top ){
	  //Coalesce with previous Free_Chunk.
	  last_chunk->chunk_size += chunk_size;
	}else{
	  //Just put obj into last of freelist.
	  chunk_top->next        = NULL;
	  chunk_top->chunk_size  = chunk_size;
	  last_chunk->next       = chunk_top;
	  last_chunk             = chunk_top;
	}
      }

      size_t obj_size = GET_OBJECT_SIZE(obj);
      scan += obj_size;
      chunk_size = 0;
      chunk_top = NULL;
    }else{
      //skip chunk.
      if(chunk_top == NULL){
	chunk_top = (Free_Chunk*)scan;
      }
      Free_Chunk* tmp = (Free_Chunk*)scan;
      scan += tmp->chunk_size;
      chunk_size += tmp->chunk_size;
    }    
  }

  if (freelists[index] == NULL && chunk_top != NULL ) {
      freelists[index] = chunk_top;
      freelists[index]->next = NULL;
      freelists[index]->chunk_size = chunk_size;
  }
}

void* sweep_thread(void* pArg)
{
#if defined( MULTI_THREADING )
  int index = *(int*)pArg;
  while ( TRUE ) {
      pthread_mutex_lock(&g_mutex);
      switch ( g_state[index] ) {
      case SWEEP_WAIT:
	  pthread_cond_wait( &g_cond, &g_mutex );
	  if ( g_state[index] == SWEEP_FINISHED ) {
	      pthread_mutex_unlock(&g_mutex);
	      break;
	  }else{
	      g_state[index] = SWEEP_DOING;
	  }
	  pthread_mutex_unlock(&g_mutex);
	  sweep_segment(index);
	  break;
      case SWEEP_DOING:
	  g_state[index] = SWEEP_DONE;
	  pthread_mutex_unlock(&g_mutex);
	  break;
      case SWEEP_DONE:
	  pthread_mutex_unlock(&g_mutex);
	  break;
      case SWEEP_FINISHED:
	  pthread_mutex_unlock(&g_mutex);
	  return NULL;
      }
  }
#else
	return NULL;
#endif
}

void sweep()
{
#if defined( MULTI_THREADING )
    pthread_mutex_lock( &g_mutex );
    pthread_cond_broadcast( &g_cond );
    pthread_mutex_unlock( &g_mutex );

    sweep_segment(THREAD_NUM);
    
    Boolean bWaiting = TRUE;
    int i;
    while ( bWaiting ) {
	bWaiting = FALSE;
	pthread_mutex_lock( &g_mutex );
	for ( i=0; i<THREAD_NUM; i++ ) {
	    if ( g_state[i] != SWEEP_DONE ) {
		bWaiting = TRUE;
		break;
	    }
	}
	pthread_mutex_unlock( &g_mutex );
    }
    
    pthread_mutex_lock( &g_mutex );
    pthread_cond_init( &g_cond, NULL );    
    for ( i=0; i<THREAD_NUM; i++ ) {
	g_state[i] = SWEEP_WAIT;
    }
    pthread_mutex_unlock( &g_mutex );
    
    memset(mark_tbl, 0, sizeof(mark_tbl));
#else
    sweep_segment(THREAD_NUM);
#endif
}

void printMeasureMS()
{
  AQ_PRINTF("------------------------------\n");
  AQ_PRINTF("mark elapsed time:  %.8f\n", measure.mark_elapsed_time);
  AQ_PRINTF("sweep elapsed time: %.8f\n", measure.sweep_elapsed_time);
}

//Start Garbage Collection.
void gc_start_marksweep()
{
  struct rusage usage;
  struct timeval ut1;
  struct timeval ut2;
  
  getrusage(RUSAGE_SELF, &usage );
  ut1 = usage.ru_utime;
  
  mark();
  
  getrusage(RUSAGE_SELF, &usage );
  ut2 = usage.ru_utime;
  measure.mark_elapsed_time += (ut2.tv_sec - ut1.tv_sec)+(double)(ut2.tv_usec-ut1.tv_usec)*1e-6;

  getrusage(RUSAGE_SELF, &usage );
  ut1 = usage.ru_utime;
  
  sweep();
  
  getrusage(RUSAGE_SELF, &usage );
  ut2 = usage.ru_utime;
  measure.sweep_elapsed_time += (ut2.tv_sec - ut1.tv_sec)+(double)(ut2.tv_usec-ut1.tv_usec)*1e-6;
}

//term.
void gc_term_marksweep()
{
#if defined( MULTI_THREADING )
  pthread_mutex_lock( &g_mutex );
  int i;
  for ( i=0; i<THREAD_NUM; i++) {
      g_state[i] = SWEEP_FINISHED;
  }
  pthread_cond_broadcast( &g_cond );
  pthread_mutex_unlock( &g_mutex );
  
  for (i=0; i<THREAD_NUM; i++ ) {
      pthread_join(tHandles[i], NULL);
  }

  for( i=0; i<THREAD_NUM; i++){
    AQ_FREE( heaps[i] );
  }
#else
  int i;
  for (i = 0; i<THREAD_NUM; i++) {
    AQ_FREE(heaps[i]);
  }
  AQ_FREE(heaps[THREAD_NUM]);
#endif
}

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE

#ifndef __minispark_h__
#define __minispark_h__

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <sched.h>

#define MAXDEPS (2)
#define TIME_DIFF_MICROS(start, end) \
  (((end.tv_sec - start.tv_sec) * 1000000L) + ((end.tv_nsec - start.tv_nsec) / 1000L))

struct RDD;
struct List;

typedef struct RDD RDD; // forward decl. of struct RDD
typedef struct List List; // forward decl. of List.
typedef struct ListNode ListNode;
// Minimally, we assume "list_add_elem(List *l, void*)"

// Different function pointer types used by minispark
typedef void* (*Mapper)(void* arg);
typedef int (*Filter)(void* arg, void* pred);
typedef void* (*Joiner)(void* arg1, void* arg2, void* arg);
typedef unsigned long (*Partitioner)(void *arg, int numpartitions, void* ctx);
typedef void (*Printer)(void* arg);

typedef enum {
  MAP,
  FILTER,
  JOIN,
  PARTITIONBY,
  FILE_BACKED
} Transform;

struct RDD {    
  Transform trans; // transform type, see enum
  void* fn; // transformation function
  void* ctx; // used by minispark lib functions
  List* partitions; // list of partitions
  
  RDD* dependencies[MAXDEPS];
  int numpartitions;
  int numdependencies; // 0, 1, or 2

  // you may want extra data members here
};

//TODO make list thread safe
//minispark will work on partitions concurrently
struct ListNode{
    ListNode* next;
    FILE *file;
    int val;
};
struct List{
    ListNode* head;
    int size;
};
List* list_init();
void list_add_elem(List* l, FILE* fp);
void list_free(List* l);


typedef struct {
  struct timespec created;
  struct timespec scheduled;
  size_t duration; // in usec
  RDD* rdd;
  int pnum;
} TaskMetric;

typedef struct {
  RDD* rdd;
  int pnum;
  TaskMetric* metric;
} Task;

//task queue
typedef struct qnode{
    Task* t;
    struct qnode* next;
} qnode;

typedef struct queue{
    qnode* front;
    qnode* back;
    pthread_mutex_t frontlock, backlock;
} queue;

void queue_init(queue* q);
void queue_push(queue* q, Task* t);
void queue_pop(queue* q, Task** t);

    
//////// threads ////////
void thread_pool_init(int numthreads);
void thread_pool_destroy();
void thread_pool_wait();
void thread_pool_submit(Task* task);

//////// actions ////////

// Return the total number of elements in "dataset"
int count(RDD* dataset);

// Print each element in "dataset" using "p".
// For example, p(element) for all elements.
void print(RDD* dataset, Printer p);

//////// transformations ////////

// Create an RDD with "rdd" as its dependency and "fn"
// as its transformation.
RDD* map(RDD* rdd, Mapper fn);

// Create an RDD with "rdd" as its dependency and "fn"
// as its transformation. "ctx" should be passed to "fn"
// when it is called as a Filter
RDD* filter(RDD* rdd, Filter fn, void* ctx);

// Create an RDD with two dependencies, "rdd1" and "rdd2"
// "ctx" should be passed to "fn" when it is called as a
// Joiner.
RDD* join(RDD* rdd1, RDD* rdd2, Joiner fn, void* ctx);

// Create an RDD with "rdd" as a dependency. The new RDD
// will have "numpartitions" number of partitions, which
// may be different than its dependency. "ctx" should be
// passed to "fn" when it is called as a Partitioner.
RDD* partitionBy(RDD* rdd, Partitioner fn, int numpartitions, void* ctx);

// Create an RDD which opens a list of files, one per
// partition. The number of partitions in the RDD will be
// equivalent to "numfiles."
RDD* RDDFromFiles(char* filenames[], int numfiles);

//////// MiniSpark ////////
// Submits work to the thread pool to materialize "rdd".
void execute(RDD* rdd);

// Creates the thread pool and monitoring thread.
void MS_Run();

// Waits for work to be complete, destroys the thread pool, and frees
// all RDDs allocated during runtime.
void MS_TearDown();


#endif // __minispark_h__

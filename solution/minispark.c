#include "minispark.h"

//task queue

//thread pool implementation
void *threadstart(void *arg){
    return NULL;
}
void thread_pool_init(int numthreads){
    if(numthreads < 1){
        numthreads = 1;
    }
    //init global list of all threads
    g_threads = malloc(sizeof(pthread_t)*numthreads);
    if(!g_threads){
        perror("pthread malloc");
        exit(1);
    }
    for(int i=0; i<numthreads; i++){
        int s = pthread_create(&g_threads[i], NULL, &threadstart, NULL);
        if(s != 0){
            perror("pthread_create");
            exit(1);
        }
    }
}

void thread_pool_submit(Task* task){
}

void thread_pool_wait(){
}

void thread_pool_destroy(){
}


//LL functions
List* list_init(){
    List *temp = (List*)malloc(sizeof(List));
    if(temp==NULL){
        perror("list init malloc fail");
        exit(1);
    }
    temp->head=NULL;
    temp->size=0;
    return temp;
}
void list_add_elem(List* l, FILE *fp){
    ListNode* curr = malloc(sizeof(ListNode));
    if(curr==NULL){
        perror("list add elem malloc fail");
        exit(1);
    }
    curr->file = fp;
    //TODO: update cur val in future
    //curr->val = 0;
    ListNode* oldh = l->head;
    curr->next = oldh;
    l->head = curr;
    l->size++;
} 
void list_free(List* l){
    ListNode* front = l->head;
    while(front){
        ListNode* temp = front;
        front = front->next;
        //TODO: free stuff in the nodes (FILE*, ...)
        free(temp);
    }
    free(l);
}

/*
List *list_init(){
    List *temp = (List *)malloc(sizeof(List));
    if (temp == NULL){
        perror("malloc");
        exit(1);
    }
    temp->file = NULL;
    temp->next = NULL;
    return temp;
}

void list_add_elem(List **e, FILE *fp){
    List *newhead = list_init();
    newhead->file = fp;
    newhead->next = *e;
    *e = newhead;
};
*/

// Working with metrics...
// Recording the current time in a `struct timespec`:
//    clock_gettime(CLOCK_MONOTONIC, &metric->created);
// Getting the elapsed time in microseconds between two timespecs:
//    duration = TIME_DIFF_MICROS(metric->created, metric->scheduled);
// Use `print_formatted_metric(...)` to write a metric to the logfile. 
void print_formatted_metric(TaskMetric* metric, FILE* fp) {
  fprintf(fp, "RDD %p Part %d Trans %d -- creation %10jd.%06ld, scheduled %10jd.%06ld, execution (usec) %ld\n",
	  metric->rdd, metric->pnum, metric->rdd->trans,
	  metric->created.tv_sec, metric->created.tv_nsec / 1000,
	  metric->scheduled.tv_sec, metric->scheduled.tv_nsec / 1000,
	  metric->duration);
}

int max(int a, int b)
{
  return a > b ? a : b;
}

RDD *create_rdd(int numdeps, Transform t, void *fn, ...)
{
  RDD *rdd = malloc(sizeof(RDD));
  if (rdd == NULL)
  {
    printf("error mallocing new rdd\n");
    exit(1);
  }

  va_list args;
  va_start(args, fn);

  int maxpartitions = 0;
  for (int i = 0; i < numdeps; i++)
  {
    RDD *dep = va_arg(args, RDD *);
    rdd->dependencies[i] = dep;
    maxpartitions = max(maxpartitions, dep->partitions->size);
  }
  va_end(args);

  rdd->numdependencies = numdeps;
  rdd->trans = t;
  rdd->fn = fn;
  rdd->partitions = NULL;
  return rdd;
}

/* RDD constructors */
RDD *map(RDD *dep, Mapper fn)
{
  return create_rdd(1, MAP, fn, dep);
}

RDD *filter(RDD *dep, Filter fn, void *ctx)
{
  RDD *rdd = create_rdd(1, FILTER, fn, dep);
  rdd->ctx = ctx;
  return rdd;
}

RDD *partitionBy(RDD *dep, Partitioner fn, int numpartitions, void *ctx)
{
  RDD *rdd = create_rdd(1, PARTITIONBY, fn, dep);
  rdd->partitions = list_init();
  rdd->numpartitions = numpartitions;
  rdd->ctx = ctx;
  return rdd;
}

RDD *join(RDD *dep1, RDD *dep2, Joiner fn, void *ctx)
{
  RDD *rdd = create_rdd(2, JOIN, fn, dep1, dep2);
  rdd->ctx = ctx;
  return rdd;
}

/* A special mapper */
void *identity(void *arg)
{
  return arg;
}

/* Special RDD constructor.
 * By convention, this is how we read from input files. */
RDD *RDDFromFiles(char **filenames, int numfiles)
{
  RDD *rdd = malloc(sizeof(RDD));
  rdd->partitions = list_init();

  for (int i = 0; i < numfiles; i++)
  {
    FILE *fp = fopen(filenames[i], "r");
    if (fp == NULL) {
      perror("fopen");
      exit(1);
    }
    list_add_elem(rdd->partitions, fp);
  }

  rdd->numdependencies = 0;
  rdd->trans = MAP;
  rdd->fn = (void *)identity;
  return rdd;
}

void execute(RDD* rdd) {
  return;
}

void MS_Run() {
	cpu_set_t set;
	CPU_ZERO(&set);
	if(sched_getaffinity(0, sizeof(set), &set) == -1){
		perror("sched_getaffinity");
		exit(1);
	}
	thread_pool_init(CPU_COUNT(&set));//create pool w/ same # of threads as cores
	return;
}

void MS_TearDown() {
	thread_pool_wait();
	thread_pool_destroy();

	//free all RDD's and lists
	return;
}

int count(RDD *rdd) {
  execute(rdd);

  int count = 0;
  // count all the items in rdd
  return count;
}

void print(RDD *rdd, Printer p) {
  execute(rdd);

  // print all the items in rdd
  // aka... `p(item)` for all items in rdd
}

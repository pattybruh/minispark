#include "minispark.h"

static pthread_t* g_threads = NULL;
static int g_threadCount = 0;
static queue* g_taskqueue = NULL;

static pthread_cond_t qempty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t qfill = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;

//task queue
void queue_init(queue* q){
    qnode* dummy= malloc(sizeof(qnode));
    if(!dummy){
        perror("queue init malloc");
        exit(1);
    }
    dummy->t = NULL;
    dummy->next = NULL;
    q->front= dummy;
    q->back = dummy;
    pthread_mutex_init(&q->frontlock, NULL);
    pthread_mutex_init(&q->backlock, NULL);
}

void queue_push(queue *q, Task *t){
    qnode* temp = malloc(sizeof(qnode));
    Task* cpyTask = malloc(sizeof(Task));
    if(!temp || !cpyTask){
        perror("queue_push malloc");
        exit(1);
    }
    *cpyTask = *t;
    temp->t = cpyTask;
    temp->next = NULL;
    
    // problem if queue size 1 has queue_pop and queue_push?
    //solved w/ dummy head
    pthread_mutex_lock(&q->backlock);
    q->back->next = temp;
    q->back = temp;
    pthread_mutex_unlock(&q->backlock);
}

//caller must free returned val!
void queue_pop(queue *q, Task** val){
    // same as q.push problem
    //solved w/ dummy head
    pthread_mutex_lock(&q->frontlock);
    qnode* dummy = q->front;
    qnode* newh = dummy->next;
    if(!newh){//empty q
        pthread_mutex_unlock(&q->frontlock);
        *val = NULL;
        return;
    }
    *val = newh->t;
    q->front = newh;
    pthread_mutex_unlock(&q->frontlock);
    free(dummy);
}

//thread pool implementation
void* threadstart(void *arg){
    
    return NULL;
}
void thread_pool_init(int numthreads){
    if(numthreads < 1){
        numthreads = 1;
    }
    g_threadCount = numthreads;
    //init global list of all threads
    g_threads = malloc(sizeof(pthread_t)*numthreads);
    //init task queue
    g_taskqueue = malloc(sizeof(queue));
    if(!g_threads || !g_taskqueue){
        perror("pthread malloc");
        exit(1);
    }
    queue_init(g_taskqueue);
    for(int i=0; i<numthreads; i++){
        int s = pthread_create(&g_threads[i], NULL, threadstart, NULL);
        if(s != 0){
            perror("pthread_create");
            exit(1);
        }
    }
}

void thread_pool_submit(Task* task){
    queue_push(g_taskqueue, task);
}

void thread_pool_wait(){
}

void thread_pool_destroy(){
    for(int i=0; i<g_threadCount; i++){
        pthread_join(g_threads[i], NULL);
    }
    g_threadCount = 0;
    free(g_threads);
    g_threads = NULL;

    qnode* h = g_taskqueue->front;
    while(h){
        qnode* curr = h;
        h=h->next;
        free(curr->t);
        free(curr);
    }
    free(g_taskqueue);
    g_taskqueue = NULL;
}


//LL functions
List* list_init(int t){
    List *temp = (List*)malloc(sizeof(List));
    if(temp==NULL){
        perror("list init malloc fail");
        exit(1);
    }
    temp->head=NULL;
    temp->size=0;
    temp->isList = t;
    if(!t){//if its 2d list, dont need a lock
        pthread_mutex_init(&temp->guard, NULL);
    }
    return temp;
}
void list_add_elem(List* l, void* e){
    ListNode* curr = malloc(sizeof(ListNode));
    if(curr==NULL){
        perror("list add elem malloc fail");
        exit(1);
    }
    curr->data = e;

    //only need locks on 1d lists
    //parition list is always created by main thread (no concurrency)
    //allows multiple threads to work on same List but differetn partitions
    if(!l->isList){
        pthread_mutex_lock(&l->guard);
    }
    curr->next = l->head;
    l->head = curr;
    l->size++;
    if(!l->isList){
        pthread_mutex_unlock(&l->guard);
    }
} 
void list_free(List* l){
    int b = l->isList;
    ListNode* front = l->head;
    if(b){
        while(front){
            ListNode* temp = front;
            front = front->next;
            list_free((List*)temp->data);
            
            //free(temp); //double free
        }
    }
    else{
        while(front){
            ListNode* temp = front;
            front = front->next;
            //TODO: might need separate func listfree2d listfree1d
            //b/c need to free either char* or fclose FILE*
            free(temp);
        }
        free(l);
    }
}
void* list_get(List* l, int idx){
    if(idx<0 || idx>= l->size) return NULL;
    ListNode* curr = l->head;
    for(int i=0; i<idx; i++){
        curr=curr->next;
    }
    return curr->data;
}

void listit_seek_to_start(List* l, ListIt* it){
    it->curr = l->head;
}

ListNode* listit_next(List* l, ListIt* it){
    ListNode* res = it->curr;
    if(!res){
        return res;
    }
    //advance iterator if not last
    it->curr = it->curr->next;
    return res;
}

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
    //map(RDD* files, GetLines) => create_rdd(1, MA, GetLines, RDD* files)
    //numdeps=1, Transform=MAP, fn=GetLines, dep=files
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
  //TODO: might not be correct use
  rdd->numpartitions = maxpartitions;

  rdd->numdependencies = numdeps;
  rdd->trans = t;
  rdd->fn = fn;
  //rdd->partitions = list_init(1);
  return rdd;
}

/* RDD constructors */
RDD *map(RDD *dep, Mapper fn)
{
  RDD* rdd = create_rdd(1, MAP, fn, dep);
  rdd->partitions = list_init(1);
  for(int i=0; i<rdd->numpartitions; i++){
      List* temp = list_init(0);
      list_add_elem(rdd->partitions, temp);
  }

  rdd->pdep = malloc(sizeof(int)*rdd->numpartitions);
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = 1;
  }
  return rdd;
}

RDD *filter(RDD *dep, Filter fn, void *ctx)
{
  RDD *rdd = create_rdd(1, FILTER, fn, dep);

  rdd->partitions = list_init(1);
  for(int i=0; i<rdd->numpartitions; i++){
      List* temp = list_init(0);
      list_add_elem(rdd->partitions, temp);
  }

  rdd->pdep = malloc(sizeof(int)*rdd->numpartitions);
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = 1;
  }
  rdd->partitions = list_init(1);
  rdd->ctx = ctx;
  return rdd;
}

RDD *partitionBy(RDD *dep, Partitioner fn, int numpartitions, void *ctx)
{
  RDD *rdd = create_rdd(1, PARTITIONBY, fn, dep);
  rdd->numpartitions = numpartitions;

  rdd->partitions = list_init(1);
  for(int i=0; i<rdd->numpartitions; i++){
      List* temp = list_init(0);
      list_add_elem(rdd->partitions, temp);
  }

  rdd->pdep = malloc(sizeof(int)*rdd->numpartitions);
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = rdd->numdependencies;
  }

  rdd->ctx = ctx;
  return rdd;
}

RDD *join(RDD *dep1, RDD *dep2, Joiner fn, void *ctx)
{
  RDD *rdd = create_rdd(2, JOIN, fn, dep1, dep2);
    /*
  for(int i=0; i<maxpartitions; i++){
      list_add_elem(rdd->partitions, NULL);
  }
  rdd->pdep = malloc(sizeof(int)**/
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
  rdd->partitions = list_init(0);//file backed, t=0

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
  //rdd->trans = MAP;
  rdd->trans = FILE_BACKED;
  rdd->fn = (void *)identity;

  rdd->numpartitions = rdd->partitions->size;
  rdd->pdep = NULL;
  return rdd;
}

void execute(RDD* rdd) {
    //TODO: add checks for pdep[pnum] before submitting a partition to q
    if(rdd->numdependencies == 0){//base
        for(int i=0; i<rdd->numpartitions; i++){
            Task* t = malloc(sizeof(Task));
            t->rdd = rdd;
            t->pnum = i;
            t->metric = NULL; //TODO
            thread_pool_submit(t);
            free(t);
        }
    }
    for(int i=0; i<rdd->numdependencies; i++){
        execute(rdd->dependencies[i]);
    }
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
	thread_pool_destroy();

	//TODO: free all RDD's and lists
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

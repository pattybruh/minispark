#include "minispark.h"


static pthread_t* g_threads = NULL;
static int g_threadCount = 0;
static queue* g_taskqueue = NULL;

//static pthread_cond_t qempty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t qfill = PTHREAD_COND_INITIALIZER;
static pthread_cond_t qempty = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
int g_activeThreads;

static pthread_t g_metricsThread;  
static queue* g_metricQueue;     
static pthread_cond_t g_metricCond = PTHREAD_COND_INITIALIZER;
static int g_metricsRunning;
FILE* g_metricsFile = NULL;

//debug vars TODO: remove
static pthread_mutex_t dlock = PTHREAD_MUTEX_INITIALIZER ;
int cnt = 0;

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
    //pthread_mutex_init(&q->frontlock, NULL);
    pthread_mutex_init(&q->backlock, NULL);
    q->size = 0;
}

int queue_size(queue* q){
    pthread_mutex_lock(&q->backlock);
    int s = q->size;
    pthread_mutex_unlock(&q->backlock);
    return s;
}
//TODO: prolly best if queue doesnt own obj
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
    if (t->rdd->trans == PARTITIONBY) {
		printf("%p %d\n", t->rdd, t->pnum);
	}
    q->back->next = temp;
    q->back = temp;
    q->size++;
    pthread_cond_signal(&qfill);
    pthread_mutex_unlock(&q->backlock);
}

//caller must free returned val!
//MUST BE CALLED WITH q->frontlock HELD!!
void queue_pop(queue *q, Task** val){
    // same as q.push problem
    //solved w/ dummy head
    //pthread_mutex_lock(&q->frontlock);
    if(q->size < 1){
        *val = NULL;
        return;
    }
    qnode* dummy = q->front;
    qnode* newh = dummy->next;
    *val = newh->t;
    q->front = newh;
    q->size--;
    //pthread_mutex_unlock(&q->frontlock);
}

int max(int a, int b)
{
  return a > b ? a : b;
}
//thread function
void* threadstart(void *arg){
    while(1){
        Task* currT = NULL;
        //pthread_mutex_lock(&g_taskqueue->frontlock);
        pthread_mutex_lock(&g_taskqueue->backlock);
        while(g_taskqueue->size < 1){
            //pthread_cond_wait(&qfill, &g_taskqueue->frontlock);
            pthread_cond_wait(&qfill, &g_taskqueue->backlock);
        }
        queue_pop(g_taskqueue, &currT);
		if (currT->metric) {
		    clock_gettime(CLOCK_MONOTONIC, &currT->metric->scheduled);
		}
        pthread_mutex_unlock(&g_taskqueue->backlock);
        //pthread_mutex_unlock(&g_taskqueue->frontlock);

        pthread_mutex_lock(&g_pool_lock);
        if(currT==NULL){
            pthread_mutex_unlock(&g_pool_lock);
            continue;
        }
        g_activeThreads++;
        pthread_mutex_unlock(&g_pool_lock);

        RDD* currRDD = currT->rdd;
        if(currRDD == NULL){
            perror("NULL rdd submitted");
            exit(1);
        }

        //exit thread
        if(currRDD->trans == KILL){
            pthread_mutex_lock(&g_pool_lock);
            --g_activeThreads;
            pthread_mutex_unlock(&g_pool_lock);
            free(currT);
            break;
        }
        int pnum = currT->pnum;
        //printf("running task on %d rdd on partition %d\n", currRDD->trans, pnum);

        switch(currRDD->trans){
            case FILE_BACKED: {
                //FILE_BACKED: they should be mapped 1:1
                //no work todo except submit child's partitions
                //printf("fuck you\n");
                break;
            }
            case MAP: {
                //MAP: apply func to each element of parent partition
                //if parent is FILE_BACKED just apply to partition until EOF
                //results added to end of LL of current partition
                RDD* parentRDD = currRDD->dependencies[0];
                Mapper func = (Mapper)currRDD->fn;
                ListNode* currP = list_get(currRDD->partitions, pnum);
                if(currP == NULL){
                    perror("error list_get\n");
                    exit(1);
                }
                //currP->data = list_init(0);//alr created by RDD* map()
                ListNode* parentP = list_get(parentRDD->partitions, pnum);
                if(parentP == NULL){
                    //printf("MAP: parentP = NULL on rdd %d, in pnum %d\n", currRDD->trans, pnum);
                    break;
                }

                if(parentRDD->trans == FILE_BACKED){
                    void* res=NULL;
                    while((res = func(parentP->data)) != NULL){
                        //printf("read: %s", (char*)res);
                        list_append(currP->data, res);
                    }
                    //printf("done reading from FILE* in partition %d\n", pnum);
                }
                else{
                    ListIt it;
                    listit_seek_to_start((List*)parentP->data, &it);
                    ListNode* temp;
                    while((temp=listit_next(parentP->data, &it)) != NULL){
                        list_append(currP->data, func(temp->data));
                    }
                }
				break;
			}
			case FILTER: {
				RDD* parentRDD = currRDD -> dependencies[0];
				Filter fn = (Filter)currRDD -> fn;
                ListNode* parentP = list_get(parentRDD->partitions, pnum);
                ListNode* filter = list_get(currRDD->partitions, pnum);
                if(!filter){
                    perror("error list_get");
                    exit(1);
                }

                ListNode* temp;
                ListIt it;
                if(parentRDD->trans == FILE_BACKED){
                    printf("parent is filebacked\n");
                    break;
                }
                listit_seek_to_start(parentP->data, &it);
                while((temp=listit_next(parentP->data, &it))!= NULL){
                    if(fn(temp->data, currRDD->ctx)){
                        list_append(filter->data, temp->data);
                    }
                }
                break;
			}
            case JOIN: {
                //TODO: avoid doubles, b/c both parent nodes could wake curr partition
				Joiner fn = (Joiner)(currRDD->fn);
				List* pl1 = list_get(currRDD->dependencies[0]->partitions, pnum)->data;
				List* pl2 = list_get(currRDD->dependencies[1]->partitions, pnum)->data;
                //printf("pl1 size: %d, pl2 size: %d\n", pl1->size, pl2->size);
				List* currP = list_get(currRDD->partitions, pnum)->data;
                
                ListIt it1;
                ListNode* temp1;
                listit_seek_to_start(pl1, &it1);
                while((temp1=listit_next(pl1, &it1)) != NULL){
                    ListIt it2;
                    ListNode* temp2;
                    listit_seek_to_start(pl2, &it2);
                    while((temp2=listit_next(pl2, &it2))!=NULL){
                        void* res = fn(temp1->data, temp2->data, currRDD->ctx);
                        if(res != NULL){
                            list_append(currP, res);
                        }
                    }
                }
                break;
            }
            case PARTITIONBY: {
                RDD* parentRDD = currRDD->dependencies[0];
                //TODO: only 1 parition is ever woken up
                //need to find way to add all partitions of a PARITTIONBY to queue
                //potential sol:
                //
                //right now we are assuming each thread just works on one partition
                Partitioner func = (Partitioner)(currRDD->fn);
                //currP->data = list_init(0);//alr created by RDD* map()
                ListNode const *parentP = list_get(parentRDD->partitions, pnum);
                ListIt it;
                listit_seek_to_start(parentP->data, &it);
                ListNode const *temp;
                while((temp = listit_next(parentP->data, &it)) != NULL){
                    int hashIdx = func(temp->data, currRDD->numpartitions, currRDD->ctx);
                    list_append(list_get(currRDD->partitions, hashIdx)->data, temp->data);
                }
                break;
            }
            default:
                break;
        }

        //PARITTIONBY AND JOINS NEED ALL PARITITONS WOKEN AT THE SAME TIME
        //add locks
        if(currRDD->child == NULL){
            //printf("rdd %d in partition %d has no child\n", currRDD->trans, pnum);
            pthread_mutex_lock(&g_pool_lock);
            if(--g_activeThreads == 0){
                if(queue_size(g_taskqueue)==0 && g_activeThreads ==0){
                    pthread_cond_signal(&qempty);
                }
                //pthread_cond_signal(&qempty);
            }
            pthread_mutex_unlock(&g_pool_lock);
            free(currT);
            continue;
        }
        pthread_mutex_lock(&currRDD->child->pdeplock);
        if(--(currRDD->child->pdep[pnum]) != 0){
            //printf("currRDD->child->pdep[pnum] = %d\n", currRDD->child->pdep[pnum]);
            pthread_mutex_unlock(&currRDD->child->pdeplock);

            pthread_mutex_lock(&g_pool_lock);
            if(--g_activeThreads == 0){
                if(queue_size(g_taskqueue)==0){
                    pthread_cond_signal(&qempty);
                }
                //pthread_cond_signal(&qempty);
            }
            pthread_mutex_unlock(&g_pool_lock);
            free(currT);
            continue;
        }
        //child->pdep[pnum]==0
        if(currRDD->child->trans==PARTITIONBY || currRDD->child->trans==JOIN){
            //printf("child is join/partition\n");
            //TODO: pdep[pnump]]==0 so we dont need to check others
            //submit a task for each parent(current rdd) partition
            int n = max(currRDD->numpartitions, currRDD->child->numpartitions);
            for(int i=0; i<n; i++){
                currRDD->child->pdep[i] = -1;
                Task* newTask = malloc(sizeof(Task));
                if(!newTask){
                    perror("malloc Task");
                    exit(1);
                }
                newTask->rdd = currRDD->child;
                newTask->pnum = i;
                newTask->metric = NULL;
                thread_pool_submit(newTask);
                /*
                Task newTask;
                newTask.rdd=currRDD->child;
                newTask.pnum = i;
                newTask.metric = NULL;
                thread_pool_submit(&newTask);
                */
            }
        }
        else{
            //printf("bruh\n");
            Task newTask;
            newTask.rdd=currRDD->child;
            newTask.pnum = pnum;
            newTask.metric = NULL;
            thread_pool_submit(&newTask);
        }
		if (currT->metric) {
			struct timespec finish;
			clock_gettime(CLOCK_MONOTONIC, &finish);
			currT->metric->duration = TIME_DIFF_MICROS(currT->metric->scheduled, finish);

			TaskMetric *copy = malloc(sizeof(TaskMetric));
			*copy = *currT->metric;
			metrics_submit(copy);
		}
		pthread_mutex_unlock(&currRDD->child->pdeplock);

        pthread_mutex_lock(&g_pool_lock);
        if(--g_activeThreads == 0){
            if(queue_size(g_taskqueue)==0){
                pthread_cond_signal(&qempty);
            }
            //pthread_cond_signal(&qempty);
        }
        pthread_mutex_unlock(&g_pool_lock);
        free(currT);
    }
    return NULL;
}

void* metricsStart(void* arg) {
    while(1) {
        pthread_mutex_lock(&g_metricQueue->backlock);
        while(g_metricQueue->size < 1 && g_metricsRunning) {
            pthread_cond_wait(&g_metricCond, &g_metricQueue->backlock);
        }
        if(!g_metricsRunning && g_metricQueue->size < 1) {
            pthread_mutex_unlock(&g_metricQueue->backlock);
            break;
        }
        Task* currT = NULL;
        queue_pop(g_metricQueue, &currT);
        pthread_mutex_unlock(&g_metricQueue->backlock);

        if(!currT) continue;

        TaskMetric* m = (TaskMetric*)currT;
        print_formatted_metric(m, g_metricsFile);
    }
    return NULL;
}

//thread pool implementation
void thread_pool_init(int numthreads){
    if(numthreads < 1){
        numthreads = 1;
    }
        numthreads = 1;
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

void metrics_init() {
	g_metricQueue = (queue*)malloc(sizeof(queue));
	if(!g_metricQueue){
		perror("metric malloc");
		exit(1);
	}
	queue_init(g_metricQueue);

	g_metricsFile = fopen("metrics.log", "w");
	if(!g_metricsFile){
        perror("fopen");
        exit(1);
    }
    g_metricsRunning = 1;
    int monitor = pthread_create(&g_metricsThread, NULL, metricsStart, NULL);
    if(monitor!=0){
	    perror("monitoring thread");
        exit(1);
    }
}

void thread_pool_submit(Task* task){
    if(task == NULL){
        printf("submitted null task\n");
        return;
    }
    //printf("SUBMIT: trans = %d, pnum = %d\n", task->rdd->trans, task->pnum);
    queue_push(g_taskqueue, task);
}

void metrics_submit(TaskMetric* metric) {
    Task* task = (Task*)malloc(sizeof(Task));
    task->metric = metric;
    task->rdd    = NULL;  
    task->pnum   = -1;

    pthread_mutex_lock(&g_metricQueue->backlock);
    queue_push(g_metricQueue, task);
    pthread_cond_signal(&g_metricCond);
    pthread_mutex_unlock(&g_metricQueue->backlock);
}

void thread_pool_wait(){
    //TODO: potential deadlock, holding 2 locks at the same time
    //g_pool_lock, and queue_size waits for q->backlock 
    pthread_mutex_lock(&g_pool_lock);
    //printf("q size: %d\n", queue_size(g_taskqueue));
    while(queue_size(g_taskqueue) != 0 || g_activeThreads > 0){
        pthread_cond_wait(&qempty, &g_pool_lock);
    }
    pthread_mutex_unlock(&g_pool_lock);
}

void thread_pool_destroy(){
    for(int i=0; i<g_threadCount; i++){
        Task* k = malloc(sizeof(Task));
        k->rdd = malloc(sizeof(RDD));
        if(k->rdd==NULL){
            perror("pool destory malloc");
            exit(1);
        }
        k->rdd->trans = KILL;
        k->pnum = -1;
        k->metric = NULL;
        thread_pool_submit(k);
        //free(k.rdd);
    }
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
        //free(curr->t);
        free(curr);
    }
    free(g_taskqueue);
    g_taskqueue = NULL;
}

void metrics_destroy() {
    pthread_mutex_lock(&g_metricQueue->backlock);
    g_metricsRunning = 0;
    
	pthread_cond_broadcast(&g_metricCond);
    pthread_mutex_unlock(&g_metricQueue->backlock);

    pthread_join(g_metricsThread, NULL);

    fclose(g_metricsFile);
    g_metricsFile= NULL;

    qnode* c= g_metricQueue->front;
    while(c){
        qnode* nxt= c->next;
        free(c->t); 
        free(c);
        c= nxt;
    }
    free(g_metricQueue);
    g_metricQueue= NULL;
}

//LL functions
List* list_init(int t){
    List *temp = (List*)malloc(sizeof(List));
    if(temp==NULL){
        perror("list init malloc fail");
        exit(1);
    }
    temp->head=NULL;
    temp->tail=NULL;
    temp->size=0;
    temp->isList = t;
    if(!t){//0=locks, 1=no locks
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
    if(l->tail == NULL){
        l->tail = curr;
    }
    if(!l->isList){
        pthread_mutex_unlock(&l->guard);
    }
} 
void list_append(List* l, void* e){
    if(l->tail == NULL){
        list_add_elem(l, e);
        return;
    }
    ListNode* curr = malloc(sizeof(ListNode));
    if(curr==NULL){
        perror("list add elem malloc fail");
        exit(1);
    }
    curr->data = e;
    if(!l->isList){
        pthread_mutex_lock(&l->guard);
    }
    l->tail->next = curr;
    l->tail = curr;
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
ListNode* list_get(List* l, int idx){
    if(idx<0 || idx>= l->size) return NULL;
    ListNode* curr = l->head;
    for(int i=0; i<idx; i++){
        curr=curr->next;
    }
    return curr;
}

void listit_seek_to_start(List* l, ListIt* it){
    it->curr = l->head;
}
ListNode* listit_next(List* l, ListIt* it){
    ListNode* res = it->curr;
    if(res){
        //advance iterator if not last
        it->curr = it->curr->next;
    }
    return res;
}
/*
int listit_next(List* l, ListIt* it){
    if(it->curr){
        it->curr = it->curr->next;
        return 1;
    }
    return 0;
}
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


RDD *create_rdd(int numdeps, Transform t, void *fn, ...)
    //map(RDD* files, GetLines) => create_rdd(1, MA, GetLines, RDD* files)
    //numdeps=1, Transform=MAP, fn=GetLines, dep=files
  //RDD *rdd = create_rdd(2, JOIN, fn, dep1, dep2);
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

    dep->child = rdd;
  }
  va_end(args);
  rdd->trans = t;
  rdd->fn = fn;
  //TODO: might not be correct use
  rdd->numpartitions = maxpartitions;
  rdd->numdependencies = numdeps;
  //rdd->partitions = list_init(1);
  pthread_mutex_init(&rdd->pdeplock, NULL);
  rdd->child = NULL;
  
  return rdd;
}

/* RDD constructors */
RDD *map(RDD *dep, Mapper fn)
{
  RDD* rdd = create_rdd(1, MAP, fn, dep);
  rdd->partitions = list_init(1);
  for(int i=0; i<rdd->numpartitions; i++){
      List* temp = list_init(0);
      //list_add_elem(rdd->partitions, temp);
      list_append(rdd->partitions, temp);
  }

  rdd->pdep = malloc(sizeof(int)*rdd->numpartitions);
  if(rdd->pdep==NULL){
      perror("map malloc error");
      exit(1);
  }
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
  if(rdd->pdep==NULL){
      perror("filter malloc error");
      exit(1);
  }
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = 1;
  }
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
  //pdeplock is init inside create_rdd();
  /*
  rdd->pdeplock = malloc(sizeof(pthread_mutex_t)*rdd->numpartitions);
  if(rdd->pdep==NULL || rdd->pdeplock==NULL){
      printf("partition malloc error");
      exit(1);
  }
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = rdd->numdependencies;
      pthread_mutex_init(&rdd->pdeplock[i], NULL);
  }
  */

  rdd->ctx = ctx;
  return rdd;
}

RDD *join(RDD *dep1, RDD *dep2, Joiner fn, void *ctx)
{
  RDD *rdd = create_rdd(2, JOIN, fn, dep1, dep2);
  rdd->partitions = list_init(1);
  for(int i=0; i<rdd->numpartitions; i++){
      List* temp = list_init(0);
      list_add_elem(rdd->partitions, temp);
  }
  rdd->pdep = malloc(sizeof(int)*rdd->numpartitions);
  if(rdd->pdep==NULL){
      perror("filter malloc error");
      exit(1);
  }
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = rdd->numpartitions*2;
  }
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
  rdd->partitions = list_init(1);//TODO: can make this no locked?

  for (int i = 0; i < numfiles; i++)
  {
    FILE *fp = fopen(filenames[i], "r");
    if (fp == NULL) {
      perror("fopen");
      exit(1);
    }
    list_append(rdd->partitions, fp);
  }

  rdd->numdependencies = 0;
  //rdd->trans = MAP;
  rdd->trans = FILE_BACKED;
  rdd->fn = (void *)identity;

  rdd->numpartitions = rdd->partitions->size;
  rdd->pdep = NULL;
  pthread_mutex_init(&rdd->pdeplock, NULL);
  rdd->child = NULL;
  return rdd;
}

//return all file backed RDDs in list
void dfs(RDD* root, List* l){
    if(root == NULL){//just in case
        return;
    }
    if(root->trans == FILE_BACKED || root->numdependencies == 0){
        list_add_elem(l, root);
        return;
    }

    for(int i=0; i<root->numdependencies; i++){
        dfs(root->dependencies[i], l);
    }
}
void execute(RDD* rdd) {
    g_activeThreads = 0;
    List* leaves = list_init(0);
    ListIt it;
    dfs(rdd, leaves);
    listit_seek_to_start(leaves, &it);
    for(int i=0; i<leaves->size; i++){
        RDD* curr = (RDD*)(listit_next(leaves, &it)->data);
        for (int j = 0; j < curr->numpartitions; j++){

            Task *t = malloc(sizeof(Task));
            t->rdd = curr;
            t->pnum = j;
            t->metric = NULL;
            thread_pool_submit(t);

            /*
            Task t;
            t.rdd = curr;
            t.pnum = j;
            t.metric = NULL;
            thread_pool_submit(&t);
            */
        }
    }

    thread_pool_wait();
    list_free(leaves);
    //TODO: cleanup RDDs?
}

void MS_Run() {
	cpu_set_t set;
	CPU_ZERO(&set);
	if(sched_getaffinity(0, sizeof(set), &set) == -1){
		perror("sched_getaffinity");
		exit(1);
	}
	thread_pool_init(CPU_COUNT(&set));//create pool w/ same # of threads as cores
	metrics_init();
	return;
}

void MS_TearDown() {
    thread_pool_wait();
	thread_pool_destroy();
	metrics_destroy();
	//TODO: free all RDD's and lists
	return;
}

int count(RDD *rdd) {
  execute(rdd);

  int count = 0;
  // count all the items in rdd
  List* res = rdd->partitions;
  ListIt it;
  listit_seek_to_start(res, &it);
  if(res->isList){//2d l
    ListNode* temp;
    while((temp = listit_next(res, &it))!=NULL){
        count += ((List*)(temp->data))->size;
    }
  }
  else{
    count = res->size;
  }
  return count;
}

void print(RDD *rdd, Printer p) {
  execute(rdd);

  // print all the items in rdd
  // aka... `p(item)` for all items in rdd
  List* res = rdd->partitions;
  ListIt it1;
  listit_seek_to_start(res, &it1);
  ListNode* temp1;
  if(res->isList){//2d l
    while((temp1 = listit_next(res, &it1))!=NULL){
        ListNode* temp2;
        ListIt it2;
        listit_seek_to_start(temp1->data, &it2);
        while((temp2 = listit_next(temp1->data, &it2))!=NULL){
            p(temp2->data);
        }
    }
  }
  else{
    while((temp1 = listit_next(res, &it1))!=NULL){
        p(temp1->data);
    }
  }
}

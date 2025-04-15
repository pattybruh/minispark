#include "minispark.h"


static pthread_t* g_threads = NULL;
static int g_threadCount = 0;
static queue* g_taskqueue = NULL;

//static pthread_cond_t qempty = PTHREAD_COND_INITIALIZER;
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
    q->size = 0;
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
    /*
    while(q->size < 1){
        pthread_cond_wait(&qfill, &q->frontlock);
    }
    */
    if(q->size < 1){
        *val = NULL;
        return;
    }
    qnode* dummy = q->front;
    qnode* newh = dummy->next;
    //should be guaranteed the queue is non empty after waking from condition
    /*
    if(!newh){//empty q
        pthread_mutex_unlock(&q->frontlock);
        *val = NULL;
        return;
    }
    */
    *val = newh->t;
    q->front = newh;
    q->size--;
    //pthread_mutex_unlock(&q->frontlock);
}

//thread function
void* threadstart(void *arg){
    while(1){
        Task* currT = NULL;
        pthread_mutex_lock(&g_taskqueue->frontlock);
        while(g_taskqueue->size < 1){
            pthread_cond_wait(&qfill, &g_taskqueue->frontlock);
        }
        queue_pop(g_taskqueue, &currT);
        pthread_mutex_unlock(&g_taskqueue->frontlock);
        if(currT==NULL){
            continue;
        }

        RDD* currRDD = currT->rdd;
        if(currRDD == NULL){
            perror("NULL rdd submitted");
            exit(1);
        }
        int pnum = currT->pnum;

        switch(currRDD->trans){
            case FILE_BACKED: {
                //FILE_BACKED: paritions should be mapped 1:1
                //no work todo except submit child's partitions
                if(currRDD->child){
                    //locks are null unless paritionby
                    if(--(currRDD->child->pdep[pnum]) == 0){
                        Task newTask;
                        newTask.rdd = currRDD->child;
                        newTask.pnum = pnum;
                        newTask.metric = NULL;
                        thread_pool_submit(&newTask);
                    }
                }
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
                    printf("error list_get\n");
                    exit(1);
                }
                //currP->data = list_init(0);//alr created by RDD* map()
                ListNode* parentP = list_get(parentRDD->partitions, pnum);

                if(parentRDD->trans == FILE_BACKED){
                    void* res=NULL;
                    while((res = func(parentP->data)) != NULL){
                        list_append(currP->data, res);
                    }
                }
                else{
                    ListIt it;
                    listit_seek_to_start((List*)parentP->data, &it);
                    ListNode* temp;
                    while((temp=listit_next(parentP->data, &it)) != NULL){
                        list_append(currP->data, func((it.curr)->data));
                    }
                }

                if(currRDD->child && (--(currRDD->child->pdep[pnum])==0)){
                    Task newTask;
                    newTask.rdd=currRDD->child;
                    newTask.pnum = pnum;
                    newTask.metric = NULL;
                    thread_pool_submit(&newTask);
                }
                break;
            }
            case FILTER: {
				RDD* parentRDD = currRDD -> dependencies[0];
				Filter fn = (Filter)currRDD -> fn;
				ListNode* filter = list_get(currRDD->partitions, pnum);
				if(!filter){
					printf("error list_get\n");
					exit(1);
				}
				if(!filter -> data){
					filter -> data = list_init(0);
				List* outList = (List*)filter -> data; 
                ListNode* parentNode = list_get(parent->partitions, pnum);
				if(!parentNode){
					break;
				}
				if(parent->trans == FILE_BACKED){
					FILE* fp = (FILE*)parentNode->data;
					char* line = NULL;
					size_t size = 0;
					while(1){
						ssize_t n = getline(&line, &size, fp);
						if(n < 0){
							if(line){
								free(line);
							}
							break;
						}
						int keep = fn(line, currRdd->ctx);
						if(keep != NULL){
							list_append(outList, line);
						}else{
							free(line);
						}
						line = NULL;
						size = 0;
					}
				}else{
					List* parentList = (List*)parentNode -> data;
					ListIt it;
					listit_seek_to_start(parentList, &it);
					while(1){
						ListNode* node = listit_next(parentList, &it);
						if(node == NULL){
							break;
						}
						void* val = node->data;
						int keep = fn(val, currRDD->ctx);
						if(keep){
							list_append(outList, val);
						}
					}
				}
				if(currRDD->child){
					if(--(currRDD->child->pdep[pnum]) == 0){
						Task newTask;
						newTask.rdd = currRDD->child;
						newTask.pnum = pnum;
						newTask.metric = NULL;
						threa_pool_submit(&newTask);
					}
				}
				break;
                }
            }
                
            case JOIN: {
				RDD* parent1 = currRDD->dependencies[0];
				RDD* parent2 = currRDD->dependencies[1];
				Joiner fn = (Joiner)(currRdd->fn);
				
				ListNode* out = list_get(currRdd->partitions, pnum);
				if(!out){
					break;
				}
				if(!out->data){
					out->data = list_init(0);
				}
				List* outList = (List*)out->data;
				List p1List;
				List p2List;
				p1List.head = NULL;
				p2List.head = NULL;
				pthread_mutex_init(&p1List.guard, NULL);	
				pthread_mutex_init(&p2List.guard, NULL);
				p1List.size = 0;
				p2List.size = 0;
				p1List.isList = 0;
				p2List.isList = 0;
				
                break;
            }
            case PARTITIONBY: {
                //TODO: only 1 parition is ever woken up
                //need to find way to add all partitions of a PARITTIONBY to queue
                //
                //right now we are assuming each thread just works on one partition
                RDD* parentRDD = currRDD->dependencies[0];
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

                if(currRDD->child && (--(currRDD->child->pdep[pnum])==0)){
                    Task newTask;
                    newTask.rdd=currRDD->child;
                    newTask.pnum = pnum;
                    newTask.metric = NULL;
                    thread_pool_submit(&newTask);
                }
                break;
            }
            default:
                perror("invalid transform");
                exit(1);
        }
        free(currT);
    }
    return NULL;
}

//thread pool implementation
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

int max(int a, int b)
{
  return a > b ? a : b;
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
  //TODO: might not be correct use
  rdd->numpartitions = maxpartitions;

  rdd->numdependencies = numdeps;
  rdd->trans = t;
  rdd->fn = fn;
  //rdd->partitions = list_init(1);
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
      list_add_elem(rdd->partitions, temp);
  }

  rdd->pdep = malloc(sizeof(int)*rdd->numpartitions);
  if(rdd->pdep==NULL){
      printf("map malloc error");
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
      printf("filter malloc error");
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
  rdd->pdeplock = malloc(sizeof(pthread_mutex_t)*rdd->numpartitions);
  if(rdd->pdep==NULL || rdd->pdeplock==NULL){
      printf("partition malloc error");
      exit(1);
  }
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = rdd->numdependencies;
      pthread_mutex_init(&rdd->pdeplock[i], NULL);
  }

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
      printf("filter malloc error");
      exit(1);
  }
  for(int i=0; i<rdd->numpartitions; i++){
      rdd->pdep[i] = 
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
  rdd->partitions = list_init(0);//TODO: can make this no locked?

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
    List* leaves = list_init(1);
    ListIt it;
    dfs(rdd, leaves);
    listit_seek_to_start(leaves, &it);
    for(int i=0; i<leaves->size; i++){
        RDD* curr = (RDD*)(listit_next(leaves, &it)->data);
        for(int j=0; j<curr->numpartitions; j++){
            Task t;
            t.rdd = curr;
            t.pnum = j;
            t.metric = NULL;
            thread_pool_submit(&t);
            //free(t);//TODO: threads will free(t) after popping from queue
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

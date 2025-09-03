# MiniSpark: Concurrent Data Processing Framework

MiniSpark is a simplified, single node reimplementation of the Spark data processing framework written in C.
It executes transformations on **Resilient Distributed Dataset (RDD) nodes**, in parallel using a custom **thread pool**, a **dependency aware scheduler**, and runtime **metrics collection**.

MiniSpark is ran on a single machine so its RDDs are nodes that simply hold data and are neither resilient nor distributed.

---

## Overview

Distributed data frameworks like Spark power large scale analytics by handling parallelism, scheduling, and data partitioning behind the scenes.  
**MiniSpark** recreates these ideas at a smaller scale using only C and POSIX threads.  

- **Lazy Computation:** RDDs are not materialized until an **action** (`count`, `print`) is invoked. This allows entire DAGs of transformations to be constructed first, then computed efficiently when needed.  
- **Purpose:** Learn concurrency by building a framework that schedules tasks and executes transformations in parallel.  
- **Scope:** Single machine, thread based parallelism
- **Key Focus:** Correctness under concurrency, avoiding deadlocks, and efficient scheduling.  


---

## Features
- **Thread Pool + Work Queue** – dynamic number of worker threads (based on available cores) with a bounded, thread safe queue.  
- **Dependency-Aware Scheduler** – tasks are only enqueued when dependencies are satisfied, preventing deadlocks and maximizing parallelism.  
- **RDD Transformations** – supports `map`, `filter`, `partitionBy`, and `join`.  
- **Actions** – supports `count` and `print` operations on RDD DAGs.  
- **Runtime Metrics** – logs enqueue, start, and finish times for every task to `metrics.log`, enabling fine-grained profiling.  

---

## RDDs and Transformations

MiniSpark programs are built as **DAGs (Directed Acyclic Graphs)** of RDDs. Each RDD represents data, and edges represent transformations.  

Supported transformations:  
- **map** – applies a function to every element in each partition.  
- **filter** – removes elements that do not satisfy a predicate.  
- **partitionBy** – repartitions data across a given number of partitions.  
- **join** – inner join on key value RDDs (hash partitioned).  

Actions:  
- **count** – returns number of elements.  
- **print** – prints all elements to stdout.  

### Example DAG
Below is an example DAG for a simple `linecount` program:  
```c
void* GetLines(void* arg) {
    FILE *fp = (FILE*)arg;

    char *line = NULL;
    size_t size = 0;
  
    if (getline(&line, &size, fp) < 0) {
      return NULL;
    }
    return line;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
      printf("need at least one file\n");
      return -1;
    }

    MS_Run();
  
    RDD* files = RDDFromFiles(argv + 1, argc - 1);
    printf("total number of lines: %d\n", count(map(files, GetLines)));
  
    MS_TearDown();
    return 0;
}
```
This DAG shows how input files are mapped to lines, then materialized and counted in parallel.


![graphic of linecount program](./graphics/getline.png)


---

## Running
Build with `make` on Linux:

Run tests by running the executable in the `/tests` directory:
```bash
./tests/run-tests.sh
```
Logs of task execution metrics are written to `metrics.log`.

Depencies:  
- GCC
- POSIX threads (pthread)
- Linux environment

## Acknowledgements
This project was inspired by Spark's RDD model and implemented as part of University of Wisconsin - Madison's CS527 Intro to Operating Systems course.
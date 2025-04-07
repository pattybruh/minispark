#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sched.h>
#include <stdarg.h>
#include "lib.h"
#include "minispark.h"

#define ROUNDS 10
#define NUMFILES (1<<ROUNDS)
#define FILENAMESIZE 50

int main(int argc, char* argv[]) {

  MS_Run();
  char *filenames[NUMFILES];
  RDD* files[2];
  
  struct colpart_ctx pctx;
  pctx.keynum = 0;

  for (int i=0; i< NUMFILES/2; i++) {
    filenames[i] = calloc(FILENAMESIZE,1);
    sprintf(filenames[i], "./test_files/largevals%d.txt", i);
     }
  for (int i=NUMFILES/2; i< NUMFILES; i++) {
    filenames[i] = calloc(FILENAMESIZE,1);
    sprintf(filenames[i], "./test_files/largevals%d.txt", i);
     }

  files[0] = partitionBy(map(map(RDDFromFiles(filenames, NUMFILES/2), GetLines), SplitCols), ColumnHashPartitioner, 128, &pctx);
  files[0] = partitionBy(files[0], ColumnHashPartitioner, 53, &pctx);
//   files[0] = partitionBy(files[0], ColumnHashPartitioner, 512, &pctx);
  files[1] = partitionBy(map(map(RDDFromFiles((filenames + NUMFILES/2), NUMFILES/2), GetLines), SplitCols), ColumnHashPartitioner, 128, &pctx);
 
  print(files[0], RowPrinter);
  print(files[1], RowPrinter);
  
  MS_TearDown();
  return 0;
}

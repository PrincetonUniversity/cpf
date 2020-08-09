#include <stdio.h>
#include <stdlib.h>

unsigned NUM_NODES;

#define NONE                               9999

struct _NODE
{
  int iDist;
  int iPrev;
};
typedef struct _NODE NODE;

struct _QITEM
{
  int iNode;
  int iDist;
  int iPrev;
  struct _QITEM *qNext;
};
typedef struct _QITEM QITEM;

QITEM *qHead = NULL;


//int AdjMatrix[NUM_NODES][NUM_NODES];
//NODE rgnNodes[NUM_NODES];
int **AdjMatrix;
NODE *rgnNodes;

void allocate_matrices()
{
  AdjMatrix = (int**) malloc( NUM_NODES * sizeof(int*) );
  for(int i=0; i<NUM_NODES; ++i)
    AdjMatrix[i] = (int*) malloc( NUM_NODES * sizeof(int) );

  rgnNodes = (NODE*) malloc( NUM_NODES * sizeof(NODE) );
}

void free_matrices()
{
  free( rgnNodes );

  for(int i=0; i<NUM_NODES; ++i)
    free( AdjMatrix[i] );
  free( AdjMatrix );
}


int g_qCount = 0;
int ch;
int iPrev, iNode;
int i, iCost, iDist;


void print_path (NODE *rgnNodes, int chNode)
{
  if (rgnNodes[chNode].iPrev != NONE)
  {
    print_path(rgnNodes, rgnNodes[chNode].iPrev);
  }
  printf (" %d", chNode);
  fflush(stdout);
}


void enqueue (int iNode, int iDist, int iPrev)
{
  QITEM *qNew = (QITEM *) malloc(sizeof(QITEM));
  QITEM *qLast = qHead;

  if (!qNew)
  {
    fprintf(stderr, "Out of memory.\n");
    exit(1);
  }
  qNew->iNode = iNode;
  qNew->iDist = iDist;
  qNew->iPrev = iPrev;
  qNew->qNext = NULL;

  if (!qLast)
  {
    qHead = qNew;
  }
  else
  {
    while (qLast->qNext) qLast = qLast->qNext;
    qLast->qNext = qNew;
  }
  g_qCount++;
}


void dequeue (int *piNode, int *piDist, int *piPrev)
{
  QITEM *qKill = qHead;

  if (qHead)
  {
    *piNode = qHead->iNode;
    *piDist = qHead->iDist;
    *piPrev = qHead->iPrev;
    qHead = qHead->qNext;
    free(qKill);
    g_qCount--;
  }
}


int qcount (void)
{
  return(g_qCount);
}

int dijkstra(int chStart, int chEnd)
{
  for (ch = 0; ch < NUM_NODES; ch++)
  {
    rgnNodes[ch].iDist = NONE;
    rgnNodes[ch].iPrev = NONE;
  }

  if (chStart == chEnd)
  {
    printf("Shortest path is 0 in cost. Just stay where you are.\n");
  }
  else
  {
    rgnNodes[chStart].iDist = 0;
    rgnNodes[chStart].iPrev = NONE;

    enqueue (chStart, 0, NONE);

    while (qcount() > 0)
    {
      dequeue (&iNode, &iDist, &iPrev);
      for (i = 0; i < NUM_NODES; i++)
      {
        if ((iCost = AdjMatrix[iNode][i]) != NONE)
        {
          if ((NONE == rgnNodes[i].iDist) ||
              (rgnNodes[i].iDist > (iCost + iDist)))
          {
            rgnNodes[i].iDist = iDist + iCost;
            rgnNodes[i].iPrev = iNode;
            enqueue (i, iDist + iCost, iNode);
          }
        }
      }
    }

    printf("Shortest path is %d in cost. ", rgnNodes[chEnd].iDist);
    printf("Path is: ");
    print_path(rgnNodes, chEnd);
    printf("\n");
  }
}

int main(int argc, char *argv[]) {
  int i,j,k;
  FILE *fp;

  if (argc<3) {
    fprintf(stderr, "Usage: dijkstra <filename> <size>\n");
  }

  /* open the adjacency matrix file */
  fp = fopen (argv[1],"r");
  NUM_NODES = atoi( argv[2] );

  allocate_matrices();

  /* make a fully connected matrix */
  for (i=0;i<NUM_NODES;i++) {
    for (j=0;j<NUM_NODES;j++) {
      /* make it more sparce */
      fscanf(fp,"%d",&k);
      AdjMatrix[i][j]= k;
    }
  }

  /* finds 10 shortest paths between nodes */
  const int N = NUM_NODES;
  for (i=0,j=N/2;i<N;i++,j++) {
    j=j%N;
    dijkstra(i,j);
  }

  free_matrices();
  exit(0);


}

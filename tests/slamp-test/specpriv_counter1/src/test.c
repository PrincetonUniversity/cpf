#include <stdio.h>
#include <stdlib.h>

#define N 1000000
int main() {
  // have a loop that malloc in one iteration and free in the next
  int *p;
  for (int i = 0; i < N; i++) {
    if (i == 0) {
      p = (int *) malloc(sizeof(int));
      *p = 0;
    }
    else if (i == N - 1) {
      free(p);
    } else {
      *p = *p + 1;
    }
  }

}

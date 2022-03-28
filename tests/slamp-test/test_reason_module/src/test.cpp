/**
 * Test constant module of SLAMP
 *
 * Ziyang Xu
 * Liberty Research Group, Princeton
 */

#include <cstdio>
#include <cstdlib>

void foo(int *ptr, int i) {
    *ptr = 2 * i + 7;
}

int main(int argc, char **argv) {
  unsigned iter;
  if (argc == 2) {
    iter = atoi(argv[1]);
  } else {
    printf("Need one arguments: iter\n");
  }

  auto p = new int();

  // initialize value
  auto sum = 0;
  for (int i = 0; i < iter; i++) {
    foo(p, i);
    foo(p, i + 1);
    sum = (sum + *p) % 101;
  }
  
  printf("%d\n", sum);
  return 0;
}

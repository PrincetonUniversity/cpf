/**
 * Test constant module of SLAMP
 *
 * Ziyang Xu
 * Liberty Research Group, Princeton
 */

#include <cstdio>
#include <cstdlib>

void foo(int *ptr) {
  *ptr = 10;
}

int main(int argc, char **argv) {
  unsigned iter;
  if (argc == 2) {
    iter = atoi(argv[1]);
  } else {
    printf("Need one arguments: iter\n");
  }

  auto a = new int[iter]();

  // initialize value
  auto sum = 0;
  for (int i = 0; i < iter; i++) {
    foo(a + i);
    sum = (sum + a[i]) % 101;
  }
  
  printf("%d\n", sum);
  return 0;
}

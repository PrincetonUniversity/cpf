/**
 * Test constant module of SLAMP
 *
 * Ziyang Xu
 * Liberty Research Group, Princeton
 */

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
  unsigned iter;
  if (argc == 2) {
    iter = atoi(argv[1]);
  } else {
    printf("Need one arguments: iter\n");
  }

  auto a = new int[iter]();

  a[0] = 0;
  // initialize value
  for (int i = 1; i < iter; i++) {
    // load
    int tmp = a[(i * (i+1)) % iter];

    // store
    a[i] = (tmp % 3 + tmp % 6 + tmp % 9) / 13; // should always be 0
  }

  auto sum = 0;
  for (int i = 0; i < iter; i++) {
    sum += a[i];
  }
  
  printf("%d\n", sum);
  return 0;
}

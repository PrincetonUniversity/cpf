/**
 * Test validity of SLAMP
 *
 * Ziyang Xu
 * Liberty Research Group, Princeton
 */

#include <stdio.h>
#include <stdlib.h>

int indirectLoad(const int *place, bool do_it) {
  if (do_it) {
    return *place;
  } else {
    return 0;
  }
}

void indirectStore(int *place, int value, bool do_it) {
  if (do_it) {
    *place = value;
  } else {
    return;
  }
}

int *data;

int main(int argc, char **argv) {
  unsigned iter;
  unsigned len;
  unsigned freq;
  if (argc == 4) {
    iter = atoi(argv[1]);
    len = atoi(argv[2]);
    freq = atoi(argv[3]);
  } else {
    printf("Need three arguments: iter, len, freq\n");
  }

  data = new int[len]; //(int *)malloc(sizeof(int) * len);

  // initialization of array
  for (auto i = 0; i < len; i++) {
    data[i] = i;
  }

  // try to create some flow deps
  auto sum = 0;
  for (auto i = 0; i < iter; i++) {
    for (auto j = 0; j < len; j++) {
      sum += indirectLoad(data + j, (j % freq == freq - 1)) % 10001;
      indirectStore(data+j, sum, (j % freq == freq - 1));
    }
  }

  printf("%d", sum);
  delete data;
  return 0;
}

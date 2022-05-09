#include <cstdlib>
#include <iostream>
#include <thread>

int nthreads;

void task(int tid, int *array, int size) {
  int batch = size / nthreads;
  int k1 = tid * batch;
  int k2 = (tid + 1) * batch;
  if (tid == nthreads - 1) {
    k2 = size;
  }

  for (int i = k1; i < k2; i++) {
    array[i] += 1;
  }

  return;
}

int main(int argc, char *argv[]) {
  nthreads = atoi(argv[1]);
  int size = atoi(argv[2]);
  int offset = atoi(argv[3]);

  int *array = (int *)calloc(size, sizeof(int));

  std::thread *threads = new std::thread[nthreads];
  for (int i = 0; i < nthreads; i++) {
    threads[i] = std::thread(task, i, array, size);
  }
  for (int i = 0; i < nthreads; i++) {
    threads[i].join();
  }

  std::cout << array[offset] << std::endl;
  free(array);

  return 0;
}

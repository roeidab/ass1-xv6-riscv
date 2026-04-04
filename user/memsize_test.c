#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int initial_size, after_alloc_size, after_free_size;
  char *ptr;

  // Print initial memory usage
  initial_size = memsize();
  printf("Initial memory size: %d bytes\n", initial_size);

  // Allocate 20KB (20480 bytes)
  ptr = malloc(20480);
  if (ptr == 0) {
    printf("malloc failed\n");
    exit(1);
  }

  // Print memory usage after allocation
  after_alloc_size = memsize();
  printf("Memory size after allocating 20KB: %d bytes\n", after_alloc_size);
  printf("Difference: %d bytes\n", after_alloc_size - initial_size);

  // Free the allocated memory
  free(ptr);

  // Print memory usage after freeing
  after_free_size = memsize();
  printf("Memory size after freeing: %d bytes\n", after_free_size);

  exit(0);
}

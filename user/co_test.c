#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int ret;

  // 1. non-existent pid
  ret = co_yield(9999, 1);
  printf("non-existent pid test: %d\n", ret);

  // 2. self-yield
  ret = co_yield(getpid(), 1);
  printf("self-yield test: %d\n", ret);

  // 3. killed process
  int child = fork();
  if(child < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(child == 0){
    sleep(100);
    exit(0);
  }

  kill(child);
  ret = co_yield(child, 1);
  printf("killed process test: %d\n", ret);
  wait(0);

  // 4. normal ping-pong
  int parent_pid = getpid();
  int child2 = fork();

  if(child2 < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(child2 == 0){
    for(;;){
      int value = co_yield(parent_pid, 1);
      if(value < 0){
        printf("child: co_yield failed\n");
        exit(1);
      }
      printf("child received: %d\n", value);
    }
  } else {
    for(;;){
      int value = co_yield(child2, 2);
      if(value < 0){
        printf("parent: co_yield failed\n");
        kill(child2);
        wait(0);
        exit(1);
      }
      printf("parent received: %d\n", value);
    }
  }

  exit(0);
}
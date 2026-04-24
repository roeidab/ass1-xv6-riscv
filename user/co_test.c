#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int ret;

  printf("=== Test 1: Error Cases ===\n");
  
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

  printf("\n=== Test 2: Simple Ping-Pong (10 rounds) ===\n");
  
  // 4. normal ping-pong
  int parent_pid = getpid();
  int child2 = fork();

  if(child2 < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(child2 == 0){
    for(int i = 0; i < 10; i++){
      int value = co_yield(parent_pid, 100 + i);
      if(value < 0){
        printf("child: co_yield failed\n");
        exit(1);
      }
    }
    printf("child: completed\n");
    exit(0);
  } else {
    for(int i = 0; i < 10; i++){
      int value = co_yield(child2, 200 + i);
      if(value < 0){
        printf("parent: co_yield failed\n");
        kill(child2);
        wait(0);
        exit(1);
      }
    }
    printf("parent: completed\n");
  }

  wait(0);

  printf("\n=== Test 3: Value Passing (Different Values) ===\n");
  
  // 5. Test with different/distinct values
  int parent_pid3 = getpid();
  int child3 = fork();
  if(child3 < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(child3 == 0){
    // Child sends sequence 1000, 2000, 3000
    int v1 = co_yield(parent_pid3, 1000);
    if(v1 != 111) {
      printf("ERROR: child expected 111, got %d\n", v1);
      exit(1);
    }
    
    int v2 = co_yield(parent_pid3, 2000);
    if(v2 != 222) {
      printf("ERROR: child expected 222, got %d\n", v2);
      exit(1);
    }
    
    int v3 = co_yield(parent_pid3, 3000);
    if(v3 != 333) {
      printf("ERROR: child expected 333, got %d\n", v3);
      exit(1);
    }
    
    printf("child: all values correct\n");
    exit(0);
  } else {
    // Parent sends sequence 111, 222, 333
    int v1 = co_yield(child3, 111);
    if(v1 != 1000) {
      printf("ERROR: parent expected 1000, got %d\n", v1);
      kill(child3);
      wait(0);
      exit(1);
    }
    
    int v2 = co_yield(child3, 222);
    if(v2 != 2000) {
      printf("ERROR: parent expected 2000, got %d\n", v2);
      kill(child3);
      wait(0);
      exit(1);
    }
    
    int v3 = co_yield(child3, 333);
    if(v3 != 3000) {
      printf("ERROR: parent expected 3000, got %d\n", v3);
      kill(child3);
      wait(0);
      exit(1);
    }
    
    printf("parent: all values correct\n");
  }
  
  wait(0);

  printf("\n=== Test 4: Rapid Alternation (50 rounds) ===\n");
  
  // 6. stress test with many rounds
  int parent_pid4 = getpid();
  int child4 = fork();
  if(child4 < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(child4 == 0){
    // Child just echoes back parent's value + 1
    for(int i = 0; i < 50; i++){
      int value = co_yield(parent_pid4, i + 1000);
      if(value < 0){
        printf("child stress test failed\n");
        exit(1);
      }
    }
    printf("child: stress test completed\n");
    exit(0);
  } else {
    // Parent tracks values
    for(int i = 0; i < 50; i++){
      int value = co_yield(child4, i + 5000);
      if(value < 0){
        printf("parent stress test failed\n");
        kill(child4);
        wait(0);
        exit(1);
      }
    }
    printf("parent: stress test completed\n");
  }
  
  wait(0);

  printf("\n=== Test 5: Parent/Child co_yield Ping-Pong ===\n");

  int pid1 = getpid(); // Parent PID
  int pid2 = fork(); // Child PID
  if(pid2 < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(pid2 == 0){ // Child
    for(int i = 0; i < 10; i++){
      int value = co_yield(pid1, 1);
      printf("Child received: %d\n", value); // Should print 2
    }
    exit(0);
  } else { // Parent
    for(int i = 0; i < 10; i++){
      int value = co_yield(pid2, 2);
      printf("parent received: %d\n", value); // Should print 1
    }
    wait(0);
  }

  printf("\n=== All Tests Passed ===\n");
  exit(0);
}
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int test_num = 0;
static int fail_count = 0;

void pass(char *name)
{
  test_num++;
  printf("TEST %d PASSED: %s\n", test_num, name);
}

void fail(char *name, char *reason)
{
  test_num++;
  fail_count++;
  printf("TEST %d FAILED: %s -- %s\n", test_num, name, reason);
}

// ---------------------------------------------------------------
// Test 1: yield to self should return -1
// ---------------------------------------------------------------
void test_yield_to_self(void)
{
  int r = co_yield(getpid(), 42);
  if (r == -1)
    pass("yield to self");
  else
    fail("yield to self", "expected -1");
}

// ---------------------------------------------------------------
// Test 2: yield to negative PID should return -1
// ---------------------------------------------------------------
void test_negative_pid(void)
{
  int r = co_yield(-1, 42);
  if (r == -1)
    pass("negative pid");
  else
    fail("negative pid", "expected -1");
}

// ---------------------------------------------------------------
// Test 3: yield to PID 0 should return -1
// ---------------------------------------------------------------
void test_zero_pid(void)
{
  int r = co_yield(0, 42);
  if (r == -1)
    pass("zero pid");
  else
    fail("zero pid", "expected -1");
}

// ---------------------------------------------------------------
// Test 4: yield to non-existent PID should return -1
// ---------------------------------------------------------------
void test_nonexistent_pid(void)
{
  int r = co_yield(9999, 42);
  if (r == -1)
    pass("nonexistent pid");
  else
    fail("nonexistent pid", "expected -1");
}

// ---------------------------------------------------------------
// Test 5: basic one-time ping-pong exchange
//   Child does one co_yield, parent does one, then parent
//   kills child (in case child is the sleeping winner) and waits.
// ---------------------------------------------------------------
void test_basic_exchange(void)
{
  int pid1 = getpid();
  int pid2 = fork();
  if (pid2 < 0)
  {
    fail("basic exchange", "fork failed");
    return;
  }

  if (pid2 == 0)
  {
    co_yield(pid1, 1);
    exit(0);
  }

  int val = co_yield(pid2, 2);
  kill(pid2);
  wait(0);
  if (val == 1)
    pass("basic exchange");
  else
    fail("basic exchange", "expected 1");
}

// ---------------------------------------------------------------
// Test 6: stress test -- 1000 exchanges
//   Child loops forever sending 1; parent verifies it receives 1
//   each of the 1000 rounds.
// ---------------------------------------------------------------
void test_stress_1000(void)
{
  int pid1 = getpid();
  int pid2 = fork();
  if (pid2 < 0)
  {
    fail("stress 1000", "fork failed");
    return;
  }

  if (pid2 == 0)
  {
    for (;;)
      co_yield(pid1, 1);
  }

  int ok = 1;
  for (int i = 0; i < 1000; i++)
  {
    int val = co_yield(pid2, 2);
    if (val != 1)
    {
      printf("  iter %d: got %d expected 1\n", i, val);
      ok = 0;
      break;
    }
  }
  kill(pid2);
  wait(0);
  if (ok)
    pass("stress 1000");
  else
    fail("stress 1000", "value mismatch");
}

// ---------------------------------------------------------------
// Test 7: partner already dead when co_yield is called
//   Child exits immediately. Parent waits, then co_yields to
//   the now-zombie/reaped child -> should return -1.
// ---------------------------------------------------------------
void test_partner_already_dead(void)
{
  int pid2 = fork();
  if (pid2 < 0)
  {
    fail("partner already dead", "fork failed");
    return;
  }

  if (pid2 == 0)
  {
    exit(0);
  }

  wait(0); // reap child first
  int val = co_yield(pid2, 42);
  if (val == -1)
    pass("partner already dead");
  else
    fail("partner already dead", "expected -1");
}

// ---------------------------------------------------------------
// Test 8: partner exits while we are sleeping in co_yield
//   Parent calls co_yield first (becomes loser, sleeps).
//   Child exits without calling co_yield -> exit() wakes parent
//   via wakeup(&child->context) -> parent gets sentinel -1.
// ---------------------------------------------------------------
void test_partner_exits_while_sleeping(void)
{
  int pid2 = fork();
  if (pid2 < 0)
  {
    fail("partner exits while sleeping", "fork failed");
    return;
  }

  if (pid2 == 0)
  {
    sleep(5); // let parent call co_yield first and sleep
    exit(0);  // exit without co_yield -> wakes parent
  }

  int val = co_yield(pid2, 42);
  wait(0);
  if (val == -1)
    pass("partner exits while sleeping");
  else
    //printf("  got %d expected -1\n", val);
    fail("partner exits while sleeping", "expected -1");
}

// ---------------------------------------------------------------
// Test 9: large values survive the exchange
// ---------------------------------------------------------------
void test_large_values(void)
{
  int pid1 = getpid();
  int pid2 = fork();
  if (pid2 < 0)
  {
    fail("large values", "fork failed");
    return;
  }

  if (pid2 == 0)
  {
    for (;;)
      co_yield(pid1, 999999);
  }

  int val = co_yield(pid2, 123456);
  kill(pid2);
  wait(0);
  if (val == 999999)
    pass("large values");
  else
    fail("large values", "expected 999999");
}

// ---------------------------------------------------------------
// Test 10: multiple rounds -- 5 back-and-forth exchanges
// ---------------------------------------------------------------
void test_multi_round(void)
{
  int pid1 = getpid();
  int pid2 = fork();
  if (pid2 < 0)
  {
    fail("multi round", "fork failed");
    return;
  }

  if (pid2 == 0)
  {
    for (;;)
      co_yield(pid1, 77);
  }

  int ok = 1;
  for (int i = 0; i < 5; i++)
  {
    int val = co_yield(pid2, 33);
    if (val != 77)
    {
      printf("  round %d: got %d expected 77\n", i, val);
      ok = 0;
      break;
    }
  }
  kill(pid2);
  wait(0);
  if (ok)
    pass("multi round");
  else
    fail("multi round", "value mismatch");
}

// ---------------------------------------------------------------
// Test 11: kill a child that is sleeping inside co_yield
//   Child calls co_yield and sleeps (parent never co_yields back).
//   Parent kills child -> child wakes with sentinel, exits.
//   Verifies kill properly wakes co_yield-sleeping processes.
// ---------------------------------------------------------------
void test_kill_coyield_sleeper(void)
{
  int pid1 = getpid();
  int pid2 = fork();
  if (pid2 < 0)
  {
    fail("kill coyield sleeper", "fork failed");
    return;
  }

  if (pid2 == 0)
  {
    co_yield(pid1, 1); // sleeps forever (parent won't co_yield)
    exit(0);
  }

  sleep(5);   // let child enter co_yield and sleep
  kill(pid2); // wake it via kill
  wait(0);    // must not freeze
  pass("kill coyield sleeper");
}

// ---------------------------------------------------------------
// Test 12: Concurrent Pairs
//   Pair A (P1 <-> P2) and Pair B (P3 <-> P4) ping-pong simultaneously.
//   Proves wait channels do not overlap.
// ---------------------------------------------------------------
void test_concurrent_pairs(void)
{
  int pid1 = getpid(); // P1 saves its PID so P2 can use it
  int pid2 = fork();
  if (pid2 == 0) {
    // CHILD 1 (P2): Ping pongs with Parent (P1)
    for(int i=0; i<100; i++) co_yield(pid1, 111);
    exit(0);
  }

  int pid3 = fork();
  if (pid3 == 0) {
    int p3_pid = getpid(); // P3 saves its PID so P4 can use it
    int pid4 = fork();
    if (pid4 == 0) {
      // CHILD 3 (P4): Ping pongs with P3
      for(int i=0; i<100; i++) co_yield(p3_pid, 444);
      exit(0);
    }
    
    // CHILD 2 (P3): Ping pongs with P4
    int val_b = 0;
    for(int i=0; i<100; i++) val_b = co_yield(pid4, 333);
    
    kill(pid4); 
    wait(0);
    
    if(val_b == 444) exit(0); else exit(1);
  }

  // PARENT (P1): Ping pongs with P2
  int val_a = 0;
  for(int i=0; i<100; i++) val_a = co_yield(pid2, 222);

  kill(pid2); 
  wait(0); // Reap P2
  
  // Wait for the second pair (P3) to finish and check their exit status
  int status;
  wait(&status); // Pass the pointer exactly as user.h expects

  if (val_a == 111 && status == 0)
    pass("concurrent pairs");
  else
    fail("concurrent pairs", "data mixed up between pairs");
}

// ---------------------------------------------------------------
// Test 13: non-parent waiter should wake when target exits
//   Create siblings: waiter W and target T.
//   W calls co_yield(T, ...), T exits without yielding back.
//   W should return -1 and exit cleanly without requiring a kill.
// ---------------------------------------------------------------
void test_nonparent_target_exit(void)
{
  int target = fork();
  if(target < 0){
    fail("non-parent target exit", "fork target failed");
    return;
  }

  if(target == 0){
    sleep(10);
    exit(0);
  }

  int waiter = fork();
  if(waiter < 0){
    kill(target);
    wait(0);
    fail("non-parent target exit", "fork waiter failed");
    return;
  }

  if(waiter == 0){
    int v = co_yield(target, 55);
    if(v == -1)
      exit(0);
    exit(2);
  }

  sleep(40);

  // Ensure we don't hang in wait() if waiter is still stuck.
  kill(waiter);

  int status1 = -999, status2 = -999;
  int pid1 = wait(&status1);
  int pid2 = wait(&status2);

  int waiter_status = -999;
  if(pid1 == waiter)
    waiter_status = status1;
  if(pid2 == waiter)
    waiter_status = status2;

  if(waiter_status == 0)
    pass("non-parent target exit");
  else
    fail("non-parent target exit", "waiter stayed blocked or wrong return");
}


// ---------------------------------------------------------------
// Test 14: exit wakes process parked by direct RUNNABLE handoff
//   Parent co_yields to a RUNNABLE child.
//   Parent becomes a co_yield sleeper and switches directly to child.
//   Child exits without yielding back.
//   exit() must wake parent and make co_yield return -1.
// ---------------------------------------------------------------
void test_exit_wakes_waiter_after_runnable_handoff(void)
{
  int pid2 = fork();
  if(pid2 < 0){
    fail("exit wakes waiter after runnable handoff", "fork failed");
    return;
  }

  if(pid2 == 0){
    // If parent switches directly to us, we exit without co_yielding back.
    exit(0);
  }

  int val = co_yield(pid2, 123);
  wait(0);

  if(val == -1)
    pass("exit wakes waiter after runnable handoff");
  else
    fail("exit wakes waiter after runnable handoff", "expected -1");
}


// ---------------------------------------------------------------
// Test 15: dynamic values should not return stale a0/a1 data
//   Each round uses a different value.
//   This catches bugs where co_yield returns an old saved value
//   instead of the current value written into a0 by the peer.
// ---------------------------------------------------------------
void test_dynamic_values_no_stale_return(void)
{
  int pid1 = getpid();
  int pid2 = fork();

  if(pid2 < 0){
    fail("dynamic values no stale return", "fork failed");
    return;
  }

  if(pid2 == 0){
    for(int i = 1; i <= 5; i++){
      co_yield(pid1, 100 + i);
    }
    exit(0);
  }

  int ok = 1;

  for(int i = 1; i <= 5; i++){
    int val = co_yield(pid2, 200 + i);

    if(val != 100 + i){
      printf("  round %d: got %d expected %d\n", i, val, 100 + i);
      ok = 0;
      break;
    }
  }

  kill(pid2);
  wait(0);

  if(ok)
    pass("dynamic values no stale return");
  else
    fail("dynamic values no stale return", "stale or wrong return value");
}

// ---------------------------------------------------------------
int main(int argc, char *argv[])
{
  printf("=== co_yield test suite ===\n\n");

  // Error-condition tests (no fork needed)
  test_yield_to_self();   // 1
  test_negative_pid();    // 2
  test_zero_pid();        // 3
  test_nonexistent_pid(); // 4

  // Functional tests
  test_basic_exchange();               // 5
  test_stress_1000();                  // 6
  test_partner_already_dead();         // 7
  test_partner_exits_while_sleeping(); // 8
  test_large_values();                 // 9
  test_multi_round();                  // 10
  test_kill_coyield_sleeper();         // 11
  test_concurrent_pairs();             // 12
  test_nonparent_target_exit();        // 13
  test_exit_wakes_waiter_after_runnable_handoff(); // 14
  test_dynamic_values_no_stale_return(); // 15
  printf("\n=== Results: %d/%d passed ===\n",
         test_num - fail_count, test_num);
  if (fail_count)
    printf("SOME TESTS FAILED\n");
  else
    printf("ALL TESTS PASSED\n");

  exit(0);
}
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

extern struct proc proc[NPROC];
extern struct spinlock wait_lock;

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_memsize(void)
{
  return myproc()->sz;
}

uint64
sys_co_yield(void)
{
  int pid;
  int value;
  struct proc *p = myproc();
  struct proc *target = 0;
  struct cpu *c = mycpu();

  // Marker for a process sleeping due to direct co_yield handoff.
  void *my_direct_chan = (void *)((uint64)p + 1);

  argint(0, &pid);
  argint(1, &value);

  if(pid <= 0)
    return -1;

  if(pid == p->pid)
    return -1;

  // Save the value this process is offering to the peer.
  p->trapframe->a1 = value;

  acquire(&wait_lock);

  // Find target and keep target->lock held when found.
  for(struct proc *pp = proc; pp < &proc[NPROC]; pp++) {
    acquire(&pp->lock);

    if(pp->pid == pid){
      target = pp;
      break;
    }

    release(&pp->lock);
  }

  if(target == 0){
    release(&wait_lock);
    return -1;
  }

  // Basic error cases.
  if(target->state == UNUSED || target->state == ZOMBIE || target->killed){
    release(&target->lock);
    release(&wait_lock);
    return -1;
  }

  // Slow rendezvous waiter (sleep(p,&wait_lock) path): wake via scheduler.
  if(target->state == SLEEPING &&
     target->chan == target &&
     target->trapframe->a0 == p->pid) {
    int ret = target->trapframe->a1;

    target->trapframe->a0 = value;
    release(&target->lock);
    wakeup(target);
    release(&wait_lock);

    return ret;
  }

  // Direct waiter (already in co_yield direct sleep marker): direct swtch.
  if(target->state == SLEEPING &&
     target->chan == (void *)((uint64)target + 1) &&
     target->trapframe->a0 == p->pid) {

    // Target receives my newly offered value when it resumes.
    target->trapframe->a0 = value;

    // Current process prepares to sleep waiting for the opposite yield.
    acquire(&p->lock);
    p->chan = my_direct_chan;
    p->state = SLEEPING;
    release(&p->lock);

    // Target runs immediately; skip RUNNABLE entirely.
    target->state = RUNNING;
    c->proc = target;

    release(&wait_lock);

    swtch(&p->context, &target->context);

    // When p is resumed later by the opposite yield, continue here.
    c->proc = p;
    p->chan = 0;

    if(!holding(&p->lock))
      acquire(&p->lock);
    p->state = RUNNING;
    release(&p->lock);

    return p->trapframe->a0;
  }

  // Target is not ready yet: wait until it yields back.
  p->trapframe->a0 = pid;
  release(&target->lock);
  sleep(p, &wait_lock);

  release(&wait_lock);

  return p->trapframe->a0;
}
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
  return 0; // not reached
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

// co_yield parks the caller in SLEEPING state and relies on a later
// co_yield from the partner to hand back a return value directly.
// If the partner exits or is killed first, exit()/kill() must make
// the parked process runnable again so co_yield can return -1.
/*
a0 = syscall return value for this process
a2 = temporary kernel bookkeeping: pid this process is waiting for
     while parked in co_yield
*/
uint64
sys_co_yield(void)
{
  int pid;
  int value;
  struct proc *p = myproc();
  struct proc *target = 0;
  struct cpu *c = mycpu();

  argint(0, &pid);
  argint(1, &value);

  if (pid <= 0)
    return -1;

  if (pid == p->pid)
    return -1;

  acquire(&wait_lock);

  // Find target and keep target->lock held when found.
  for (struct proc *pp = proc; pp < &proc[NPROC]; pp++)
  {
    acquire(&pp->lock);

    if (pp->pid == pid)
    {
      target = pp;
      break;
    }

    release(&pp->lock);
  }

  if (target == 0)
  {
    release(&wait_lock);
    return -1;
  }

  // Basic error cases.
  if (target->trapframe == 0 ||
      target->state == UNUSED ||
      target->state == ZOMBIE ||
      target->killed)
  {
    release(&target->lock);
    release(&wait_lock);
    return -1;
  }

  // Direct handoff: target is already waiting in co_yield for us.
  // a0 = syscall return value written by the peer
  // a2 = waiting-for PID while parked in co_yield
  if (target->state == SLEEPING &&
      target->chan == &target->context &&
      target->trapframe->a2 == p->pid)
  {
    // The peer is parked in the co_yield sleep state on its context channel,
    // and a2 confirms it is specifically waiting for our pid. We write the
    // value that the peer's co_yield will return, then park the current
    // process and switch directly to the peer. When we resume later, our
    // own return value will already be staged in a0.

    acquire(&p->lock);
    p->trapframe->a0 = -1; // Default error if woken without successful handoff
    p->trapframe->a2 = pid;
    target->trapframe->a0 = value;
    p->chan = &p->context;
    p->state = SLEEPING;
    target->state = RUNNING;
    release(&wait_lock);
    c->proc = target;
    release(&p->lock);

    swtch(&p->context, &target->context);

    c->proc = p;
    p->chan = 0;
    p->trapframe->a2 = 0;

    uint64 ret = p->trapframe->a0;

    release(&p->lock);
    return ret;
  }

  // Target is alive but not already waiting for us.
  // If it is RUNNABLE, give it the CPU directly.
  if (target->state == RUNNABLE)
  {
    acquire(&p->lock);

    // Mark current process as waiting in co_yield for this target.
    p->trapframe->a0 = -1; // Default error if woken without successful handoff
    p->trapframe->a2 = pid;
    p->chan = &p->context;
    p->state = SLEEPING;

    // Run target directly, bypassing the scheduler.
    target->state = RUNNING;

    release(&wait_lock);
    c->proc = target;
    release(&p->lock);

    swtch(&p->context, &target->context);

    // We resume here when another co_yield switches back to us, or when
    // exit()/kill() makes us runnable again with an error return in a0.
    c->proc = p;
    p->chan = 0;
    p->trapframe->a2 = 0;

    uint64 ret = p->trapframe->a0;

    release(&p->lock);
    return ret;
  }

  // Target is not prepared and also not runnable, so we cannot safely switch to it.
  release(&target->lock);
  release(&wait_lock);
  return -1;
}

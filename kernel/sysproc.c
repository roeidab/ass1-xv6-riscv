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

// To add comment about the exit because of the mixing of -1
/*
a0 = what I receive (return value)
a1 = what I offer (my yielded value)
a2 = who I'm waiting for (temporary marker, cleared after resumption)
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

  if(pid <= 0)
    return -1;

  if(pid == p->pid)
    return -1;

  // Save the value this process is offering to the peer.  A process that
  // sleeps below keeps this in a1 until its peer completes the rendezvous.
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
  if(target->trapframe == 0 ||
     target->state == UNUSED ||
     target->state == ZOMBIE ||
     target->killed){
    release(&target->lock);
    release(&wait_lock);
    return -1;
  }

  // Direct handoff: target is already waiting in co_yield for us.
  // a0 = syscall return value (set by peer before switching back)
  // a1 = value offered by peer (set when peer enters co_yield)
  // a2 = waiting-for PID (set when peer sleeps in co_yield)
  if(target->state == SLEEPING &&
     target->chan == &target->context &&
     target->trapframe->a2 == p->pid) {
    // The peer is parked on its direct context channel, waiting for our pid.
    // We set the peer's return value and push current process to wait,
    // then swtch directly to peer. When we resume later, our return value
    // will be in a0 (set by the peer before switching back to us).

    acquire(&p->lock);
    p->trapframe->a0 = -1;  // Default error if woken without successful handoff
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
  // If it is RUNNABLE, give it the CPU directly instead of using sleep/sched.
  if(target->state == RUNNABLE)
  {
    acquire(&p->lock);

    // Mark current process as waiting in co_yield for this target.
    p->trapframe->a0 = -1;  // Default error if woken without successful handoff
    p->trapframe->a2 = pid;
    p->chan = &p->context;
    p->state = SLEEPING;

    // Run target directly, bypassing the scheduler.
    target->state = RUNNING;

    release(&wait_lock);
    c->proc = target;
    release(&p->lock);

    swtch(&p->context, &target->context);

    // We resume here only when some later co_yield switches directly back to us.
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

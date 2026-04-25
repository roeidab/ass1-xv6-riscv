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

  if(target->state == SLEEPING &&
     target->chan == &target->context &&
     target->trapframe->a0 == p->pid) {
    // Direct handoff protocol.  The peer is already parked in sleep(),
    // so it expects to resume with target->lock held.  The caller becomes
    // the next co_yield sleeper, but its lock is released before swtch()
    // so the peer can acquire it when yielding back.  This relies on
    // CPUS=1: while target->lock is held, interrupts remain disabled, so
    // the scheduler cannot observe the caller's transient sleeping state
    // before the direct switch.
    uint64 received = target->trapframe->a1;

    acquire(&p->lock);
    p->trapframe->a0 = pid;
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
    release(&p->lock);
    return received;
  }

  if(target->state == SLEEPING && target->chan == &target->context){
    // The target is a co_yield sleeper, but it is waiting for a different
    // peer.  Without extra per-process state we cannot safely queue multiple
    // contenders for the same coroutine endpoint, so fail instead of mixing
    // values or creating a three-process deadlock.
    release(&target->lock);
    release(&wait_lock);
    return -1;
  }

  release(&target->lock);

  // The target is alive but not ready for a direct handoff.  Park this
  // process as a co_yield sleeper.  A later successful rendezvous must
  // switch directly into this context; exit()/kill() may wake it only to
  // report failure.
  p->trapframe->a0 = pid;
  sleep(&p->context, &wait_lock);

  if(killed(p)){
    release(&wait_lock);
    return -1;
  }

  release(&wait_lock);
  return p->trapframe->a0;
}

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

  argint(0, &pid);
  argint(1, &value);

  if(pid <= 0)
    return -1;

  if(pid == p->pid)
    return -1;

  acquire(&wait_lock);

  // find target
  for(struct proc *pp = proc; pp < &proc[NPROC]; pp++){
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

  if(target->state == UNUSED || target->state == ZOMBIE || target->killed){
    release(&target->lock);
    release(&wait_lock);
    return -1;
  }

  // target ready → exchange
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

  // not ready → sleep
  release(&target->lock);

  sleep(p, &wait_lock);           

  release(&wait_lock);

  return p->trapframe->a0;
}
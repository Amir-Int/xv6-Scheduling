#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;

// (Added by AmirInt)
//   new_proc: determines if a new process has started
//   must call ptable.lock before accessing 
int new_proc;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // (Added by AmirInt) When this thread starts, it must know that it
  // has no child threads.
  p->threads = 0;

  //(added by hadiinz)
  p->priority = 3;
  //when one process is allocated ,the creation time initialized
  p->creation_t = ticks;
  p->sleeping_t = 0;
  p->runnable_t = 0;
  p->running_t = 0;

  p->full_time_runner = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->rrr = QUANTUM;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");


  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  // (Added by AmirInt) Locking the page table to prevent those
  // naughty threads from causing trouble.
  acquire(&ptable.lock);

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0) {
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0) {
      release(&ptable.lock);
      return -1;
    }
  }

  curproc->sz = sz;
  switchuvm(curproc);

  release(&ptable.lock);

  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  np->state = RUNNABLE;
  new_proc = 1;
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  //(added by hadiinz) hen the proces exit 
  curproc->termination_t = ticks;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      
      if (p->pid == curproc->pid)
        continue;

      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int getWaitingTime()
{
  return myproc()->sleeping_t + myproc()->runnable_t;
}

int getTurnAroundTime()
{
  return myproc()->sleeping_t + myproc()->runnable_t + myproc()->running_t;
}

int getCBT()
{
  return myproc()->running_t;
}

//(added by hadiinz) update time in each process in every states
void updateProcTimes()
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    switch (p->state)
    {
      case RUNNING:
        p->running_t++;
        break;
      case RUNNABLE:
        p->runnable_t++;
        break;
      case SLEEPING:
        p->sleeping_t++;
        break;
      default:
        break;
    }
  }
}

// (added by hadiinz)
//   0 -> DEFAULT
//   1 -> ROUND_ROBIN
//   2 -> PRIORITY
//   3 -> MULTILAYERED_PRIORITY
int 
changePolicy(int newPolicy)
{
  if (0 <= newPolicy && newPolicy < 4)
  {
    policy = newPolicy;
    return 0;
  }
  //failed
  else
    return -1;
}

//(Added by hadiinz) set priority for priority scheduling
int
setPriority(int priority)
{
  if (policy == DYNAMIC_MLP)
    return 0;
  
  struct proc *p = myproc();
  
  if (1 <= priority && priority <= 6)
  {
    p->priority = priority;
  }
  else 
    p->priority = 5;
  return 1;
}

// (Added by AmirInt) context-switches the given cpu onto the given process
void switch_context(struct cpu *c, struct proc *p) {
  // Switch to chosen process.  It is the process's job
  // to release ptable.lock and then reacquire it
  // before jumping back to us.
  c->proc = p;
  switchuvm(p);
  p->state = RUNNING;

  swtch(&(c->scheduler), p->context);
  switchkvm();
        
  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;
}

// (Added by AmirInt) Default Scheduler
void def_scheduler(struct cpu *c, struct proc *p) {
  // Loop over process table looking for process to run.
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE)
      continue;
    switch_context(c, p);
  }
  release(&ptable.lock);
}

// (Added by AmirInt) Round-Robin-schedules the CPU
void rr_scheduler(struct cpu *c, struct proc **p) {

  acquire(&ptable.lock);
  if ((*p)->state != RUNNING) {
    struct proc *pr = *p;
    int found = 0;
    // Loop over process table looking for process to run.
    for(++(*p); *p < &ptable.proc[NPROC]; ++(*p)){
      if((*p)->state != RUNNABLE)
        continue;
      found = 1;
      switch_context(c, *p);
      break;
    }
    if (found == 0)
      for(*p = ptable.proc; *p <= pr; ++(*p)){
        if((*p)->state != RUNNABLE)
          continue;
        found = 1;
        switch_context(c, *p);
        break;
      }
  }
  
  release(&ptable.lock);
}

//added by Hadiinz (priority scheduling)
void
priority_scheduler(struct cpu *c, struct proc *p)
{
  struct proc *p_high = 0;
  int hasRunnable = 0;
  acquire(&ptable.lock);
  //choose the first ready process
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->state == RUNNABLE)
    {
      p_high = p;
      hasRunnable = 1;
      break;
    }
  }
  if (hasRunnable == 1)
  {
    //find the first process having high priority
    for(; p < &ptable.proc[NPROC]; p++){
      if (p->state != RUNNABLE)
        continue;
      
      if (p->priority < p_high->priority)
      {
        p_high = p;

      }
    }
    c->proc = p_high;
    switchuvm(p_high);
    p_high->state = RUNNING;

    swtch(&(c->scheduler), p_high->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;

  }
  release(&ptable.lock);
  
}

// (Added by AmirInt) Multilayered-Priority-schedules the CPU
void mlp_scheduler(struct cpu *c, struct proc **p) {
  
  acquire(&ptable.lock);
  if ((*p)->state != RUNNING || new_proc == 1) {

    struct proc *pr = *p;
    int found = 0;

    // Loop over process table looking for process to run.
    if (new_proc == 1 && (*p)->priority > 1) {
      for(pr = ptable.proc; pr < &ptable.proc[NPROC]; ++pr)
        if(pr->state == RUNNABLE && pr->priority < (*p)->priority) {
          *p = pr;
          switch_context(c, *p);
        }
    }
    else if (new_proc == 0) {
      for(++(*p); *p < &ptable.proc[NPROC]; ++(*p)){
        if((*p)->state != RUNNABLE || (*p)->priority > pr->priority)
          continue;
        found = 1;
        switch_context(c, *p);
        break;
      }
      if (found == 0)
        for(*p = ptable.proc; *p <= pr; ++(*p)){
          if((*p)->state != RUNNABLE || (*p)->priority > pr->priority)
            continue;
          found = 1;
          switch_context(c, *p);
          break;
        }
    }
    // if (found == 1 && (*p)->pid > 2) {
    //   cprintf("to: %d (%d, %d)\n\n", c->apicid, (*p)->pid, (*p)->priority);

    // }
    new_proc = 0;
  }
  release(&ptable.lock);
}

// (Added by AmirInt) Multilayered-Priority-schedules the CPU
void dmlp_scheduler(struct cpu *c, struct proc **p) {
  
  acquire(&ptable.lock);
  if ((*p)->state != RUNNING || new_proc == 1) {

    struct proc *pr = *p;
    int found = 0;

    if ((*p)->full_time_runner == 1 && (*p)->priority < 6)
      ++(*p)->priority;

    // Loop over process table looking for process to run.
    if (new_proc == 1 && (*p)->priority > 1) {
      for(pr = ptable.proc; pr < &ptable.proc[NPROC]; ++pr)
        if(pr->state == RUNNABLE && pr->priority < (*p)->priority) {
          *p = pr;
          switch_context(c, *p);
        }
    }
    else if (new_proc == 0) {
      for(++(*p); *p < &ptable.proc[NPROC]; ++(*p)){
        if((*p)->state != RUNNABLE || (*p)->priority > pr->priority)
          continue;
        found = 1;
        switch_context(c, *p);
        break;
      }
      if (found == 0)
        for(*p = ptable.proc; *p <= pr; ++(*p)){
          if((*p)->state != RUNNABLE || (*p)->priority > pr->priority)
            continue;
          found = 1;
          switch_context(c, *p);
          break;
        }
    }
    if (found == 1 && (*p)->pid > 2) {
      cprintf("to: %d (%d, %d)\n\n", c->apicid, (*p)->pid, (*p)->priority);

    }
    new_proc = 0;
  }
  release(&ptable.lock);
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  p = ptable.proc;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    switch (policy) {
      case DEFAULT:
        def_scheduler(c, p);
        break;
      case ROUND_ROBIN:
        rr_scheduler(c, &p);
        break;
      case PRIORITY:
        priority_scheduler(c, p);
        break;
      case MULTILAYERED_PRIORITY:
        mlp_scheduler(c, &p);
        break;
      case DYNAMIC_MLP:
        dmlp_scheduler(c, &p);
        break;
    }
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->full_time_runner = 1;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {
      if (policy == DYNAMIC_MLP)
        p->priority = 1;
      p->state = RUNNABLE;
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// (Added by AmirInt) returns the number of active processes
int getProcCount(void) {
  int proc_counter = 0;
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->state != UNUSED)
      ++proc_counter;
  }
  return proc_counter;
}

// (Added by AmirInt) returns the number of read attempts whence
// the kernel boots
int getReadCount(void) {
  
  extern int read_count;

  return read_count;
}

// (Added by AmirInt) creates a thread and returns the thread ID
int thread_create(void* stack) {
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // The parent process here has one more thread.
  ++(curproc->threads);
  // Initialising The stack_top of the new thread.
  np->stack_top = (int)((char*) stack + PGSIZE);

  // Locking so that while the new thread is picking
  // parent's page directory, the parent cannot change it.
  acquire(&ptable.lock);
  np->pgdir = curproc->pgdir;
  np->sz = curproc->sz;
  release(&ptable.lock);

  // Copying the content of the parent stack into the thread's.
  int parent_occupied_stack = curproc->stack_top - curproc->tf->esp;
  np->tf->esp = np->stack_top - parent_occupied_stack; 
  memmove((void*) np->tf->esp, (void*) curproc->tf->esp, parent_occupied_stack);

  np->parent = curproc;
  *np->tf = *curproc->tf;

  np->tf->eax = 0;

  np->tf->esp = np->stack_top - parent_occupied_stack;
  np->tf->ebp = np->stack_top - (curproc->stack_top - curproc->tf->ebp);


  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

// The new thread must inherit the parent's process id.
  pid = curproc->pid;
  np->pid = pid;

  acquire(&ptable.lock);
  np->state = RUNNABLE;
  new_proc = 1;
  release(&ptable.lock);

  return pid;
}

// (Added by AmirInt) makes the calling process to wait for its threads
int thread_wait(void) {
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  // The parent must wait only if it has at least a child thread.
  if (curproc->threads > 0) {
    acquire(&ptable.lock);
    for(;;){
      // Scan through table looking for exited children.
      havekids = 0;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->parent != curproc)
          continue;
        
        if (p->pid == curproc->pid) {
          havekids = 1;
          if(p->state == ZOMBIE){
            // Found one.
            pid = p->pid;
            kfree(p->kstack);
            p->kstack = 0;
            p->pid = 0;
            p->parent = 0;
            p->name[0] = 0;
            p->killed = 0;
            p->state = UNUSED;
            release(&ptable.lock);
            return pid;
          }
        }
      }

      // No point waiting if we don't have any children.
      if(!havekids || curproc->killed){
        release(&ptable.lock);
        return -1;
      }

      // Wait for children to exit.  (See wakeup1 call in proc_exit.)
      sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    }
  }
  return -1;
}
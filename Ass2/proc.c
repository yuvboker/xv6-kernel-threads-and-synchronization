#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "proc.h"
#include "tournament_tree.h"



struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;


struct {
  struct spinlock mlock;
  struct kthread_mutex_t mutex[MAX_MUTEXES];
} mtable;


static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
int nextmid = 1;
int helper;

extern void forkret(void);
extern void trapret(void);
static void wakeup1(void *chan);
int kthread_join2(struct thread* t);
void safe_exit(struct proc *curproc, struct thread *curthread);
void wakeup2(struct proc *curproc, void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&mtable.mlock, "mtable");
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


// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct thread*
mythread(void) {
  struct cpu *c;
  struct thread *t;
  pushcli();
  c = mycpu();
  t = c->thread;
  popcli();
  return t;
}

static struct thread*
allocthread(void)
{
  struct proc *p;
  struct thread *t;
  char *sp;

  for(p = &ptable.proc[helper]; p< &ptable.proc[NPROC];p++)
    if(p->state == PUNUSED)
      goto found;
  for(p = ptable.proc; p< &ptable.proc[NPROC];p++)
    if(p->state == PUNUSED)
      goto found;
  return 0;

  found:
  	if(helper == NPROC - 1)
  		helper = 0;
  	else
  		helper++;
    p->pid = nextpid++;
    p->state = PEMBRYO;
    initlock(&p->lock, "plock");
    t = &(p->threads[0]);
    t->state = TEMBRYO;
    t->tid = nexttid++;
    t->parent = p;

  // Allocate kernel stack.
  if((t->kstack = kalloc()) == 0){
    p->state = PUNUSED;
    t->state = TUNUSED;
    return 0;
  }

  sp = t->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;
  return t;

}


//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  struct thread *t;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  acquire(&ptable.lock);
  t = allocthread();
  p = t->parent;
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(t->tf, 0, sizeof(*t->tf));
  t->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  t->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  t->tf->es = t->tf->ds;
  t->tf->ss = t->tf->ds;
  t->tf->eflags = FL_IF;
  t->tf->esp = PGSIZE;
  t->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.

  // the state of proc changed to "USED" during the call to "allocproc".
  p->state = PUSED;
  t->state = TRUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();
  acquire(&curproc->lock);
  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0) {
      release(&curproc->lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0) {
      release(&curproc->lock);
      return -1;
    }
  }
  curproc->sz = sz;
  switchuvm(curproc,curthread);
  release(&curproc->lock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *p;
  struct thread *t;
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();

  acquire(&ptable.lock);

  // Allocate process.
  if((t = allocthread()) == 0){
    release(&ptable.lock);
    return -1;
  }
  p = t->parent;
  // Copy process state from proc.
  if((p->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(t->kstack);
    t->kstack = 0;
    t->state = TUNUSED;
    p->state = PUNUSED;
    release(&ptable.lock);
    return -1;
  }

  p->sz = curproc->sz;
  p->parent = curproc;
  *t->tf = *curthread->tf;

  // Clear %eax so that fork returns 0 in the child.
  t->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      p->ofile[i] = filedup(curproc->ofile[i]);
  p->cwd = idup(curproc->cwd);

  safestrcpy(p->name, curproc->name, sizeof(curproc->name));

  pid = p->pid;

  p->state = PUSED;
  t->state = TRUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
// The process asks all its threads to kill themselves.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();

  if(curproc == initproc)
  panic("init exiting");
  //kill all threads except current thread
  kill_other_threads(curproc,curthread);
  // Close all open files.
  safe_exit(curproc,curthread);
}

void
safe_exit(struct proc *curproc, struct thread *curthread)
{
  int fd;
  struct proc *p;
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
      if(p->state == PZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = PZOMBIE;
  curthread->state = TUNUSED;
  sched();
  panic("zombie exit");
}
void
remove_thread(struct thread * t)
{
  if(t->kstack)
    kfree(t->kstack);
  if(t->mutex)
    kthread_mutex_dealloc(t->mutex->mid);
  t->kstack = 0;
  t->tid = 0;
  t->state = TUNUSED;
  t->parent = 0;
  t->killed = 0;

}

void
kill_other_threads(struct proc* curproc,struct thread *curthread){

  struct thread *t;
  acquire(&ptable.lock);

  if(curthread->killed == 1)
  {
    wakeup1(curthread);
    curthread->state = TZOMBIE;
    sched();
  }
  int found;
  for(;;){
    found = 0;
    for(t=curproc->threads; t< &curproc->threads[NTHREAD];t++){
       if(t->tid!=curthread->tid && t->state!= TUNUSED){
          found = 1;
          t->killed = 1;
          if(t->state == TSLEEPING || t->state == TZOMBIE){
            remove_thread(t);
          }
      }
    }
    if(!found)
      break;
    else{
      curthread->state = TRUNNABLE;
      sched();
    }
  }
  release(&ptable.lock);
}
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  struct thread *t;
  
  acquire(&ptable.lock);
  for(;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == PZOMBIE) {
        // Found one.
        for (t = p->threads; t < &p->threads[NTHREAD]; t++){
            remove_thread(t);
        }
      pid = p->pid;
      freevm(p->pgdir);
      p->pid = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->state = PUNUSED;
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
  struct thread *t;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state != PUSED){
        continue;
      }

      c->proc = p;
      for (t = p->threads; t < &p->threads[NTHREAD]; t++) {
        if(t->state == TSLEEPING && t->killed){
          t->state = TRUNNABLE;
        }
        if (t->state != TRUNNABLE){
          continue;
        }

        // Switch to chosen process and look for a runnable thread.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        //cprintf("got to runnalbe thread");
        c->thread = t;
        t->state = TRUNNING;
        switchuvm(c->proc,c->thread); // tell CPU to use this process's page table
        swtch(&c->scheduler, t->context); // context switch to the thread's kernel thread
        switchkvm();
        c->thread = 0;
        if(p->state != PUSED)
          break;
      }
      c->thread = 0;
      c->proc = 0;
     
    }
    release(&ptable.lock);

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
  struct thread *curthread = mythread();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(curthread->state == TRUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&curthread->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  mythread()->state = TRUNNABLE;
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
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();
  
  if(curproc == 0)
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
  // we need to acquire the lock of the current process because we change the state of the thread.
  curthread->chan = chan;
  curthread->state = TSLEEPING;
  sched();

  // Tidy up.
  curthread->chan = 0;

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
  struct thread *t;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    for (t = p->threads; t < &p->threads[NTHREAD]; t++) {
      if (t->state == TSLEEPING && t->chan == chan){
        t->state = TRUNNABLE;
      }
    }
  }
}
void
wakeup2(struct proc *curproc, void *chan)
{
  struct thread *t;
  for (t = curproc->threads; t < &curproc->threads[NTHREAD]; t++) {
    if (t->state == TSLEEPING && t->chan == chan){
      t->state = TRUNNABLE;
    }
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
    [PUNUSED] "punused",
    [PUSED] "pused",
    [PZOMBIE] "pzombie"
  };
  int i;
  struct proc *p;
  struct thread *t;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == PUNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    for(t = p->threads ; t < &p->threads[NTHREAD]; t++){
      if(t->state == TSLEEPING) {
        getcallerpcs((uint *) t->context->ebp + 2, pc);
        for (i = 0; i < 10 && pc[i] != 0; i++)
          cprintf(" %p", pc[i]);
        cprintf("\n");
      }
    }

  }
}

int
kthread_create (void (*start_func)(), void *stack){
  struct thread *t;
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();
  char *sp;

  acquire(&ptable.lock);
  for(t = curproc->threads; t< &curproc->threads[NTHREAD]; t++)
      if(t->state == TUNUSED)
        goto found;
  release(&ptable.lock);
  return -1;

found:
  
  t->state = TEMBRYO;
  t->tid = nexttid++;
  t->parent = curproc;

  if((t->kstack = kalloc()) == 0){
    t->state = TUNUSED;
    release(&ptable.lock);
    return -1;
  }

  sp = t->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;

  *t->tf = *curthread->tf;
  t->tf->eip =(uint)start_func;
  t->tf->esp =(uint)(stack + MAX_STACK_SIZE);
  t->state = TRUNNABLE;
  release(&ptable.lock);
  return t->tid;

}

int
kthread_id(){
  struct thread *curthread = mythread();
  if(myproc() && curthread)
    return curthread->tid;
  return -1;
}

void
kthread_exit() {
  struct thread *t;
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();
  int found = 0;

  acquire(&ptable.lock);

  for(t = curproc->threads; t < &curproc->threads[NTHREAD]; t++){
    if(t != curthread && t->state != TUNUSED && t->state != TZOMBIE){
        found = 1; // we found more threads of that process, thus we shouldn't exit the process
        break;
      }
  }
  if(!found){
    release(&ptable.lock);
    exit();
  }
  curthread->state = TZOMBIE;
  wakeup2(curproc,curthread);
  sched();
}

int
kthread_join2(struct thread* t){
  //int tid;

  acquire(&ptable.lock);

  for(;;){
    if(t->state != TZOMBIE && t->state != TUNUSED){
        sleep(t,&ptable.lock);
        //
      }
    else{
      break;
    }
  }

  if(t->state == TZOMBIE){
    //we found the thread we've been waiting for
    release(&ptable.lock);
    return 0;
  }
  release(&ptable.lock);
  return -1;
}

int
kthread_join(int thread_id){
  struct thread *t;
  //struct thread *curthread = mythread();
  struct proc *curproc = myproc();
  //int tid;

  acquire(&ptable.lock);
  for(t = curproc->threads; t < &curproc->threads[NTHREAD]; t++){
    if(t->tid == thread_id){
      break; // we found more threads of that process, thus we shouldn't exit the process
    }
  }
  for(;;){
    if(t->tid == thread_id && t->state != TZOMBIE && t->state != TUNUSED){
        sleep(t,&ptable.lock);
        //
      }
    else{
      break;
    }
  }

  if(t->state == TZOMBIE){
    //we found the thread we've been waiting for
    remove_thread(t);
    release(&ptable.lock);
    return 0;
  }
  release(&ptable.lock);
  return -1;
}

int
kthread_mutex_alloc(){
 
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();
  acquire(&mtable.mlock);
  struct kthread_mutex_t *m;
  for(m = mtable.mutex ; m < &mtable.mutex[MAX_MUTEXES];m++){
    if(m->mid == 0) {
      m->mid = nextmid++;
      m->mythread = 0;
      m->myproc = curproc;
      curthread->mutex = m;
      m->state = MUNLOCKED;
      release(&mtable.mlock);
      return m->mid;
    }
  }
  release(&mtable.mlock);
  return -1;
}
int
kthread_mutex_dealloc(int mutex_id){
  struct kthread_mutex_t *m;
  struct thread *curthread = mythread();
  acquire(&mtable.mlock);

  for(m = mtable.mutex ; m < &mtable.mutex[MAX_MUTEXES];m++){
    if(m->mid == mutex_id) {
      if(m->state == MLOCKED){
         release(&mtable.mlock);
         return -1;
      }
      m->mid = 0;
      m->mythread=0;
      m->state = MUNLOCKED;
      curthread->mutex = 0;
      release(&mtable.mlock);
      return 0;
    }

  }
  release(&mtable.mlock);
  return -1;
}

int
kthread_mutex_lock(int mutex_id){
  struct thread *curthread = mythread();
  //struct proc *curproc = myproc();
  struct kthread_mutex_t *m;
  acquire(&mtable.mlock);

  for(m = mtable.mutex ; m < &mtable.mutex[MAX_MUTEXES];m++) {
    if (m->mid != mutex_id)
      continue;
    if(m->state == MLOCKED && m->mythread == curthread){
      release(&mtable.mlock);
      return -1;
    }
    while(xchg(&m->state, MLOCKED) != 0){
      sleep(m, &mtable.mlock);
    }
    m->mythread = curthread;
    release(&mtable.mlock);
    return 0;
  }
  release(&mtable.mlock);
  return -1;

}

int
kthread_mutex_unlock(int mutex_id){
  struct kthread_mutex_t *m;
  acquire(&mtable.mlock);

  for(m = mtable.mutex ; m < &mtable.mutex[MAX_MUTEXES];m++){
    if(m->mid != mutex_id)
       continue;
    if(m->state == MUNLOCKED || m->mythread->tid != mythread()->tid){
      release(&mtable.mlock);
      return -1;
    }

    m->mythread=0;
    m->state = MUNLOCKED;
    release(&mtable.mlock);
    wakeup(m);
    return 0;
  }
  release(&mtable.mlock);
  return -1;
}


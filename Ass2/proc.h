#include "kthread.h"

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
  struct thread *thread;       // The thread running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { PUNUSED, PEMBRYO, PUSED , PZOMBIE};
enum threadstate {TUNUSED, TEMBRYO, TSLEEPING, TRUNNABLE, TRUNNING, TZOMBIE, TINVALID};
enum mutexstate {MUNLOCKED,MLOCKED};

struct thread {
    int tid;                        // Thread ID - to identify the tread(in join for example)
    enum threadstate state;         // Thread state(to know if it can be used for example)
    char *kstack;                   // Bottom of kernel stack for this thread(each thread needs to have its own CS)
    struct proc *parent;            // Process of the thread(for printing purposes)
    struct trapframe *tf;           // Trap frame for thread system calls
    struct context *context;        // For switching between threads
    void* chan;                     // non-zero = the channel the thread sleeps on
    int killed;                     // non-zero = killed (when we exit a proccess we should kill each thread)
    struct kthread_mutex_t *mutex;
};

// Per-process state
struct proc {
  uint sz;                          // Size of process memory (bytes)
  pde_t* pgdir;                     // Page table
  enum procstate state;             // Process state
  int pid;                          // Process ID
  struct proc *parent;              // Parent process
  int killed;                       // If non-zero, have been killed
  struct file *ofile[NOFILE];       // Open files
  struct inode *cwd;                // Current directory
  char name[16];                    // Process name (debugging)
  struct spinlock lock;
  struct thread threads[NTHREAD];   // Array of threads
  int num_of_threads;
  int invalid;
};

struct kthread_mutex_t {
    struct thread *mythread;
    struct proc *myproc;
    int mid;
    enum mutexstate state;
};
// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

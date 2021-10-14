# xv6

## sleep

content: Implement the UNIX program `sleep` for xv6.

​	system call会传入2个及以上的参数，第一个参数默认是system call的名称，第二个及之后的参数才是system call需要的参数，如sleep 10，10会作为参数传递给sleep system call。main函数中argc表示参数的数量，argv[]中存储参数。

​	每一个指令底层都是使用system call。sysproc.c提供了一系列的system call。

​	用户态的参数通过寄存器传入内核态，可以通过`argint()`, `argstr()`, `argaddr()`等函数获取。

## Pingpong

content: Write a program that uses UNIX system calls to ''ping-pong'' a byte between two processes over a pair of pipes, one for each direction.

background:

（1）`pipe(p) system call`会创建一个管道，返回管道的输入输出到`p[2]`，`p[0]`是输入，`p[1]`是输出。

（2）`write(fd, buf, sizeof(buf)) system call`会将`buf`中的`sizeof(buf)`位写入到`fd`中，`fd`表示标准输出，可以是管道，也可以是其他的输出。

（3）`read(fd, buf, sizeof(buf)) system call`则是从`fd`中读取最多`sizeof(buf)`个字符到`buf`中，`fd`表示标准输入。可以通过`pipe`, `read`, `write`完成任意两个进程间的通讯。

（4）`fork()`会创建子进程，对于父进程返回进程的pid，对于子进程，返回0。但是如果子进程再创建子进程，则`getpid()`返回的结果是一样的，即不会再分配新的pid，不知道为什么这样设计。

hint:

（1）Use `pipe` to create a pipe.

（2）Use `fork` to create a child.

（3）Use `read` to read from the pipe, and `write` to write to the pipe.

（4）Use `getpid` to find the process ID of the calling process

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
pingpong()
{
    char receive[512];
    char send1[512] = {"00000000"};
    char send2[512] = {"10000000"};
    char send3[512] = {"11000000"};
    int p1[2], p2[2], p3[2];
    pipe(p1);
    pipe(p2);
    pipe(p3);
    printf("%d \n", getpid());
    write(p1[1], send1, sizeof(send1));
    int pid = fork();
    if(pid == 0){
        int n;
        if((n = read(p1[0], receive, sizeof(receive))) < 0){
            printf("child: read error\n");
        }else{
            printf("<%d>:received ping-%s \n", getpid(), receive);
            write(p2[1], send2, sizeof(send2));

            fork(); //child's son
            if((n = read(p2[0], receive, sizeof(receive))) < 0){
                printf("child's child: read error\n");
            }else{ //getpid() still is 4
                printf("<%d>:received pingpong-%s \n", getpid(), receive);
                write(p3[1], send3, sizeof(send3));
            }

        }
    }else{
        int m;
        wait(0);
        if((m = read(p3[0], receive, sizeof(receive))) < 0){
            printf("parent: read error\n");
        }else{
            printf("<%d>:received pong-%s \n", getpid(), receive);  
        }
    }
}

int
main(int argc, char *argv[])
{
  pingpong();
  exit(0);
}
```

（5）Add the program to UPROGS in Makefile.

```c
UPROGS=\
	...
	$U/_pingpong\
	...
```

（6）结果

```
$ pingpong
4 
<5>:received ping-00000000 
<5>:received pingpong-10000000 
<4>:received pong-11000000
```



## Primes

content: use `pipe` and `fork` to set up the pipeline.

（1）开始我设想的是由父进程不断的产生自然数，产生一个素数就创建一个子进程，子进程又创建一个子进程，将下一个素数通过`pipe()`传过去，这样不断的传，直到35，但是`fork()`对于子进程返回的pid是0，不知道怎么遍历所有的子进程。

（2）然后看网上的解法是一次产生所有的数，用两个数组作为pipe的读写缓冲。
需要注意的点是`write()`, `read()`的缓冲区都是char * 指针，如果传输的是非char类型，需要强制类型转化。如：

```c
int p[512];
int temp = p[i];
write(p[1], (char * )(&temp), sizeof(char * ));

char buf[4];
read(p[0], buf, sizeof(char * ));
int p[i] == * (int * )buf;
```

（3）还有就是`write()`之后要用`close()`将`pipe()`关闭，不然之后的read可能会一直等待结束。
（4）每个进程结束要用exit退出，不然会出现莫名其妙的bug。
（5）`wait()`是等待子进程结束，试过如果不用`wait()`会出现zombie，暂时不知道为什么。



## Find

content: find all the files in a directory tree with a specific name.

​	这个实验走了弯路，正确的思路应该是用fstat获取文件状态后判断这个文件是文件还是文件夹，如果是文件则比较，然后输出，如果是文件夹则遍历其中的所有文件，然后递归。但要注意对于.和..文件不要遍历。

​	我之前是先遍历path中的文件。判断是否是文件夹，如果是文件夹在递归调用。但是没有及时的用`fstat()`获取文件的状态，导致一直用当前文件夹的状态去判断，从而一直是T_DIR，不能对T_FILE进行判断。



## System call tracing

content: add a system call tracing feature.

这个实验的思路比较简单，在`syscall.c`中添加printf指令即可。难点主要有以下几点：

（1）`mask`怎样传递。开始我以为要通过外部显式传递，但应该像sleep一样，在proc结构体中增加一个参数mask，然后通过`argint()`获取当前process的`mask`，关于这个函数的实现还要研究一下，很重要。函数的参数都是通过寄存器传递的，如果参数多于6个，通过栈传递。

（2）trace的参数开始理解错了，以为直接是syscall的id，但应该是通过移位来判断对应的syscall是否打印。

然后关于进程执行，即proc结构体要仔细研究一下。

hint:

（1）Add `$U/_trace` to UPROGS in Makefile

```c
UPROGS=\
	...
	$U/_trace\
	...
```

（2）add a prototype for the system call to `user/user.h`, a stub to `user/usys.pl`, and a syscall number to `kernel/syscall.h`.

（3）在proc结构体中添加`mask`变量，用来记录是否打印该trace；

```c
// Per-process state
struct proc {
  struct spinlock lock;
  ...
  int mask[23];                // mask for trace
};
```

（4）Add a `sys_trace()` function in `kernel/sysproc.c`

```c
uint64 sys_trace(void){
  struct proc * p = myproc();
  int n, num = 0;
  
  if(argint(0, &n) < 0)
    return -1;

  while (n > 0){
    if(n % 2)
      p->mask[num++] = 1;
    else
      p->mask[num++] = 0;
    n >>= 1;
  }
  return 0;
}
```

（5）Modify `fork()`

```c
int
fork(void)
{
  ...

  // mask
  for(int i = 0; i < 23; i++)
    np->mask[i] = p->mask[i];

   ...
}
```

（6）print the trace output.

```c
char * syscall_name[] = {"", "fork", "exit", "wait", "pipe",
    "read", "kill", "exec", "fstat", "chdir", "dup", "getpid",
    "sbrk", "sleep", "uptime", "open", "write", "mknod", "unlink",
    "link", "mkdir", "close", "trace"};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
    if(p->mask[num] == 1){
      printf("%d: syscall %s -> %d\n", p->pid, syscall_name[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

（7）结果

```
$ trace 2 usertests forkforkfork
usertests starting
3: syscall fork -> 4
test forkforkfork: 3: syscall fork -> 5
5: syscall fork -> 6
6: syscall fork -> 7
7: syscall fork -> 8
6: syscall fork -> 9
7: syscall fork -> 10
...
ALL TESTS PASSED
$ grep hello README
$ trace 32 grep hello README
132: syscall read -> 1023
132: syscall read -> 966
132: syscall read -> 70
132: syscall read -> 0
```



## System info

content: add a system call, `sysinfo`, that collects information about the running system.

hint:

（1）Add `$U/_sysinfotest` to UPROGS in Makefile;

```c
UPROGS=\
	...
	$U/_sysinfotest\
	...
```

（2）sysinfo needs to copy a `struct sysinfo` back to user space;

```c
uint64 sys_sysinfo(void){
  uint64 addr;
  struct sysinfo s;
  struct proc * p = myproc();
  s.freemem = freemen();
  s.nproc = procnum();

  if(argaddr(0, &addr) < 0)
    return -1;

  if(copyout(p->pagetable, addr, (char *)&s, sizeof(s)) < 0)
      return -1;
    return 0;
}
```

（3）To collect the amount of free memory, add a function to `kernel/kalloc.c`;

```c
// kalloc.c
...
uint64          freemen(void);
```

```c
uint64 freemen(void){
  struct run * r;
  int num = 0;

  for(r = kmem.freelist; r; r = r -> next){
    num++;
  }

  return num * PGSIZE;
}
```

（4）To collect the number of processes, add a function to `kernel/proc.c;`

```c
// proc.c
...
uint64          procnum(void);
```

```c
uint64 procnum(void){
  int num = 0;
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++){
    if(p->state != UNUSED)
      num++;
  }

  return num;
}
```

（5）结果

```
$ sysinfotest
sysinfotest: start
sysinfotest: OK
```

遇到的问题：

​	需要详细了解的函数：`mycpu()`, `myproc()`, `argaddr()`, `copyout()`。同时，没有搞懂用户空间user.h中定义的system calls怎么传递到kernel中，调用kernel中的syscall。

​	sysproc.c和sysfile.c中定义所有的syscall，然后proc.c和file.c中定义了syscall的实现。

（1）在sysproc.c中定义的sys_sysinfo()函数怎样直接修改sysinfo结构体的值。很简单，就是正常的函数结构体，直接定义就好。

（2）mycpu()：通过调用`cpuid()`获取当前cpuid，而`cpuid()`中通过读取tp(thread pointer)寄存器得到core number（tp寄存器怎么处理有待下一步探究)。而cpus结构体变量中保存了所有的cpu上下文。

（3）`myproc()`是获取当前process的状态，即运行在当前cpu的process。

（4）`argaddr()`是获取对应寄存器的值。（当程序要从user写入数据到kernel中，获取到的寄存器就是保存地址指针？）

（5）`copyout()`: copy from kernel to user, copy len bytes from src to virtual address dstva in a given page table.



## Print a page table

content: write a function that prints the contents of a page table.

hint:

（1）Insert `if(p->pid==1) vmprint(p->pagetable)` in exec.c just before the `return argc`, to print the first process's page table.

```c
  if (p->pid ==1 ) {
    vmprint(p->pagetable);
  }
```

（2）implementing `vmprint()`;

```c
void vmprint(pagetable_t pagetable){
  printf("page table %p\n", pagetable);
  reprint(pagetable, 0); // pagetable of this process
}

void reprint(pagetable_t pagetable, int level){
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && level == 0) { // 0th pagetable
      printf("..%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      reprint((pagetable_t)child, 1);
    } else if ((pte & PTE_V) && level == 1) { // 1th pagetable
      printf(".. ..%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      reprint((pagetable_t)child, 2);
    } else { // 2th(last) pagetable
      if ((pte & PTE_V)) {
        printf(".. .. ..%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
      }
    }
  }
}
```

（3）结果

```
hart 2 starting
hart 1 starting
page table 0x0000000087f6e000
..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
.. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
.. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
.. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
.. .. ..510: pte 0x0000000021fdd807 pa 0x0000000087f76000
.. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
init: starting sh
```

​	开始想的是直接递归调用`vmprint()`，但是不知道level和pte index怎么解决，后来才知道要另外声明一个函数`tbprint(pagetable_t pagetable, int level)`，在`vmprint()`里调用，传入level，同时遍历所有的pte，index也就出来了，而不是我之前想的用`PXMASK`，`PXSHIFT`，`PX`宏来计算得到。



## A kernel page table per process

content: modify the kernel so that every process uses its own copy of the kernel page table when executing in the kernel.

​	“every process uses its own copy of the kernel page table when executing in the kernel.”这句话什么意思？每个进程在kernel时使用自己的kernel page table。这个kernel page table要怎么构造，进程怎么使用它。在这个实验中，仅仅是初始化和释放了`kpagetable`，要在下一个实验才会使用`kpagetable`。

（1）首先在`proc`结构体中声明一个`kpagetable`变量；

```c
// Per-process state
struct proc {
  ...
  char name[16];               // Process name (debugging)
  pagetable_t kpagetable;      // kernel page table
};
```

（2）然后初始化这个`kpagetable`，初始化的过程和kernel page table的过程一样；同时要重新设计一个函数`ukvmmap()`，用来将`kpagetable`的各个页映射到物理空间，这个函数和原来的`kvmmap()`类似；

```c
// add a mapping to the user kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
ukvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(pagetable, va, sz, pa, perm) != 0)
    panic("ukvmmap");
}
```

```c
/*
 * create a direct-map page table for the user kernel page table.
 */
pagetable_t
ukvminit()
{
  pagetable_t ukpagetable = (pagetable_t) kalloc();
  memset(ukpagetable, 0, PGSIZE);

  // uart registers
  ukvmmap(ukpagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  ukvmmap(ukpagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT, why don't need local interrupt controller?
  ukvmmap(ukpagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  ukvmmap(ukpagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  ukvmmap(ukpagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  ukvmmap(ukpagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  ukvmmap(ukpagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return ukpagetable;
}
```

（3）在`allocproc()`中初始化这个`kpagetable`，和初始化原有的`pagetable`一样；

```c
static struct proc*
allocproc(void)
{
  ...

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user kernel page table.
  p->kpagetable = ukvminit();
  if(p->kpagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // process's kernel page table has a mapping
  // for that process's kernel stack
  char *pa = kalloc();
  if(pa == 0)
      panic("kalloc");
  uint64 va = KSTACK((int) (p - proc));
  ukvmmap(p->kpagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  p->kstack = va;

  ...
}
```

（4）Make sure that each process's kernel page table has a mapping for that process's kernel stack.这个过程不理解，或者说不理解这个kstack是干什么用的。

​	***kstack***: When the process enters the kernel (for a system call or interrupt), the kernel code executes on the process’s kernel stack. 即process在kernel中执行时，kernel code执行在kernel stack中。

​	那么这个操作就是为`kstack`分配物理空间，然后映射，将`va`保存到`p->stack`中。

```c
// process's kernel page table has a mapping
  // for that process's kernel stack
  char *pa = kalloc();
  if(pa == 0)
      panic("kalloc");
  uint64 va = KSTACK((int) (p - proc));
  ukvmmap(p->kpagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  p->kstack = va;
```

（5）在切换process前将`kpagetable`保存到`stap`寄存器中。

```c
void
scheduler(void)
{
  ...
    
    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        w_satp(MAKE_SATP(p->kpagetable));
        sfence_vma();
        swtch(&c->context, &p->context); // switch process

        kvminithart(); // after process end, change back to kernel.
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;

        found = 1;
      }
      ...
} 
```

（6）最后也是最麻烦的是free kpagetable。

```c
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  if(p->kpagetable)
    proc_freekpagetable(p->kpagetable, p->kstack);
  p->pagetable = 0;
  p->kpagetable = 0;
  ...
}
```


​	同理，和正常的pagetable释放过程类似，重新设计一个free函数——`proc_freekpagetable()`。稍有不同的是`kpagetable`在分配时和kernel pagetable一样，分配了`UART0`，`VIRTIO0`，`PLIC`，`CLINT`，`kernel text`，`data`，`TRAMPOLINE`，在free时都要一一释放掉。最后`kstack`也要单独free，因为为它分配了物理空间，这一点借鉴了网上的实现，自己想不到。

```c
// Free a process's kernel page table, and free the
// physical memory it refers to.
void
proc_freekpagetable(pagetable_t pagetable, uint64 kstack)
{
  uvmunmap(pagetable, UART0, 1, 0);
  uvmunmap(pagetable, VIRTIO0, 1, 0);
  uvmunmap(pagetable, PLIC, 0x400000 >> 12, 0);
  uvmunmap(pagetable, CLINT, 0x10000 >> 12, 0);
  uvmunmap(pagetable, KERNBASE, ((uint64)getetext()-KERNBASE) >> 12, 0);
  uvmunmap(pagetable, (uint64)getetext(), (PHYSTOP-(uint64)getetext()) >> 12, 0);
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  kuvmfree(pagetable, kstack, 1);
}
```

（7）结果

```
$ usertests
usertests starting
test execout: OK
test copyin: OK
test copyout: OK
...
test dirfile: OK
test iref: OK
test forktest: OK
test bigdir: OK
ALL TESTS PASSED
```



## Simplify copyin/copyinstr

content: add user mappings to each process's kernel page table (created in the previous section) that allow `copyin` (and the related string function `copyinstr`) to directly dereference user pointers.

background: The kernel's `copyin` function reads memory pointed to by user pointers. It does this by translating them to physical addresses, which the kernel can directly dereference. It performs this translation by walking the process page-table in software.

```c
// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0); // translate pa to va.
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0); // va0 is page aliend, srcva - va0 is offset
    if(n > len)
      n = len;
    printf("copyin: n: %d, len: %d\n", n, len);
    memmove(dst, (void *)(pa0 + (srcva - va0)), n); // pa0 is the physical address of va0

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}
```

hint:

（1）Add mappings for user addresses to each process's kernel page table; 开始想的是`kpagetable`和`pagetable`一样的初始化，但应该是`pagetable`初始化后将起直接复制给`kpagetable`. 这里由于kpagetable是执行在kernel中的，所以最后要将pte的PTE_U置为0.

```c
// copy user kernel pagetable for user process pagetable
void
copypage(pagetable_t pagetable, pagetable_t kpagetable, uint64 start, uint64 end)
{
  pte_t *pte, *kpte;

  if (end < start)
    return;

  start = PGROUNDUP(start);
  for(uint64 i = start; i < end; i += PGSIZE){
    if ((pte = walk(pagetable, i, 0)) == 0)
      panic("copypage: pte not exist");
    if ((kpte = walk(kpagetable, i, 1)) == 0)
      panic("copypage: pte alloc failed");
    
    uint64 pa = PTE2PA(*pte);
    // printf("pte: %p, pa, %p, pa2pte: %p\n", *pte, pa, PA2PTE(pa));
    *kpte = PA2PTE(pa) | (PTE_FLAGS(*pte) & (~PTE_U));
  }
}
```

（2）At each point where the kernel changes a process's user mappings, change the process's kernel page table in the same way;

​	a. fork()

```c
int
fork(void)
{
  ...

  np->sz = p->sz;

  copypage(np->pagetable, np->kpagetable, 0, np->sz);

  np->parent = p;

  ...

  return pid;
}
```

​	b. exec()

```c
int
exec(char *path, char **argv)
{
  ...
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-2*PGSIZE);
  sp = sz;
  stackbase = sp - PGSIZE;

  copypage(pagetable, p->kpagetable, 0, sz);

  // Push argument strings, prepare rest of stack in ustack.
  ...
}
```

​	c. sbrk(), Don't forget about the above-mentioned `PLIC` limit.

```c
// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if (PGROUNDUP(sz + n) >= PLIC)
      return -1;
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
    copypage(p->pagetable, p->kpagetable, sz - n, sz); // sz already plus n
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
    copypage(p->pagetable, p->kpagetable, sz, sz - n); // sz already minus n
  }
  p->sz = sz;
  return 0;
}
```

（3）Don't forget that to include the first process's user page table in its kernel page table in `userinit`;

```c
// Set up first user process.
void
userinit(void)
{
  ...
  p->sz = PGSIZE;

  copypage(p->pagetable, p->kpagetable, 0, p->sz);

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer
  ...
}
```

（4）需要注意的一点是，由于user address 直接映射到kpagetable，free时只要删除映射关系即可，而不要删除物理内存，直接删除对应的物理内存会在返回user态的时候出现缺页异常。所以在这里重新定义一个unmapping的函数，用来删除kpagetable的pte，具体实现和`vmprint()`相似.

```c
// Free a process's kernel page table, donnot free the physical memory.
void
proc_freekpagetablenophy(pagetable_t kpagetable)
{
  for (int i = 0; i < 512; i++) {
		pte_t pte = kpagetable[i];
		if (pte & PTE_V) {
			if ((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
				uint64 child = PTE2PA(pte);
				proc_freekpagetablenophy((pagetable_t)child);
			}
		}
	}
	kfree((void*)kpagetable);
}
```

（5）结果

```
== Test pte printout == 
$ make qemu-gdb
pte printout: OK (3.0s) 
== Test answers-pgtbl.txt == answers-pgtbl.txt: FAIL 
    Cannot read answers-pgtbl.txt
== Test count copyin == 
$ make qemu-gdb
count copyin: OK (0.4s) 
== Test usertests == 
$ make qemu-gdb
(151.2s) 
== Test   usertests: copyin == 
  usertests: copyin: OK 
== Test   usertests: copyinstr1 == 
  usertests: copyinstr1: OK 
== Test   usertests: copyinstr2 == 
  usertests: copyinstr2: OK 
== Test   usertests: copyinstr3 == 
  usertests: copyinstr3: OK 
== Test   usertests: sbrkmuch == 
  usertests: sbrkmuch: OK 
== Test   usertests: all tests == 
  usertests: all tests: OK 
== Test time == 
time: OK 
Score: 61/66
```

遇到的问题：

（1）遇到bug

```
FAILED -- lost some free pages 31964 (out of 32166)
```

这是因为在A kernel page table per process实验的（4）中为`p->stack`分配了物理空间，然后在`proc_freekpagetable()`函数中释放了该物理内存，但是这个实验由于定义了新的删除pte的函数`proc_freekpagetablenophy()`，从而没有删除`p->stack`对应的物理内存，导致了该bug。故定义`proc_freekstack()`函数删除`p->kstack`的物理空间。

```c
// free kstack's physical space
void
proc_freekstack(struct proc * p){
  pte_t *pte = walk(p->kpagetable, p->kstack, 0);
  if(pte == 0)
    panic("freeproc: free kstack");
  kfree((void*)PTE2PA(*pte));
  p->kstack = 0;
}
```

很多时候遇到这个bug都要着重考虑free相关的函数。

## RISC-V assembly

```
1. a1 and a2.
2. call.asm:44 printf, call.asm:30 return g(x).
3. 0000000000000630
4. 0x0
5. HE110 World, 0x726c6400
6. maybe produce randomly.
```

## Backtrace

content: backtrace: a list of the function calls on the stack above the point at which the error occurred.

hint:

（1）Add the prototype;

```c
// printf.c
...
void            backtrace();
```

（2）The GCC compiler stores **the frame pointer** of the currently executing function in the register `s0`;

```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```

（3）implementing backtrace;

```c
void backtrace(void){
  uint64 *s0 = (uint64 *)r_fp();
  uint64 up = PGROUNDUP((uint64)s0);

  printf("backtrace:\n");

  while ((uint64)s0 != up){
    s0 = (uint64*)((uint64)s0 - 16);
    // printf("s0 + 8:%p \n", s0 + 8);
    // printf("(uint64)s0 + 8):%p \n", (uint64)s0 + 8);
    // printf("(uint64*)((uint64)s0 + 8):%p \n", (uint64*)((uint64)s0 + 8));
    printf("%p \n", *(uint64*)((uint64)s0 + 8));
    s0 = (uint64*)*s0;
    // printf("s0:%p\n", s0);
    // printf("*s0:%p\n", *s0);
    // printf("(uint64*)*s0:%p\n", (uint64*)*s0);
  }
}
```

（4） Insert a call to this function in `sys_sleep`;

```c
uint64
sys_sleep(void)
{
  ...
  release(&tickslock);
  backtrace();
  return 0;
}
```

（5）结果

```
$ bttest
backtrace:
0x0000000080002e0e 
0x0000000080002c70 
0x00000000800028c6
```

```
0x0000000080002e0e 
0x0000000080002c70 
0x00000000800028c6
/home/guanshun/gitlab/xv6-labs-2020/kernel/sysproc.c:74
/home/guanshun/gitlab/xv6-labs-2020/kernel/syscall.c:161
/home/guanshun/gitlab/xv6-labs-2020/kernel/trap.c:76
```

难点有二：

（1）`uint64 up = PGROUNDUP((uint64)s0)` 设置stack pointer的上限，用来终止循环。

（2）指针的使用，`uint64 *`只是用来修改指针的格式，不改变指针的值。

## Alarm

content: add a feature to xv6 that periodically alerts a process as it uses CPU time.

hint:

（1）`sys_sigalarm()` should store the alarm interval and the pointer to the handler function in new fields in the `proc` structure.

```c
// Per-process state
struct proc {
  struct spinlock lock;

  ...
  char name[16];               // Process name (debugging)
  int alarmticks;
  void(* alarmhandler)();
  int tickcounts;
  struct trapframe cptrapframe;
  uint64 tpc;                   // save the pc when trap
};
```

```c
uint64
sys_sigalarm(void)
{
  int n;
  void (*handler)();
  
  if(argint(0, &n) < 0)
    return -1;
  if(argptr(1, (char **)&handler, 1) < 0)
    return -1;
  
  myproc()->alarmticks = n;
  myproc()->alarmhandler = handler;
  return 0;
}
```

```c
// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.
int
argptr(int n, char **pp, int size)
{
  int i;
  struct proc *curproc = myproc();
 
  if(argint(n, &i) < 0)
    return -1;
  if(size < 0 || (uint64)i >= curproc->sz || (uint64)i+size > curproc->sz)
    return -1;
  *pp = (char*)(uint64)i;
  return 0;
}
```

（2）initialize `proc` fields in `allocproc()` in `proc.c`;

```c
static struct proc*
allocproc(void)
{
  struct proc *p;
  ...
  p->alarmticks = 0;
  p->alarmhandler = 0;
  p->tickcounts = 0;

  return p;
}
```

（3）Every tick, the hardware clock forces an interrupt, which is handled in `usertrap()` in `kernel/trap.c`;

```c
void
usertrap(void)
{
  int which_dev = 0;

  ...

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    p->tickcounts++;
    if(p->alarmticks == p->tickcounts) {
      // p->tickcounts = 0;
      // p->trapframe->kernel_sp -= 8;
      // *(uint64 *) p->trapframe->kernel_sp = p->trapframe->epc;
      // p->tpc = p->trapframe->epc;
      memmove(&(p->cptrapframe), p->trapframe, sizeof(struct trapframe));
      p->trapframe->epc = (uint64) p->alarmhandler;
      // printf("which_dev: %d\nhandler: %d\n", which_dev, (uint64) p->alarmhandler);
    }
    yield();
  }
  
  usertrapret();
}
```

（4）save and restore registers;

```c
uint64
sys_sigreturn()
{
  struct proc * p = myproc();
  // p->trapframe->epc = *(uint64 *) p->trapframe->kernel_sp;
  // p->trapframe->kernel_sp += 8;
  p->trapframe->epc = p->tpc;
  memmove(p->trapframe, &(p->cptrapframe), sizeof(struct trapframe));
  p->tickcounts = 0;
  return 0;
}
```

（5）结果

```c
== Test answers-traps.txt == answers-traps.txt: OK 
== Test backtrace test == 
$ make qemu-gdb
backtrace test: OK (2.2s) 
== Test running alarmtest == 
$ make qemu-gdb
(3.3s) 
== Test   alarmtest: test0 == 
  alarmtest: test0: OK 
== Test   alarmtest: test1 == 
  alarmtest: test1: OK 
== Test   alarmtest: test2 == 
  alarmtest: test2: OK 
== Test usertests == 
$ make qemu-gdb
usertests: OK (210.2s) 
== Test time == 
time: OK 
Score: 85/85
```

遇到的问题：

（1）怎样计算n ticks

Every tick, the hardware clock forces an interrupt.即每个tick，cpu都会检查中断信息。

（2）kernel怎样调用alarmtest中的`fn()`。

保存了fn的地址，之后将pc置为这个地址即可。

这里注意是将epc保存在kernel stack。这个是错误的，保存在哪里不重要，只要保存下来就好，可以在proc中定义变量来保存，将整个trapframe都保存下来，之后再恢复。

## Eliminate allocation from sbrk()

注：`lazy`实验由于无意中drop stash，然后也没有commit，所以没有完整的代码。`lazy`分支的代码并不正确，应该是`cow`实验的部分代码。

## lazy page allocation

content: Modify the code in trap.c to respond to a page fault from user space by mapping a newly-allocated page of physical memory at the faulting address, and then returning back to user space to let the process continue executing.

​	这个实验想要传递的系统设计思想是在进程申请内存时并不直接分配物理内存，而是只更新进程需要的内存空间`pagetable->sz`，之后等进程需要访问物理内存时触发page fault，在usertrap中通过`kalloc()`和`mappages()`来为进程分配物理空间。以下几点需要注意：

（1）由于开始没有分配物理内存，也就没有va->pa的映射， 所以遇到这映射的判断时不要触发panic，而是直接检查下一个映射。

（2）检查va是否有效。

​		a. 是否超过了内存需要的地址空间；

```c
if(va >= p->sz){ // out of the allocate memory
	p->killed = 1;
	exit(-1);
}
```

处理栈溢出；

```c
va = PGROUNDDOWN(va);
if(va < PGROUNDDOWN(p->trapframe->sp)){
	p->killed = 1;
	exit(-1);
}
```

关于os的page fault有了新的认知，即重新分配内存，然后mapping。

```c
if(r_scause() == 0xd || r_scause() == 0xf){
	char * mem = kalloc();
	if(mem == 0) // allocate memory failed
	p->killed = 1;
	exit(-1);
	
	uint64 va = r_stval();
	if(va >= p->sz){ // out of the allocate memory
	p->killed = 1;
	exit(-1);
	}
	va = PGROUNDDOWN(va);
	if(va < PGROUNDDOWN(p->trapframe->sp)){ // invaild page below the stack
		p->killed = 1;
		exit(-1);
	}
	// printf("mem: %p\n", (uint64)mem);
	memset(mem, 0, PGSIZE);
	// printf("pgtbl: %p, stval: %p, mem: %p\n", p->pagetable, r_stval(), mem);	
	if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
		kfree(mem);
		p->killed = 1;
		exit(-1);
	}
}
```



## Implement copy-on write

content: implement copy-on-write fork in the xv6 kernel.

​	COW defers allocating and copying physical memory pages for the child until the copies are actually needed。即`fork()`原本在创建子进程时需要将所有的父进程的内容复制到子进程的进程空间，但是这个很耗时，因此想到子进程和父进程共用进程空间，当子进程和父进程要写某个page时触发page fault，这时再`kalloc()`，将该page的内容复制到新的page，更新pte。

hint:

（1）在`uvmcopy()`中将父进程的该页的`pte`置为`PTE_COW`和`~PTE_W`，不需要`kalloc()`， map的时候还是用父进程的pa;

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    *pte = ((*pte) & (~PTE_W)) | PTE_COW; // set parent's page unwritable
    if(mappages(new, i, PGSIZE, (uint64)pa, (flags & (~PTE_W)) | PTE_COW) != 0){
      goto err;
    }
    // rcount(pa, '+', 0);
    increase(pa, 1);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

（2）Modify `usertrap()` to recognize page faults; 注意这里和lazy实验不一样，只需要处理`r_scause() == 0xf`，即写的缺页中断。

```c
void
usertrap(void)
{
  ...
  } else if (r_scause() == 0xf) {
    uint64 va = r_stval();
    if(copyonwrite(va) != 0){
      p->killed = 1;
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
...
}
```

（2）定义一个count变量来确定每一个page的使用情况，以及对应的操作函数。

```c
struct COUNT{
  uint count[(PHYSTOP - KERNBASE) / PGSIZE];
  struct spinlock lock;
}count;
```

```c
uint64 index(uint64 pa){
  return (pa - KERNBASE) / PGSIZE;
}

void lock(){
  acquire(&count.lock);
}

void unlock(){
  release(&count.lock);
}

void set(uint64 pa, int n){
  count.count[index(pa)] = n;
}

uint get(uint64 pa){
  return count.count[index(pa)];
}

void increase(uint64 pa, int n){
  // lock();
  count.count[index(pa)] += n;
  // unlock();
}
```

（3）在`kalloc()`初始化时将对应page的count置为1；

```c
void *
kalloc(void)
{
  ...

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    set((uint64)r, 1); // set the page count to 1
  }
  return (void*)r;
}
```

（4）在`kfree()`中，如果count > 1，则说明有多个process使用该页，不需要free，只需要将count - 1，反之free该页。

```c
void
kfree(void *pa)
{
  struct run *r;

  // lock();
  if(get((uint64)pa) > 1){
    count.count[index((uint64)pa)] -= 1;
    // increase((uint64)pa, -1); // still have processes use this page
    // unlock();
    return;
  }

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  set((uint64)pa, 0);
  // unlock();
  // printf("kfree: rcount: %d\n", rcount((uint64)pa, 'v', 0));
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

（5）结果

```
== Test running cowtest == 
$ make qemu-gdb
(6.3s) 
== Test   simple == 
  simple: OK 
== Test   three == 
  three: OK 
== Test   file == 
  file: OK 
== Test usertests == 
$ make qemu-gdb
(62.3s) 
== Test   usertests: copyin == 
  usertests: copyin: OK 
== Test   usertests: copyout == 
  usertests: copyout: OK 
== Test   usertests: all tests == 
  usertests: all tests: OK 
== Test time == 
time: OK 
Score: 110/110
```



## Uthread: switching between threads

content: design the context switch mechanism for a user-level threading system, and then implement it.

（1）创建进程；

```c
// Saved registers for user context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;c
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```

```c
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)(*func);
  t->context.sp = (uint64)(t->stack + STACK_SIZE);
}

struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct context context;       /* swtch() here to enter scheduler() */
};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
extern void thread_switch(uint64, uint64); // old, new
```

​	主要是创建context，然后将返回地址保存到`ra`寄存器中，将堆栈地址保存在`sp`寄存器中。但为什么堆栈地址要保存成`(uint64)(t→stack + STACK_SIZE)`还不清楚，试过保存`(uint64)(t→stack)`，这样就不能正常的switch，只执行了thread_c。

​       在手册中有这样一句话：caller-saved registers are saved on the stack (if needed) by the calling C code. 也就是说`t->context.sp = (uint64)(t->stack + STACK_SIZE)`是保存caller的registers的，如果不加上STACK_SIZE，那么就没有保存caller的registers，之后执行还是在第一个线程的stack中执行，这就是为什么不加STACK_SIZE执行只print thread_c的内容。

（2）进程切换；

```c
void 
thread_schedule(void)
{
  struct thread *t, *next_thread;
  ...
  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    // save the current context in t->context
    // switch to the scheduler context previously
    // saved in current_thread->scheduler
    thread_switch((uint64)(&t->context), (uint64)(&current_thread->context));
  } else
    next_thread = 0;
}
```

```c
	.globl thread_switch
thread_switch:
	    sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
	ret    /* return to ra */
```

结果：

```
$ uthread
thread_a started
thread_b started
thread_c started
thread_c 0
thread_a 0
thread_b 0
thread_c 1
thread_a 1
...
thread_b 98
thread_c 99
thread_a 99
thread_b 99
thread_c: exit after 100
thread_a: exit after 100
thread_b: exit after 100
thread_schedule: no runnable threads
```



## Using threads

context: explore parallel programming with threads and locks using a hash table.

​	首先是线程的互斥问题导致的hash miss，那么加锁，让不同的线程互斥访问即可。

（1）声明锁；

```c
pthread_mutex_t lock;            // declare a lock
```

（2）初始化，注意在`main()`中初始化一次即可，所有的thread用同一个锁；

```c
pthread_mutex_init(&lock, NULL); // initialize the lock
```

（3）给put, get加锁；

```c
static 
void put(int key, int value)
{
  int i = key % NBUCKET;
  // is the key already present?
  // pthread_mutex_lock(&lock);       // acquire lock
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    pthread_mutex_lock(&lock);       // acquire lock
    insert(key, value, &table[i], table[i]);
    pthread_mutex_unlock(&lock);     // release lock
  }
  // pthread_mutex_unlock(&lock);     // release lock
}
```

```c
static struct entry*
get(int key)
{
  int i = key % NBUCKET;
  
  pthread_mutex_lock(&lock);       // acquire lock
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) 
      break;
  }
  pthread_mutex_unlock(&lock);     // release lock

  return e;
}
```

（4）结果

```
guanshun@Jack-ubuntu ~/g/xv6-labs-2020 (thread)> make ph
gcc -o ph -g -O2 notxv6/ph.c -pthread
guanshun@Jack-ubuntu ~/g/xv6-labs-2020 (thread)> ./ph 1
100000 puts, 5.462 seconds, 18307 puts/second
0: 0 keys missing
100000 gets, 5.494 seconds, 18201 gets/second
guanshun@Jack-ubuntu ~/g/xv6-labs-2020 (thread)> ./ph 2
100000 puts, 5.811 seconds, 17208 puts/second
0: 0 keys missing
1: 0 keys missing
200000 gets, 11.477 seconds, 17426 gets/second
```

但是2个thread并没有比1个thread快两倍，因为两个thread并不是并行插入hash table的，而是一个thread写前半部分，一个thread写后半部分，然后get的时候get两个thread都get一次。那么可以只给会造成冲突的部分加锁，即insert部分，其他部分两个thread同时访问。

```c
static 
void put(int key, int value)
{
  int i = key % NBUCKET;
  // is the key already present?
  // pthread_mutex_lock(&lock);       // acquire lock
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    pthread_mutex_lock(&lock);       // acquire lock
    insert(key, value, &table[i], table[i]);
    pthread_mutex_unlock(&lock);     // release lock
  }
  // pthread_mutex_unlock(&lock);     // release lock
}
```

```
guanshun@Jack-ubuntu ~/g/xv6-labs-2020 (thread)> ./ph 1
100000 puts, 5.414 seconds, 18469 puts/second
0: 0 keys missing
100000 gets, 5.401 seconds, 18516 gets/second
guanshun@Jack-ubuntu ~/g/xv6-labs-2020 (thread)> ./ph 2
100000 puts, 2.912 seconds, 34345 puts/second
0: 0 keys missing
1: 0 keys missing
200000 gets, 11.351 seconds, 17620 gets/second
```



## Barrier

content: barrier: a point in an application at which all participating threads must wait until all other participating threads reach that point too.

​	首先明确函数功能：

​	The `pthread_cond_wait()` and `pthread_cond_timedwait()` functions are used to block on a condition variable. These functions **atomically release mutex** and cause the calling thread to block on the condition variable cond.

​	The `pthread_cond_signal()` call unblocks **at least one of the threads** that are blocked on the specified condition variable cond (if any threads are blocked on cond).

​	The `pthread_cond_broadcast()` call unblocks **all threads** currently blocked on the specified condition variable cond.

（1）编写`barrier()`函数，在函数开始时加锁，如果当前进程是最后一个进程，则唤醒所有block的进程，如果不是，则`pthread_cond_wait()`，这个函数会释放`barrier_mutex`锁，所以第33行不需要释放`barrier_mutex`，这是开始犯的一个错误，没有仔细看手册。这里用到的条件变量`barrier_cond`，互斥变量`barrier_mutex`都是源码声明好的。

```c
struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;
```

```c
static void 
barrier()
{
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  
  pthread_mutex_lock(&bstate.barrier_mutex);       // acquire lock
  bstate.nthread++;
  // pthread_mutex_unlock(&bstate.barrier_mutex);     // release lock
  if (bstate.nthread == nthread){
    // pthread_mutex_lock(&bstate.barrier_mutex);       // acquire lock
    bstate.nthread = 0;
    bstate.round++;
    pthread_cond_broadcast(&bstate.barrier_cond);
    // pthread_mutex_unlock(&bstate.barrier_mutex);     // release lock
    
    // printf("2. round: %d, count: %d\n", bstate.round - 1, bstate.nthread);
  } else {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);     // release lock
}
```

```
guanshun@Jack-ubuntu ~/g/xv6-labs-2020 (thread)> ./barrier 128
OK; passed
```

## Memory allocator

content: redesign the memory allocator to avoid a single lock and list. The basic idea is to maintain a free list per CPU, each list with its own lock. Allocations and frees on different CPUs can run in parallel, because each CPU will operate on a different list.

background:

spin lock 和sleep lock之间的区别。

Holding a spinlock that long would lead to waste if another process wanted to acquire it, since the acquiring process would waste CPU for a long time while spinning. 即如果其他的process想使用spinlock，它会一直acquire，就是所谓的“忙等”。	而sleeplock在获取到lock后进程会sleep，让出cpu。

A process cannot yield the CPU while retaining a spinlock.

Spin-locks are best suited to **short critical sections**, since waiting for them wastes CPU time; sleep-locks work well for **lengthy operations**.

按照hint写完之后遇到bug：

```
hart 2 starting
hart 1 starting
initproc: 0x00000000800127a8
panic: init exiting
```

是因为kinit()只为一个cpu分配了freelist，其他cpu的freelist都是空的，之后kalloc()肯定出错。

```c
void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem" + i);
  }
  freerange(end, (void *)PHYSTOP);
}
```

所以`kalloc()`中要"steal" part of the other CPU's free list。

```c
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu_id = cpuid();
  pop_off();

  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;
  if(r)
    kmem[cpu_id].freelist = r->next;
  release(&kmem[cpu_id].lock);

  if (!r) {
    for (int i = 0; i < NCPU; i++) {
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if (r) {
        kmem[i].freelist = r->next;
      }
      release(&kmem[i].lock);
      if (r) {
        break;
      }
    }
  }
  // printf("freelist: %p, kalloc: cpuid: %d\n", kmem[cpu_id].freelist, cpu_id);
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
```

也就是说直接将其他cpu的freelist分配给当前cpu，之后free是根据pa，不会产生冲突。未分配物理空间的cpu第一次free的`freelist`是0，之后随着free的次数增多，`freelist`就随即建立起来。

```
freelist: 0x0000000000000000, kfree1: cpuid: 3
freelist: 0x0000000087f73000, kfree2: cpuid: 3
freelist: 0x0000000087f73000, kfree1: cpuid: 3
freelist: 0x0000000087f71000, kfree2: cpuid: 3
freelist: 0x0000000087f71000, kfree1: cpuid: 3
```

最后，重新定义`kmem`数据结构。

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
```

注意：不能在acquire()中再次使用`acquire()`，必须要`release()`后才能使用。



## Buffer cache

content: Modify the block cache so that the number of `acquire` loop iterations for all locks in the bcache is close to zero when running `bcachetest`.

The buffer cache has two jobs: 

（1）synchronize access to disk blocks to ensure that only one copy of a block is in memory and that only one kernel thread at a time uses that copy;

（2）cache popular sblocks so that they don’t need to be re-read from the slow disk.

思路：

（1）hint中说：“We suggest you look up block numbers in the cache with a hash table that has a lock per hash bucket.“，所以对`bcache`数据结构进行修改，以`blockno`作为hash key, buckets = 13。

```c
#define prime 13

struct {
  struct spinlock lock[prime];
  struct buf buf[prime][2]; // NBUF is 30
} bcache;
```

（2）hint4: Remove the list of all buffers (`bcache.head` etc.) and instead time-stamp buffers using the time of their last use. 在buf数据结构中加上ticks变量，用来记录时间戳。

```c
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
  uint ticks; // used for LRU
};
```

重写替换算法。

```c
// Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  struct buf * temp = bcache.buf[bucket];
  for(b = bcache.buf[bucket]; b < bcache.buf[bucket] + NBUF; b++){
    if(b->ticks < temp->ticks) {
      temp = b;
    }
  }
  temp->dev = dev;
  temp->blockno = blockno;
  temp->valid = 0;
  temp->refcnt = 1;
  acquire(&tickslock);
  temp->ticks = ticks;    // use ticks instead of link-list for LRU
  release(&tickslock);
  release(&bcache.lock[bucket]);
  // printf("dev: %p, blockno: %p\n", temp->dev, temp->blockno);
  acquiresleep(&temp->lock);
  return temp;
```

遇到的bug：

（1）panic findslot

```
#0  findslot (lk=0x80084650 <icache+2648>) at kernel/spinlock.c:42
#1  initlock (lk=lk@entry=0x80084650 <icache+2648>, name=name@entry=0x80008700 "sleep lock") at kernel/spinlock.c:56
#2  0x000000008000467e in initsleeplock (lk=lk@entry=0x80084648 <icache+2640>, name=name@entry=0x80008638 "inode") at kernel/sleeplock.c:15
#3  0x0000000080003818 in iinit () at kernel/fs.c:186
#4  0x0000000080001366 in main () at kernel/main.c:31
```

`NBUF`为30，申请的buf大于`NBUF`。

（2）bcachetest的test1, test2都能通过，但是usertests的manywrites超时。

```
$ make qemu-gdb
(98.2s) 
== Test   kalloctest: test1 == 
  kalloctest: test1: OK 
== Test   kalloctest: test2 == 
  kalloctest: test2: OK 
== Test kalloctest: sbrkmuch == 
$ make qemu-gdb
kalloctest: sbrkmuch: OK (11.7s) 
== Test running bcachetest == 
$ make qemu-gdb
(9.1s) 
== Test   bcachetest: test0 == 
  bcachetest: test0: OK 
== Test   bcachetest: test1 == 
  bcachetest: test1: OK 
== Test usertests == 
$ make qemu-gdb
Timeout! usertests: FAIL (300.2s) 
    ...
         hart 1 starting
         init: starting sh
         $ usertests
         usertests starting
         test manywrites: qemu-system-riscv64: terminating on signal 15 from pid 416200 (make)
    MISSING '^ALL TESTS PASSED$'
    QEMU output saved to xv6.out.usertests
```

经过测试，只有manywrites没有通过，进一步研究manywrites的测试部分，应该是导致了deadlock。（这个bug暂未解决）



## Large files

content: increase the maximum size of an xv6 file.

hint：

（1）an xv6 inode contains 12 "**direct**" block numbers and one "**singly-indirect**" block number, which refers to a block that holds up to 256 more block numbers, for a total of 12+256=**268** blocks.

```c
#define NDIRECT 12
...
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  ...
  uint addrs[NDIRECT+1];   // Data block addresses
};
```

```c
// in-memory copy of an inode
struct inode {
  ...
  uint addrs[NDIRECT+1];
};
```

​	change the xv6 file system code to support a "**doubly-indirect**" block in each inode, containing 256 addresses of singly-indirect blocks, each of which can contain up to 256 addresses of data blocks. The result will be that a file will be able to consist of up to  **256*256+256+11** blocks .

```c
#define NDIRECT 11
...
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)

// On-disk inode structure
struct dinode {
  ...
  uint addrs[NDIRECT+2];   // Data block addresses
};
```

```c
// in-memory copy of an inode
struct inode {
  ...
  uint addrs[NDIRECT+2];
};
```

（2）修改`bmap()`，和`itrunc()`。

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a, *b;
  struct buf *bp1, *bp2;

  ...

  if (bn < NINDIRECT * NINDIRECT) { // doubly-indirect blocks
    // Load doubly-indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT + 1]) == 0)
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    bp1 = bread(ip->dev, addr);
    a = (uint*)bp1->data;
    if ((addr = a[bn / NINDIRECT]) == 0) {
      a[bn / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp1);
    }
    brelse(bp1);

    bp2 = bread(ip->dev, addr);
    b = (uint*)bp2->data;
    if((addr = b[bn % NINDIRECT]) == 0){
      b[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp2);
    }
    brelse(bp2);
    // printf("2. addr: %d\n", addr);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp1, *bp2;
  uint *a, *b;

  ...

  if(ip->addrs[NDIRECT + 1]){
    bp1 = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp1->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j]) {
        bp2 = bread(ip->dev, a[j]);
        b = (uint*)bp2->data;
        for (int k = 0; k < NINDIRECT; k++) {
          if (b[j]) {
            bfree(ip->dev, b[k]);
          }
        }
        brelse(bp2);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp1);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

（4）结果

```
$ usertests
usertests starting
...
ALL TESTS PASSED
$ bigfile
..................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
wrote 65803 blocks
bigfile done; ok
```



## Symbolic links

content: add symbolic links to xv6. Symbolic links (or soft links) refer to a linked file by pathname.

思路：开始想的是找到`target`所指向的file，将其赋给`path`，然后写入directtory。但是后来发现这样不行，因为存在target指向的file本身就是symlink，故`create`一个`inode`，它的路径就是`path`，type是`T_SYMLINK`，然后将target指向的file通过`memmove`直接复制到新的`inode`中。

（1）添加`sym_symlink()`;

```c
uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;
  return symlink(target, path);
}

int symlink(char *target, char *path){
  struct inode *ip;

  begin_op();
  ip = create(path, T_SYMLINK, 0, 0);
  if(ip == 0){
    end_op();
    return -1;
  }

  if(writei(ip, 0, (uint64)target, 0, MAXPATH) != MAXPATH) {
    panic("symlink");
  }

  iunlockput(ip);
  end_op();
  return 0;
}
```

（2）在`sys_open()`中处理`T_SYMLINK`类型的文件，注意需要设置`threshold`，超过10次就返回错误。

```c
  if (ip->type == T_SYMLINK) {
    int threshold = 0;
    char target[MAXPATH];
    if (!(omode & O_NOFOLLOW)) {
      while (ip->type == T_SYMLINK) {
        if (threshold == 10){
          // panic("threshold: 10\n");
          iunlockput(ip);
          end_op();
          // printf("failed\n");
          return -1;
        }
        memset(target, 0, sizeof(target));
        if(readi(ip, 0, (uint64)target, 0, MAXPATH) != MAXPATH) {
          panic("readi\n");
        }
        iunlockput(ip);
        if ((ip = namei(target)) == 0) {
          end_op();
          return -1;
        }
        threshold++;
        ilock(ip);
      }
    }
  }
```

这其中有两个关键的函数 `readi(ip, 0, (uint64)target, 0, MAXPATH)` 和 `ip = namei(target)` 前一个是将ip的`offset = 0`, `n = MAXPATH`的数据读取到target中，也就是之前写入的路径名，后一个是通过路径名得到指向的inode。

（3）结果

```
$ make qemu-gdb
running bigfile: OK (75.9s) 
== Test running symlinktest == 
$ make qemu-gdb
(0.7s) 
== Test   symlinktest: symlinks == 
  symlinktest: symlinks: OK 
== Test   symlinktest: concurrent symlinks == 
  symlinktest: concurrent symlinks: OK 
== Test usertests == 
$ make qemu-gdb
usertests: OK (151.8s) 
== Test time == 
time: OK 
Score: 100/100
```



## mmap

background:

`mmap`, `munmap` - map or unmap files or devices into memory

```c
#include <sys/mman.h>

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
int munmap(void *addr, size_t length);
```

`mmap()`  creates  a  new  mapping in the virtual address space of the calling process.  The starting address for the new mapping is specified in **addr**.  The **length** argument specifies the length of the mapping (which must be greater than 0).

If addr is **NULL**, then the kernel chooses **the (page-aligned) address** at which to create the mapping.  If addr is not NULL, then the  kernel takes  it  as  a hint about where to place the mapping;

The  contents  of a file mapping are initialized using **length** bytes starting at offset **offset** in the file (or other object) referred to by the file descriptor **fd**.

The **prot** argument describes the desired memory protection of the mapping.

The **flags** argument determines whether updates to the mapping are visible to other processes mapping the same region, and whether updates are carried through to the underlying file.

content: `mmap` can be called in many ways, but this lab requires only a subset of its features relevant to *memory-mapping a file*.

The `munmap()` system call deletes the mappings for the specified address range, and causes further references to addresses within the range to generate invalid memory references. If the process has modified the memory and has it mapped `MAP_SHARED`, the modifications should first be written to the file.

hint:

（1）Define a structure corresponding to the VMA (virtual memory area). 这里需要在`proc`中添加`vma`变量，用来表示该进程的mmap。然后所有的进程公用一个`VMA` array，每次添加新的mmap映射都需要互斥的从array中获取。

```c
#define NVMA 16
#define VMASTART MAXVA / 2
struct VMA {
  int count;
  uint64 start;
  uint64 end;
  uint64 length;
  int prot;
  int flags;
  struct file * fd;
  uint offset;
  struct VMA * next;
  struct spinlock lock;
};
```

```c
struct VMA vma[NVMA];

struct VMA * getvma(){
  for (int i = 0; i < NVMA; i++){
    acquire(&vma[i].lock);
    if(vma[i].end == vma[i].start){
      vma[i].count = i;
      release(&vma[i].lock);
      return &vma[i];
    }
    release(&vma[i].lock);
  }
  panic("not enough vma\n");
}
```

（2）Implement `mmap`.  每个进程添加新的mmap映射时都需要从array中获取一个VMA变量，初始化VMA变量，然后将其添加到proc的VMA链表中，每个进程都维护一个VMA链表。这里需要对flag做一些处理，因为mmap映射的地址空间有不同的权限，这些权限要准确的写入VMA。

```c
uint64 mmap(uint64 addr, uint length, int prot, int flags, int fd, uint offset)
{
  struct proc * p = myproc();
  struct VMA * v = getvma();
  struct file * f = p->ofile[fd];

  int pte_flag = PTE_U;
  if (prot & PROT_WRITE) {
    if(!f->writable && !(flags & MAP_PRIVATE))
      return -1; // map to a unwritable file with PROT_WRITE
    pte_flag |= PTE_W;
  }
  if (prot & PROT_READ) {
    if(!f->readable)
      return -1; // map to a unreadable file with PROT_READ
    pte_flag |= PTE_R;
  }

  printf("length: %x, prot: %x, flags: %x,"
    " fd: %x, offset: %x\n", length, prot, flags, fd, offset);
  v->fd = f;
  v->flags = flags;
  v->offset = offset;
  v->length = length;
  v->prot = pte_flag;
  // p->sz += length;
  filedup(f); // increase the file's reference count
  struct VMA * pv = p->vma;
  if(pv == 0){
    v->start = VMASTART;
    v->end = v->start + length;
    p->vma = v;
  }else{
    while(pv->next) {
      printf("pv: %p\n", pv);
      pv = pv->next;
    }
    v->start = PGROUNDUP(pv->end);
    v->end = v->start + length;
    pv->next = v;
    v->next = 0;
  }
  addr = v->start;
  printf("mmap: p->sz: %x, offset: %x, start: %x, end: %x, length: %x\n",
      p->sz, v->offset, v->start, v->end, v->length);
  return addr;
}
```

（3）Implement `usertrap`. 这部分的代码可以用`lab: lazy`的代码，而最后读文件`readi()`也可以直接用原有的代码。值得注意的是读取文件时使用的偏移量`v->offset + a - v->start`，例如`va = 3000`, `v->start = 3000`，不能直接用`va`当作偏移量，因为`v->start = 3000`的byte是文件的第0 byte。

```c
    if(r_scause() == 0xd || r_scause() == 0xf){
      uint64 va = r_stval();
      struct VMA * v = p->vma;

      while (v) {
        if (va >= v->start && va < v->end) {
          break;
        }
        v = v->next;
      }

      if(v == 0){
        printf("trap: v = 0, va: %x\n", PGROUNDDOWN(va));
        p->killed = 1; // not mmap addr
        goto fault;
      }
      if(r_scause() == 0xd && !(v->prot & PTE_R)){
        printf("unreadable vma\n");
        p->killed = 1; // unreadable vma
        goto fault;
      }
      if(r_scause() == 0xf && !(v->prot & PTE_W)){
        printf("unwritable vma\n");
        p->killed = 1; // unwritable vma
        goto fault;
      }

      uint64 a = PGROUNDDOWN(va);

      char * mem = kalloc();
      if(mem == 0){ // alloc memory failed
        p->killed = 1;
      }
      // printf("mem: %p\n", (uint64)mem);
      memset(mem, 0, PGSIZE);
      printf("offset: %x, va: %x, start: %x, read: %x\n",
        v->offset, a, v->start, v->offset + a - v->start);
      if(mappages(p->pagetable, a, PGSIZE, (uint64)mem, v->prot) != 0){
        printf("mapping fault\n");
        kfree(mem);
        p->killed = 1;
        goto fault;
      }

      int r;
      struct file * f = v->fd;
      ilock(f->ip);
      if((r = readi(f->ip, 0, (uint64)mem, v->offset + a - v->start, PGSIZE)) > 0)
        f->off += r;
      iunlock(f->ip);
      
      ...
      
      fault:
  		if(p->killed)
    	exit(-1);
```

（4）Implement `munmap`. 主要有三个部分：当flags是`MAP_SHARED`时写回；删除addr对应的映射信息；当这个mmap全部删除后，将引用的文件`refcount--`。

```c
int munmap(uint64 addr, uint length)
{
  struct proc * p = myproc();
  struct VMA * v = p->vma;
  struct VMA * pre = 0;
  int wb;
  while (v) {
    if (addr >= v->start && addr < v->end) {
      v->length -= length;
      break;
    }
    pre = v;
    v = v->next;
  }

  if (v == 0)
    return -1;

  struct file * f = v->fd;

  if ((v->flags & MAP_SHARED) && (v->start == addr)) {
    wb = writeback(v, v->start, length);
    if(wb != 0)
      return wb;
  }
  uvmunmap(p->pagetable, addr, PGROUNDUP(length) / PGSIZE, 1);

  if (addr == v->start) { // munmap from start
    if (v->length == 0) { // remove all pages of a previous mmap
      fileclose(f);           // decrement the reference count
      if(pre == 0){
        pre = v->next;
      }else{
        pre->next = v->next;
      }
      p->vma = pre; // free the vma
    } else {
      v->start -= length;
      v->offset += length;
    }
  } else { // munmap from end
      v->end -= length;
  }
  printf("munmap: flags: %x, addr: %x, start: %x, end: %x, lenght: %x\n",
      v->flags, addr, v->start, v->end, v->length);
  return 0;
}
```

（5）Modify `exit`. 

```c
  // remove all mmap vma
  struct VMA* v = p->vma;
  while(v){
    writeback(v, v->start, v->length);
    uvmunmap(p->pagetable, v->start, PGROUNDUP(v->length) / PGSIZE, 1);
    fileclose(v->fd);
    v = v->next;
  }
```

（6）Modify `fork`. 这两部分比较简单。

```c
  // np->vma = p->vma;
  // filedup(np->vma->fd);
  np->vma = 0;
  struct VMA *pv = p->vma;
  struct VMA *pre = 0;
  while(pv){
    struct VMA *vma = getvma();
    vma->count = pv->count;
    vma->start = pv->start;
    vma->end = pv->end;
    vma->offset = pv->offset;
    vma->length = pv->length;
    vma->prot = pv->prot;
    vma->flags = pv->flags;
    vma->fd = pv->fd;
    filedup(vma->fd);
    vma->next = 0;
    if(pre == 0){
      np->vma = vma;
    }else{
      pre->next = vma;
    }
    pre = vma;
    pv = pv->next;
  }
```

（7）结果：

```
== Test running mmaptest == 
$ make qemu-gdb
(3.8s) 
== Test   mmaptest: mmap f == 
  mmaptest: mmap f: OK 
== Test   mmaptest: mmap private == 
  mmaptest: mmap private: OK 
== Test   mmaptest: mmap read-only == 
  mmaptest: mmap read-only: OK 
== Test   mmaptest: mmap read/write == 
  mmaptest: mmap read/write: OK 
== Test   mmaptest: mmap dirty == 
  mmaptest: mmap dirty: OK 
== Test   mmaptest: not-mapped unmap == 
  mmaptest: not-mapped unmap: OK 
== Test   mmaptest: two files == 
  mmaptest: two files: OK 
== Test   mmaptest: fork_test == 
  mmaptest: fork_test: OK 
== Test usertests == 
$ make qemu-gdb
usertests: OK (85.9s) 
== Test time == 
time: OK 
Score: 140/140
```

（8）关键：搞清楚mmap的原理是什么。做试验时在定义`VMA`结构体时遇到困难，开始想的是直接定义大小为16的array，然后定义一个count，互斥的对count进行操作，从而达到对VMA互斥访问的效果，但是没有想到每个`proc`结构体也要维护一个`VMA`链表。另一个问题是`VAMSTART`的定义，开始一直不明白初始化VMA时`v->start`要怎么获取，原来是自己定义。

## networking

content: complete `e1000_transmit()` and `e1000_recv()`, both in `kernel/e1000.c`, so that the driver can transmit and receive packets.

background:

（1）use a network device called the E1000 to handle network communication.

（2） `e1000_init()` provides the E1000 with **multiple buffers** into which the E1000 can write packets. The E1000 requires these buffers to be described by an array of "**descriptors**" in RAM; each descriptor contains an address in RAM where the E1000 can write a received packet(get buffers through desrciptors).

（3）`e1000_recv()` code must **scan the RX ring** and deliver each new packet's mbuf to the network stack (in `net.c`) by calling `net_rx()`.  Then allocate a new mbuf and place it into the descriptor, so that when the E1000 reaches that point in the RX ring again it finds a fresh buffer into which to DMA a new packet.

（4）When the network stack in `net.c` needs to send a packet, it calls `e1000_transmit()` with an mbuf that holds the packet to be sent. Your transmit code must **place a pointer to the packet data** in a descriptor in the TX (transmit) ring. 

（5）Transmit Descriptor Tail register (**TDT**): This register holds a value which is an offset from the base, and indicates the location beyond the last descriptor hardware can process. This is the location where software writes the first new descriptor.

（6）Legacy Transmit Descriptor Format（Section 3.3.3 in the E1000 manual）

```c
struct tx_desc
{
  uint64 addr;
  uint16 length;
  uint8 cso; // the Checksum Offset
  uint8 cmd; // Checksum Start
  uint8 status;
  uint8 css;
  uint16 special;
};
```

hint:

（1）implementing `e1000_transmit`. 跟着hint做就好，比较简单。

```c
int
e1000_transmit(struct mbuf *m)
{
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  // printf("trans: len: %x, buf: %s\n", m->len, m->head);
   // get the next packet form TX ring
  // printf("TDT: %d\n", regs[E1000_TDT]);
  // for(int i = 0; i < TX_RING_SIZE; i++){
  //   printf("tx_ring%d: addr: %x, length: %x, cso: %x, cmd: %x,"
  //       " status: %x, css: %x, special: %x\n", i, tx_ring[i].addr, tx_ring[i].length,
  //       tx_ring[i].cso, tx_ring[i].cmd, tx_ring[i].status, tx_ring[i].css, tx_ring[i].special);
  // }
  acquire(&e1000_lock);
  int ring_tail = regs[E1000_TDT];
  struct tx_desc tail = tx_ring[ring_tail];
  if((tail.status & E1000_TXD_STAT_DD) == 0){ // check if the the ring is overflowing
    release(&e1000_lock);
    panic("E1000_TXD_STAT_DD\n");
  }else{
    if(tx_mbufs[ring_tail]) // free the last mbuf that was transmitted from that descriptor
      mbuffree(tx_mbufs[ring_tail]);
  }

  tx_ring[ring_tail].addr = (uint64)m->head;
  tx_ring[ring_tail].length = (uint16)m->len;
   // 10011011, IDE = 1, VLE = 0, DEXT = 0, RPS = 1, RS = 1, IC = 0, IFCS = 1, EOP = 1
  tx_ring[ring_tail].cmd = 0x9B;
  // struct mbuf * stash = m;
  regs[E1000_TDT] = (ring_tail + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}
```

（2）implementing `e1000_recv`. 和`e1000_transmit`不同的是，这里RDT指向的是当前有效的descriptor，所以要+1才是空闲的descriptor，然后要循环rx_ring。

```c
static void
e1000_recv(void)
{
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  // printf("RDT: %d\n", (regs[E1000_RDT] + 1) % RX_RING_SIZE);
  // for(int i = 0; i < RX_RING_SIZE; i++){
  //   printf("rx_ring%d: addr: %x, length: %x, csum: %x, status: %x,"
  //       " errors: %x, special: %x\n", i, rx_ring[i].addr, rx_ring[i].length,
  //       rx_ring[i].csum, rx_ring[i].status, rx_ring[i].errors, rx_ring[i].special);
  // }
  int ring_head = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
  struct rx_desc head = rx_ring[ring_head];
  while(head.status & E1000_RXD_STAT_DD){ // scan the RX ring
    acquire(&e1000_lock);
    struct mbuf * buf = rx_mbufs[ring_head];
    buf->len += head.length; // update the mbuf's m->len
    rx_mbufs[ring_head] = mbufalloc(0); // allocate a new mbuf using mbufalloc()
    if (!rx_mbufs[ring_head])
      panic("e1000");
    rx_ring[ring_head].addr = (uint64) rx_mbufs[ring_head]->head;
    rx_ring[ring_head].status = 0;
    regs[E1000_RDT] = ring_head;
    release(&e1000_lock);
    net_rx(buf); // Deliver the mbuf to the network stack
    ring_head = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    head = rx_ring[ring_head];
  }
}
```

（3）结果

```
== Test running nettests == 
$ make qemu-gdb
(4.3s) 
== Test   nettest: ping == 
  nettest: ping: OK 
== Test   nettest: single process == 
  nettest: single process: OK 
== Test   nettest: multi-process == 
  nettest: multi-process: OK 
== Test   nettest: DNS == 
  nettest: DNS: OK 
== Test time == 
time: OK 
Score: 100/100
```

关键：这个实验比较简单，但是需要看的资料比较多，题目说的manual章节不需要所有细节都看，看个大概就行，最主要的的弄清楚存储数据用的是buf，而descriptor是用来描述buf的。所有涉及到这两部的章节要看，然后很多寄存器，如`E1000_RDT`，`E1000_TDT`等，要弄明白是干什么的。

## 注

（1）若代码跑不通，可以看我的[https://github.com/UtopianFuture/xv6-labs](https://github.com/UtopianFuture/xv6-labs)，上传了源码。

（2）xv6调试没有好的办法，遇到bug用printf打印出关键变量。


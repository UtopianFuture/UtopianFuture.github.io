# xv6

## sleep

​	system call会传入2个及以上的参数，第一个参数默认是system call的名称，第二个及之后的参数才是system call需要的参数，如sleep 10，10会作为参数传递给sleep system call。main函数中argc表示参数的数量，argv[]中存储参数。

​	每一个指令底层都是使用system call。sysproc.c提供了一系列的system call。

## Pingpong

(1) pipe(p) system call会创建一个管道，返回管道的输入输出到p[2]，p[0]是输入，p[1]是输出。

(2) write(fd, buf, sizeof(buf)) system call会将buf中的sizeof(buf)位写入到fd中，fd表示标准输出，可以是管道，也可以是其他的输出。

(3) read(fd, buf, sizeof(buf)) system call则是从fd中读取最多sizeof(buf)个字符到buf中，fd表示标准输入。可以通过pipe, read, write完成任意两个进程间的通讯。

(4) fork()会创建子进程，对于父进程返回进程的pid，对于子进程，返回0。但是如果子进程再创建子进程，则getpid()返回的结果是一样的，即不会再分配新的pid，不知道为什么这样设计。

## Primes

（1）开始我设想的是由父进程不断的产生自然数，产生一个素数就创建一个子进程，子进程又创建一个子进程，将下一个素数通过pipe传过去，这样不断的传，直到35，但是fork()对于子进程返回的pid是0，不知道怎么遍历所有的子进程。

（2）然后看网上的解法是一次产生所有的数，用两个数组作为pipe的读写缓冲。
需要注意的点是write, read的缓冲区都是char * 指针，如果传输的是非char类型，需要强制类型转化。如：

int p[512];
int temp = p[i];
write(p[1], (char * )(&temp), sizeof(char * ));

char buf[4];
read(p[0], buf, sizeof(char * ));
int p[i] == * (int * )buf;

（3）还有就是write之后要用close将pipe关闭，不然之后的read可能会一直等待结束。
（4）每个进程结束要用exit退出，不然会出现莫名其妙的bug。
（5）wait是等待子进程结束，试过如果不用wait会出现zombie，暂时不知道为什么。

## Find

​	这个实验走了弯路，正确的思路应该是用fstat获取文件状态后判断这个文件是文件还是文件夹，如果是文件则比较，然后输出，如果是文件夹则遍历其中的所有文件，然后递归。但要注意对于.和..文件不要遍历。

​	我之前是先遍历path中的文件。判断是否是文件夹，如果是文件夹在递归调用。但是没有及时的用fstat获取文件的状态，导致一直用当前文件夹的状态去判断，从而一直是T_DIR，不能对T_FILE进行判断。

## System call tracing

​	这个实验的思路比较简单，在syscall.c中添加printf指令即可。难点主要有以下几点：

（1）mask怎样传递。开始我以为要通过外部显式传递，但应该像sleep一样，在proc结构体中增加一个参数mask，然后通过argint()获取当前process的mask，关于这个函数的实现还要研究一下，很重要。

（2）trace的参数开始理解错了，以为直接是syscall的id，但应该是通过移位来判断对应的syscall是否打印。

然后关于进程执行，即proc结构体要仔细研究一下。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_395234dd23ef4425.png)  

## System info

​	需要详细了解的函数：mycpu(), myproc(), argaddr(), copyout()。同时，没有搞懂用户空间user.h中定义的system calls怎么传递到kernel中，调用kernel中的syscall。

​	sysproc.c和sysfile.c中定义所有的syscall，然后proc.c和file.c中定义了syscall的实现。

​	遇到的问题：

（1）在sysproc.c中定义的sys_sysinfo()函数怎样直接修改sysinfo结构体的值。很简单，就是正常的函数结构体，直接定义就好。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_6ac4060319193641.png)  
     
（2）mycpu()：通过调用cpuid()获取当前cpuid，而cpuid()中通过读取tp(thread pointer)寄存器得到core number（tp寄存器怎么处理有待下一步探究)。而cpus结构体变量中保存了所有的cpu上下文。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_3dd33f129662edbd.png)  
    
（3）myproc()是获取当前process的状态，即运行在当前cpu的process。

（4）argaddr()是获取对应寄存器的值。（当程序要从user写入数据到kernel中，获取到的寄存器就是保存地址指针？）

（5）copyout(): copy from kernel to user, copy len bytes from src to virtual address dstva in a given page table.（具体实现没看懂）

## Print a page table

​	开始想的是直接递归调用vmprint()，但是不知道level和pte index怎么解决，后来才知道要另外声明一个函数tbprint(pagetable_t pagetable, int level)，在vmprint()里调用，传入level，同时遍历所有的pte，index也就出来了，而不是我之前想的用PXMASK，PXSHIFT，PX宏来计算得到。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_e47e2522ed8c449e.png)  

## A kernel page table per process

​	“every process uses its own copy of the kernel page table when executing in the kernel.”这句话什么意思？每个进程在kernel时使用自己的kernel page table。这个kernel page table要怎么构造，进程怎么使用它。

（1）首先在proc结构体中声明一个kpagetable变量；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_d21e37d3c4ed43e7.png)  

（2）然后初始化这个pagetable，初始化的过程和kernel page table的过程一样；同时要重新设计一个函数ukvmmap()，用来将kpagetable的各个页映射到物理空间，这个函数和原来的kvmmap()类似；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_a1a6fc5acd609d4d.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_d8293489dc0b58aa.png)  

（3）在allocproc()中初始化这个kpagetable，和初始化原有的pagetable一样；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_3b387dc3f5be8ae0.png)  

（4）Make sure that each process's kernel page table has a mapping for that process's 	kernel stack.这个过程不理解，或者说不理解这个kstack是干什么用的。

​	***kstack***: When the process enters the kernel (for a system call or interrupt), the kernel code 	executes on the process’s kernel stack. 即process在kernel中执行时，kernel code执行在kernel stack中。

​	那么这个操作就是为kstack分配物理空间，然后映射，将va保存到p->stack中。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_f5f29ebd6ed2b25a.png)  

（5）在切换process前将kpagetable保存到stap寄存器中。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_1701dfc4aff607a9.png)  

（6）最后也是最麻烦的是free kpagetable。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_4b45ddbfda1b3c12.png)  


​	同理，和正常的pagetable释放过程类似，重新设计一个free函数——proc_freekpagetable()。稍有不同的是kpagetable在分配时和kernel pagetable一样，分配了UART0，VIRTIO0，PLIC，CLINT，kernel text，data，TRAMPOLINE，在free时都要一一释放掉。最后kstack也要单独free，这一点借鉴了网上的实现，自己想不到。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_db91ff9a54b18ad1.png)  

## Simplify copyin/copyinstr

​	未完成。

## Backtrace

难点有二：

（1）uint64 up = PGROUNDUP((uint64)s0); 设置stack pointer的上限，用来终止循环。

（2）指针的使用，uint64 *只是用来修改指针的格式，不改变指针的值。

## Alarm

（1）怎样计算n ticks

Every tick, the hardware clock forces an interrupt.即每个tick，cpu都会检查中断信息。

（2）kernel怎样调用alarmtest中的fn()。

保存了fn的地址，之后将pc置为这个地址即可。

这里注意是将epc保存在kernel stack。这个是错误的，保存在哪里不重要，只要保存下来就好，可以在proc中定义变量来保存，将整个trapframe都保存下来，之后再恢复。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_54e33df27e87ce0f.png)  

## lazy page allocation

​	这个实验想要传递的系统设计思想是在进程申请内存时并不直接分配物理内存，而是只更新进程需要的内存空间，之后等进程需要访问物理内存时触发page fault，通过usertrap和walkaddr来为进程分配物理空间。以下几点需要注意：

（1）由于开始没有分配物理内存，也就没有va->pa的映射， 所以遇到这映射的判断时不要触发panic，而是直接检查下一个映射。

（2）检查va是否有效。

1. a. 是否超过了内存需要的地址空间；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_ab3ea537ba22377f.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_9eef094af338e13d.png)  
       
处理栈溢出；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_911ac2099945c493.png)  

关于os的page fault有了新的认知，即重新分配内存，然后mapping。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_63cd687600da8253.png)  

## Implement copy-on write

​	COW defers allocating and copying physical memory pages for the child until the copies are actually needed。即fork()原本在创建子进程时需要将所有的父进程的内容复制到子进程的进程空间，但是这个很耗时，因此想到子进程和父进程公用进程空间，当子进程和父进程要写某个page时触发page fault，这时再kalloc()，将该page的内容复制到新的page，更新pte。

（1）在uvmcopy()中将父进程的该页的pte置为PTE_COW和(~PTE_W)，不需要kalloc()， map的时候还是用父进程的pa。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_94731780871b1e56.png)  

（2）定义一个count变量来确定每一个page的使用情况，以及对应的操作函数。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_15eff0f465b79375.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_4ba5f2c142ba10bb.png)  

（3）在kalloc()初始化时将对应page的count置为1；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_d022abea0d6b8ab5.png)  

kfree()时减1。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_3a8eee769c163057.png)

（4）这里在usertrap和copyout时都会遇到PTE_COW的页，都需要进行上述处理，因此将理过程封装成函数，与usertrap和copyout解耦（这种写法以后多用，改bug能方便很多）。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_714862dfb3c578d9.png)  

## Uthread: switching between threads

​	content: design the context switch mechanism for a user-level threading system, and then implement it.

（1）创建进程；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_1a15f515159e6140.png)  

​	主要是创建context，然后将返回地址保存到ra寄存器中，将堆栈地址保存在sp寄存器中。但为什么堆栈地址要保存成(uint64)(t→stack + STACK_SIZE)还不清楚，试过保存(uint64)(t→stack)，这样就不能正常的switch，只执行了thread_c。

​       在手册中有这样一句话：caller-saved registers are saved on the stack (if needed) by the calling C code. 也就是说t->context.sp = (uint64)(t->stack + STACK_SIZE);是保存caller的registers的，如果不加上STACK_SIZE，那么就没有保存caller的registers，之后执行还是在第一个线程的stack中执行，这就是为什么不加STACK_SIZE执行只print thread_c的内容。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_88ed7e494a5d8832.png)  

（2）进程切换；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_b2abe2b0f725b042.png)  
     
![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_ebf32f6fd83731b5.png)  

## Using threads

​	context: explore parallel programming with threads and locks using a hash table.

​	首先是线程的互斥问题导致的hash miss，那么加锁，让不同的线程互斥访问即可。

（1）声明锁；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_b8c62b0c60c0c280.png)  

（2）初始化，注意在main()中初始化一次即可，所有的thread用同一个锁；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_3eb4bd51561084fe.png)  

（3）给put, get加锁；

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_d840a09249383b12.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_eb1e41e03fb2cbab.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_3c5d86bacbbd1cea.png)  

但是2个thread并没有比1个thread快两倍，因为两个thread并不是并行插入hash table的，而是一个thread写前半部分，一个thread写后半部分，然后get的时候get两个thread都get一次。那么可以只给会造成冲突的部分加锁，即insert部分，其他部分两个thread同时访问。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_131544fc665b5c7a.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_805edb6ce977f6a2.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_4299285a0c3b808.png)  

## Barrier

​	Barrier: a point in an application at which all participating threads must wait until all other participating threads reach that point too.

​	首先明确函数功能：

​	The pthread_cond_wait() and pthread_cond_timedwait() functions are used to block on a condition variable. These functions **atomically release mutex** and cause the calling thread to block on the condition variable cond.

​	The pthread_cond_signal() call unblocks **at least one of the threads** that are blocked on the specified condition variable cond (if any threads are blocked on cond).

​	The pthread_cond_broadcast() call unblocks **all threads** currently blocked on the specified condition variable cond.

（1）编写barrier()函数，在函数开始时加锁，如果当前进程是最后一个进程，

​		*bstate.nthread == nthread*

则唤醒所有block的进程，如果不是，则pthread_cond_wait()，这个函数会释放barrier_mutex锁，所以第33行不需要释放barrier_mutex，这是开始犯的一个错误，没有仔细看手册。

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_7b41ee214a2cc346.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_bba6f3731cab1b0c.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_18f1c2c9dbcbaa9b.png)  

![img](file:///tmp/lu6741cyqsq7.tmp/lu6741cyr06i_tmp_7f44593c6b9d9bf2.png)  
（2）还有这两个变量，一个是互斥变量，一个是条件变量。

## Memory allocator





## Buffer cache
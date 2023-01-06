## BPF

很早就听 eBPF 这个东西很有意思，但那会我还太菜，需要学的东西太多，没时间看这个。现在秋招告一段落，可以多花点时间来看看这个。我对 eBPF 最初的印像来源于这个[网站](https://www.brendangregg.com/bpf-performance-tools-book.html)和这张[图片](https://www.brendangregg.com/BPF/bpf_performance_tools_book.png)。

很多中文翻译的书没必要看，越看越迷糊，直接看这个[网站](https://ebpf.io/what-is-ebpf/)，eBPF 的前应后果一清二楚。

网上关于 eBPF 的资料很多，头昏眼花，无从下手，推荐 [Brendan Gregg](https://www.brendangregg.com/blog/2019-01-01/learn-ebpf-tracing.html)，他的博客写了从入门到高级应该怎么学。这里先搞懂 bcc 中 11 个 BPF 程序。

所有的 bpf 工具如下图所示，

![bpf tool](https://www.brendangregg.com/Perf/bcc_tracing_tools.png)

### execsnoop

首要要学习很多 python 的东西，不然代码看不懂。

在 python 中经常见到如下代码，

```python
import argparse

parser = argparse.ArgumentParser(
    description="Trace exec() syscalls",
    formatter_class=argparse.RawDescriptionHelpFormatter,
    epilog=examples)
parser.add_argument("-T", "--time", action="store_true",
    help="include time column on output (HH:MM:SS)")

	...

args = parser.parse_args()
```

这个是命令行解析模块。平时看到的 `man -h` 之类的 help 信息就是通过这种方式展示出来，最后的 args 就是从命令行得到的参数。具体的介绍可以看[这里](https://docs.python.org/zh-cn/3/howto/argparse.html)，解释的很清楚。

根据介绍，execsnoop 是跟踪 `execve` 系统调用的，能够给出 PID、PPID 和详细的调用命令。`bpf_text = """ ... """` 中就是 BPF 源码。

```c
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>
#include <linux/fs.h>

#define ARGSIZE  128

enum event_type {
    EVENT_ARG,
    EVENT_RET,
};

struct data_t {
    u32 pid;  // PID as in the userspace term (i.e. task->tgid in kernel)
    u32 ppid; // Parent PID as in the userspace term (i.e task->real_parent->tgid in kernel)
    u32 uid;
    char comm[TASK_COMM_LEN];
    enum event_type type;
    char argv[ARGSIZE];
    int retval;
};

BPF_PERF_OUTPUT(events); // 这个是 bcc 提供的工具，在 docs/reference_guide.md 中有详细的说明

static int __submit_arg(struct pt_regs *ctx, void *ptr, struct data_t *data)
{
    // 先将数据从用户态复制到 BPF stack，再通过 perf_submit 返回到用户态
    bpf_probe_read_user(data->argv, sizeof(data->argv), ptr);
    events.perf_submit(ctx, data, sizeof(struct data_t));
    return 1;
}

static int submit_arg(struct pt_regs *ctx, void *ptr, struct data_t *data)
{
    const char *argp = NULL;
    bpf_probe_read_user(&argp, sizeof(argp), ptr);
    if (argp) {
        return __submit_arg(ctx, (void *)(argp), data);
    }
    return 0;
}

int syscall__execve(struct pt_regs *ctx,
    const char __user *filename, // 这些参数和内核中的 execve 系统调用是一样的
    const char __user *const __user *__argv,
    const char __user *const __user *__envp)
{

    u32 uid = bpf_get_current_uid_gid() & 0xffffffff;

    UID_FILTER // 这个包括后面的 PPID_FILTER 在 python 代码中都会被替换（还能这样写？）

    if (container_should_be_filtered()) {
        return 0;
    }

    // create data here and pass to submit_arg to save stack space (#555)
    struct data_t data = {}; // data 应该就是要返回的数据
    struct task_struct *task;

    data.pid = bpf_get_current_pid_tgid() >> 32;

    task = (struct task_struct *)bpf_get_current_task();
    // Some kernels, like Ubuntu 4.13.0-generic, return 0
    // as the real_parent->tgid.
    // We use the get_ppid function as a fallback in those cases. (#1883)
    data.ppid = task->real_parent->tgid;

    PPID_FILTER

    bpf_get_current_comm(&data.comm, sizeof(data.comm));
    data.type = EVENT_ARG;

    __submit_arg(ctx, (void *)filename, &data);

    // skip first arg, as we submitted filename
    #pragma unroll
    for (int i = 1; i < MAXARG; i++) {
        if (submit_arg(ctx, (void *)&__argv[i], &data) == 0)
             goto out;
    }

    // handle truncated argument list
    char ellipsis[] = "...";
    __submit_arg(ctx, (void *)ellipsis, &data);
out:
    return 0;
}

int do_ret_sys_execve(struct pt_regs *ctx)
{
    if (container_should_be_filtered()) {
        return 0;
    }

    struct data_t data = {};
    struct task_struct *task;

    u32 uid = bpf_get_current_uid_gid() & 0xffffffff;
    UID_FILTER

    data.pid = bpf_get_current_pid_tgid() >> 32;
    data.uid = uid;

    task = (struct task_struct *)bpf_get_current_task();
    // Some kernels, like Ubuntu 4.13.0-generic, return 0
    // as the real_parent->tgid.
    // We use the get_ppid function as a fallback in those cases. (#1883)
    data.ppid = task->real_parent->tgid;

    PPID_FILTER

    bpf_get_current_comm(&data.comm, sizeof(data.comm));
    data.type = EVENT_RET;
    data.retval = PT_REGS_RC(ctx);
    events.perf_submit(ctx, &data, sizeof(data));

    return 0;
}
```

这里涉及一些 BPF 提供的工具，需要熟练使用，使用实例都可以从上面的代码中找到。

- BPF_PERF_OUTPUT

  Creates a BPF table for **pushing out custom event data to user space** via a perf ring buffer. This is the preferred method for pushing per-event data to user space.

  即将我们需要的内核信息返回到用户态，但是通过 "a perf ring buffer" 是啥意思？

- perf_submit

  A method of a `BPF_PERF_OUTPUT` table, for **submitting custom event data to user space**. See the `BPF_PERF_OUTPUT` entry. This ultimately calls `bpf_perf_event_output()` system call.

- get_syscall_fnname

  **Return the corresponding kernel function name of the syscall**. This helper function will try different prefixes and use the right one to concatenate with the syscall name.

- attach_kprobe

  ```Python
  b.attach_kprobe(event="sys_clone", fn_name="do_trace")
  ```

  This will instrument the kernel `sys_clone()` function, which will then run our BPF defined ```do_trace()``` function each time it is called.

  You can call attach_kprobe() more than once, and attach your BPF function to multiple kernel functions and also call attach_kprobe() more than once to attach multiple BPF functions to the same kernel function.

- attach_kretprobe

  Instruments the return of the kernel function ```event()``` using kernel dynamic tracing of the function return, and attaches our C defined function ```name()``` to be called when the kernel function returns.

  ```Python
  b.attach_kretprobe(event="vfs_read", fn_name="do_return")
  ```

  This will instrument the kernel ```vfs_read()``` function, which will then run our BPF defined ```do_return()``` function each time it is called.

- open_perf_buffer

  This operates on a table as defined in BPF as `BPF_PERF_OUTPUT()`, and associates the callback **Python function ```callback``` to be called when data is available in the perf ring buffer**. This is part of the recommended mechanism for transferring per-event data from kernel to user space. The size of the perf ring buffer can be specified via the ```page_cnt``` parameter, which must be a power of two number of pages and defaults to 8. If the callback is not processing data fast enough, some submitted data may be lost. ```lost_cb``` will be called to log / monitor the lost count. If ```lost_cb``` is the default ```None``` value, it will just print a line of message to ```stderr```.

- perf_buffer_poll

  This polls from all open perf ring buffers, calling the callback function that was provided when calling open_perf_buffer for each entry.

  ```Python
  # loop with callback to print_event
  // 经典用法，当数据在 perf ring buffer 可用是，就执行回到函数 print_event
  b["events"].open_perf_buffer(print_event)
  while 1:
      try:
          b.perf_buffer_poll()
      except KeyboardInterrupt:
          exit();
  ```

- bpf_probe_read_user

  This attempts to safely **read size bytes from user address space to the BPF stack**, so that BPF can later operate on it. For safety, all user address space memory reads must pass through `bpf_probe_read_user()`.

就目前看到的，BPF 确实能够帮助分析内核，所以最终目标就是学会根据自己的需求编写 BPF 程序。

### opensnoop

从图中来看，execsnoop 和 opensnoop 都是跟踪系统调用的，为什么 BPF 程序完全不一样呢？

其是并没有不一样，只是将程序分开来写，

```python
    bpf_text += bpf_text_kfunc_header_open.replace('FNNAME', fnname_open)
    bpf_text += bpf_text_kfunc_body

    bpf_text += bpf_text_kfunc_header_openat.replace('FNNAME', fnname_openat)
    bpf_text += bpf_text_kfunc_body
```

因为 open 系统调用有 `open`、`openat` 和 `openat2` 三种形式，需要分开考虑。python 好方便啊！！！

- BPF_HASH

  Creates a hash map (associative array) named ```name```, with optional parameters.

  Defaults: ```BPF_HASH(name, key_type=u64, leaf_type=u64, size=10240)```

  For example:

  ```C
  BPF_HASH(start, struct request *);
  ```

  This creates a hash named ```start``` where the key is a ```struct request *```, and the value defaults to u64.

  map 能够让数据在用户态和内核态之间来回传送。而数据是通过 key 去索引的，所以将其称之为 map。

- kretfuncs

  This is a macro that instruments the kernel function via trampoline *after* the function is executed.

  For example:
  ```C
  KRETFUNC_PROBE(do_sys_open, int dfd, const char *filename, int flags, int mode, int ret)
  {
      ...
  ```

  This instruments the `do_sys_open` kernel function and **make its arguments accessible as standard argument values together with its return value**.

我们以 `open` 系统调用为例分析，

```C
#include <uapi/linux/ptrace.h>
#include <uapi/linux/limits.h>
#include <linux/sched.h>
#ifdef FULLPATH
#include <linux/fs_struct.h>
#include <linux/dcache.h>

#define MAX_ENTRIES 32

enum event_type {
    EVENT_ENTRY,
    EVENT_END,
};
#endif

struct val_t {
    u64 id;
    char comm[TASK_COMM_LEN];
    const char *fname;
    int flags; // EXTENDED_STRUCT_MEMBER
};

struct data_t {
    u64 id;
    u64 ts;
    u32 uid;
    int ret;
    char comm[TASK_COMM_LEN];
#ifdef FULLPATH
    enum event_type type;
#endif
    char name[NAME_MAX];
    int flags; // EXTENDED_STRUCT_MEMBER
};

BPF_PERF_OUTPUT(events);
BPF_HASH(infotmp, u64, struct val_t); // 不用 HASH，用 execsnoop 中的方法可以么

int trace_return(struct pt_regs *ctx)
{
    u64 id = bpf_get_current_pid_tgid();
    struct val_t *valp;
    struct data_t data = {};

    u64 tsp = bpf_ktime_get_ns();

    valp = infotmp.lookup(&id);
    if (valp == 0) {
        // missed entry
        return 0;
    }

    bpf_probe_read_kernel(&data.comm, sizeof(data.comm), valp->comm);
    bpf_probe_read_user_str(&data.name, sizeof(data.name), (void *)valp->fname);
    data.id = valp->id;
    data.ts = tsp / 1000;
    data.uid = bpf_get_current_uid_gid();
    data.flags = valp->flags; // EXTENDED_STRUCT_MEMBER
    data.ret = PT_REGS_RC(ctx);

    events.perf_submit(ctx, &data, sizeof(data)); // 将数据返回到用户态

    infotmp.delete(&id);

    return 0;
}

// 为什么和 open 系统调用不一样？
/*
 * SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode)
 */
int syscall__trace_entry_open(struct pt_regs *ctx, const char __user *filename, int flags)
{
    struct val_t val = {};
    u64 id = bpf_get_current_pid_tgid();
    u32 pid = id >> 32; // PID is higher part
    u32 tid = id;       // Cast and get the lower part
    u32 uid = bpf_get_current_uid_gid();

    PID_TID_FILTER // 这些在 python 源码中会替换掉
    UID_FILTER
    FLAGS_FILTER

    if (container_should_be_filtered()) {
        return 0;
    }

    if (bpf_get_current_comm(&val.comm, sizeof(val.comm)) == 0) {
        val.id = id;
        val.fname = filename;
        val.flags = flags; // EXTENDED_STRUCT_MEMBER
        infotmp.update(&id, &val); // 放入到 map 中
    }

    return 0;
};
```

然后调用 `attach_kprobe` 将这些函数 attach 到 `open` 系统调用上，

```python
# initialize BPF
b = BPF(text=bpf_text)
b.attach_kprobe(event=fnname_open, fn_name="syscall__trace_entry_open")
b.attach_kretprobe(event=fnname_open, fn_name="trace_return")
```

### ext4slower

### Reference

[1] https://www.brendangregg.com/bpf-performance-tools-book.html

[2] Linux 内核观测技术 BPF. David Calavera 等著，范彬等译

[3] https://cloud.tencent.com/developer/article/1698426 资料汇总

[4] https://ebpf.io/what-is-ebpf/

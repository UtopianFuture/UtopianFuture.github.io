## SYSCALL - execve

在这里仔细分析一下 `do_execve` 是怎样加载用户态程序的。

##### linux_binprm

内核使用 `linux_binprm` 结构体表示一个可执行文件。

```c
/*
 * This structure is used to hold the arguments that are used when loading binaries.
 */
struct linux_binprm {
	char buf[BINPRM_BUF_SIZE]; // 保存可执行文件的前 128 个字节
#ifdef CONFIG_MMU
	struct vm_area_struct *vma; // 哈，这个就很熟悉了
	unsigned long vma_pages;
#else
# define MAX_ARG_PAGES	32
	struct page *page[MAX_ARG_PAGES];
#endif
	struct mm_struct *mm; // 进程的地址空间描述符
	unsigned long p; /* current top of mem */ // 当前内存页最高地址（？）
	unsigned int
		cred_prepared:1,/* true if creds already prepared (multiple
				 * preps happen for interpreters) */
		cap_effective:1;/* true if has elevated effective capabilities,
				 * false if not; except for init which inherits
				 * its parent's caps anyway */

	unsigned int recursion_depth; /* only for search_binary_handler() */
	struct file * file; // 要执行的文件
	struct cred *cred;	/* new credentials */
	int unsafe;		/* how unsafe this exec is (mask of LSM_UNSAFE_*) */
	unsigned int per_clear;	/* bits to clear in current->personality */
	int argc, envc; // 命令行参数和环境变量数目
	const char * filename;	/* Name of binary as seen by procps */
	const char * interp;	/* Name of the binary really executed. Most
				   of the time same as filename, but could be
				   different for binfmt_{misc,script} */
	unsigned interp_flags;
	unsigned interp_data;
	unsigned long loader, exec;
};
```

##### linux_binfmt

内核对所支持的每种可执行的程序类型都用 `linux_binfmt` 表示，

```c
/*
 * This structure defines the functions that are used to load the binary formats that
 * linux accepts.
 */
struct linux_binfmt {
	struct list_head lh;
	struct module *module;
	int (*load_binary)(struct linux_binprm *);
	int (*load_shlib)(struct file *);
	int (*core_dump)(struct coredump_params *cprm);
	unsigned long min_coredump;	/* minimal dump size */
};
```

并提供了 3 种方法来加载和执行可执行程序：

- load_binary

通过读存放在可执行文件中的信息为当前进程建立一个新的执行环境；

- load_shlib

用于动态的把一个共享库捆绑到一个已经在运行的进程，这是由 `uselib` 系统调用激活的；

- core_dump

在名为 core 的文件中，存放当前进程的执行上下文。这个文件通常是在进程接收到一个缺省操作为”dump”的信号时被创建的，其格式取决于被执行程序的可执行类型；

所有的 `linux_binfmt` 对象都处于一个链表中，可以通过调用 `register_binfmt` 和 `unregister_binfmt` 函数在链表中插入和删除元素。在系统启动期间，为每个编译进内核的可执行文件都执行 `registre_fmt` 函数。当实现了一个新的可执行格式的模块正被装载时，也执行这个函数，当模块被卸载时，执行`unregister_binfmt` 函数。

#### 关键函数do_execveat_common

```c
/*
 * sys_execve() executes a new program.
 */
static int do_execveat_common(int fd, struct filename *filename,
			      struct user_arg_ptr argv,
			      struct user_arg_ptr envp,
			      int flags)
{
	char *pathbuf = NULL;
	struct linux_binprm *bprm;
	struct file *file;
	struct files_struct *displaced;
	int retval;

	...

	/* We're below the limit (still or again), so we don't want to make
	 * further execve() calls fail. */
	current->flags &= ~PF_NPROC_EXCEEDED;

	retval = unshare_files(&displaced); // 为进程复制一份文件表

	retval = -ENOMEM;
	bprm = kzalloc(sizeof(*bprm), GFP_KERNEL); // 分配一个 linux_binprm 结构体

	retval = prepare_bprm_creds(bprm);

	check_unsafe_exec(bprm);
	current->in_execve = 1;

	file = do_open_execat(fd, filename, flags); // 查找并打开二进制文件
	retval = PTR_ERR(file);

	sched_exec(); // 找到最小负载的CPU，用来执行该二进制文件

    // 配置 linux_binprm 结构体中的 file、filename、interp 成员
	bprm->file = file;
	if (fd == AT_FDCWD || filename->name[0] == '/') {
		bprm->filename = filename->name;
	} else {
		if (filename->name[0] == '\0')
			pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d", fd);
		else
			pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d/%s",
					    fd, filename->name);
		if (!pathbuf) {
			retval = -ENOMEM;
			goto out_unmark;
		}
		/*
		 * Record that a name derived from an O_CLOEXEC fd will be
		 * inaccessible after exec. Relies on having exclusive access to
		 * current->files (due to unshare_files above).
		 */
		if (close_on_exec(fd, rcu_dereference_raw(current->files->fdt)))
			bprm->interp_flags |= BINPRM_FLAGS_PATH_INACCESSIBLE;
		bprm->filename = pathbuf;
	}
	bprm->interp = bprm->filename;

    // 创建进程的内存地址空间，为新程序初始化内存管理，并调用 init_new_context
    // 检查当前进程是否使用自定义的局部描述符表；如果是，那么分配和准备一个新的 LDT
	retval = bprm_mm_init(bprm);

    // 配置 argc、envc 成员
	bprm->argc = count(argv, MAX_ARG_STRINGS);

	bprm->envc = count(envp, MAX_ARG_STRINGS);

    // 检查该二进制文件的可执行权限
	retval = prepare_binprm(bprm);

    // 从内核空间获取二进制文件的路径名称
	retval = copy_strings_kernel(1, &bprm->filename, bprm);

    // 从用户空间拷贝环境变量和命令行参数
	bprm->exec = bprm->p;
	retval = copy_strings(bprm->envc, envp, bprm);

	retval = copy_strings(bprm->argc, argv, bprm);

	would_dump(bprm, bprm->file);

    // 至此，二进制文件已经被打开，linux_binprm 中也记录了重要信息
    // 内核开始调用 exec_binprm 执行可执行程序
	retval = exec_binprm(bprm);

	/* execve succeeded */
	current->fs->in_exec = 0;
	current->in_execve = 0;
	acct_update_integrals(current);
	task_numa_free(current);
	free_bprm(bprm);
	kfree(pathbuf);
	putname(filename);
	if (displaced)
		put_files_struct(displaced);
	return retval;

	...
}
```

内核中实际执行 `execv` 或 `execve` 系统调用的程序是 `do_execve`，这个函数先打开目标映像文件，读取目标文件的 ELF 头，然后调用另一个函数 `search_binary_handler`，在此函数里面，它会搜索我们上面提到的内核支持的可执行文件类型队列，让各种可执行程序的处理程序前来认领和处理。如果类型匹配，则调用 `load_binary` 函数指针所指向的处理函数来处理目标映像文件。在ELF文件格式中，处理函数是 `load_elf_binary` 函数，下面主要就是分析 `load_elf_binary` 函数的执行过程。

#### 关键函数exec_binprm

该函数用来执行可执行文件，不过它的主要任务还是调用 `search_binary_handler`。

```c
static int exec_binprm(struct linux_binprm *bprm)
{
	pid_t old_pid, old_vpid;
	int ret;

	/* Need to fetch pid before load_binary changes it */
	old_pid = current->pid;
	rcu_read_lock();
	old_vpid = task_pid_nr_ns(current, task_active_pid_ns(current->parent));
	rcu_read_unlock();

	ret = search_binary_handler(bprm);
	if (ret >= 0) {
		audit_bprm(bprm);
		trace_sched_process_exec(current, old_pid, bprm);
		ptrace_event(PTRACE_EVENT_EXEC, old_vpid);
		proc_exec_connector(current);
	}

	return ret;
}
```

### Reference

[1] https://blog.csdn.net/gatieme/article/details/51594439

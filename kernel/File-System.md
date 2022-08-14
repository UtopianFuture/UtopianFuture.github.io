## File System

文件系统我觉得是三个部分中最难的，可能是因为《奔跑吧 linux》降低了学习成本。但我也觉得这部分非常有用，有意思，在开发过程中，常常不能理解各种挂载，格式化操作，所以系统的学习这部分。

### 虚拟文件系统

虚拟文件系统（Virtual Filesystem Switch）所隐含的思想是把表示很多不同种类文件系统的共同信息放入内核，其中有一个字段或函数来支持内核所支持的所有实际文件系统所提供的任何操作。对所调用的每个读、写或者其他函数，内核都能将其替换成本地文件系统的实际函数。

VFS 支持的文件系统可以分成三种主要类型：

- 磁盘文件系统。这些文件系统（Ext2, Ext3, System V, UFS, NTFS 等）管理在本地磁盘分区中可用的存储空间或其他可以起到磁盘作用的设备（如 USB 闪存）
- 网络文件系统。这些文件系统允许轻易的访问属于其他网络计算机的文件系统所包含的文件。如：NFS, AFS, CIFS 等。
- 特殊文件系统。这些文件系统不管理本地或者远程磁盘空间。/proc 是一个典型的特殊文件系统。

#### 通用文件模型

VFS 为了支持尽可能多的文件系统，引入了一个通用的文件模型，要实现每个具体的文件系统，必须将其物理组织结构转换为虚拟文件系统的通用文件模型。

应用程序对 `read()` 的调用引起内核调用相应的 `sys_read()` 系统调用，而文件在内核中是用 `struct file` 表示的，`file` 中包含成员变量 `f_op`，该成员变量包含指向实际文件系统的函数指针，包括读写文件的函数，`sys_read()` 找到该函数的指针，并调用它。

通用文件模型由下列对象类型组成：

- 超级块对象（superblock object）

  存放以安装文件系统的有关信息。基于磁盘的文件系统，这类对象通常对应于存放在磁盘上的文件系统控制块（filesystem control block）。在内核中的数据结构为 [super_block](#super_block)。

- 索引节点对象（inode object）

  存放具体文件的信息，每个索引节点都有一个索引节点号，**这个节点号唯一地表示文件系统中的文件**。在内核中的数据结构为 [inode](#inode)。

- 文件对象（file object）

  存放打开文件域进程之间进行交互的有关信息，这类信息仅当进程访问文件期间存在内核内存中。在内核中的数据结构为 [files_struct](#files_struct)。

- 目录项对象（dentry object）

  存放目录项（文件的特定名称）域对应文件进行链接的有关信息。在内核中的数据结构为 [dentry](#dentry)。

下面我们看看进程怎样与文件进行交互。三个不同的进程已经打开同一个文件，其中两个进程使用同一个硬链接，这样每个进程都有自己的文件对象，但只需要两个目录项对象，每个硬链接对应一个目录项，这两个目录项指向同一个索引节点，该索引节点表示超级块以及随后的普通磁盘文件。

![process-VFS.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/process-VFS.png?raw=true)

### VFS 的数据结构

这节介绍 VFS 相关的数据结构及其关系。这些数据结构都是通过 slub 描述符分配内存空间。我们先看看整体的关系图。

![VFS.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/VFS.png?raw=true)

#### super_block

```c
struct super_block {
	struct list_head	s_list;		/* Keep this first */ // 指向超级块链表的指针
	dev_t			s_dev;		/* search index; _not_ kdev_t */ // 设备标识符
	unsigned char		s_blocksize_bits; // 以位为单位的块大小
	unsigned long		s_blocksize; // 以字节为单位的块大小
	loff_t			s_maxbytes;	/* Max file size */
	struct file_system_type	*s_type; // 文件系统的类型
	const struct super_operations	*s_op; // 超级块方法
	const struct dquot_operations	*dq_op; // 磁盘限额处理方法
	const struct quotactl_ops	*s_qcop; // 磁盘限额管理方法（？）
	const struct export_operations *s_export_op; // 网络文件系统使用的输出操作
	unsigned long		s_flags; // 安装标志
	unsigned long		s_iflags;	/* internal SB_I_* flags */
	unsigned long		s_magic; // 文件系统的魔数
	struct dentry		*s_root; // 文件系统根目录的目录项对象
	struct rw_semaphore	s_umount; // 卸载使用的信号量
	int			s_count; // 引用计数器
	atomic_t		s_active; // 此即引用计数器
#ifdef CONFIG_SECURITY
	void                    *s_security;
#endif
	const struct xattr_handler **s_xattr; // 扩展属性结构
#ifdef CONFIG_FS_ENCRYPTION
	const struct fscrypt_operations	*s_cop;
	struct key		*s_master_keys; /* master crypto keys in use */
#endif
#ifdef CONFIG_FS_VERITY
	const struct fsverity_operations *s_vop;
#endif
#ifdef CONFIG_UNICODE
	struct unicode_map *s_encoding;
	__u16 s_encoding_flags;
#endif
	struct hlist_bl_head	s_roots;	/* alternate root dentries for NFS */
	struct list_head	s_mounts;	/* list of mounts; _not_ for fs use */
	struct block_device	*s_bdev;
	struct backing_dev_info *s_bdi;
	struct mtd_info		*s_mtd;
	struct hlist_node	s_instances;
	unsigned int		s_quota_types;	/* Bitmask of supported quota types */
	struct quota_info	s_dquot;	/* Diskquota specific options */

	struct sb_writers	s_writers;

	/*
	 * Keep s_fs_info, s_time_gran, s_fsnotify_mask, and
	 * s_fsnotify_marks together for cache efficiency. They are frequently
	 * accessed and rarely modified.
	 */
	void			*s_fs_info;	/* Filesystem private info */ // 指向特定文件系统的超级块信息的指针

	/* Granularity of c/m/atime in ns (cannot be worse than a second) */
	u32			s_time_gran; // 时间戳粒度
	/* Time limits for c/m/atime in seconds */
	time64_t		   s_time_min;
	time64_t		   s_time_max;
#ifdef CONFIG_FSNOTIFY
	__u32			s_fsnotify_mask;
	struct fsnotify_mark_connector __rcu	*s_fsnotify_marks;
#endif

	char			s_id[32];	/* Informational name */
	uuid_t			s_uuid;		/* UUID */

	unsigned int		s_max_links;
	fmode_t			s_mode;

	/*
	 * The next field is for VFS *only*. No filesystems have any business
	 * even looking at it. You had been warned.
	 */
	struct mutex s_vfs_rename_mutex;	/* Kludge */

	/*
	 * Filesystem subtype.  If non-empty the filesystem type field
	 * in /proc/mounts will be "type.subtype"
	 */
	const char *s_subtype;

	const struct dentry_operations *s_d_op; /* default d_op for dentries */

	/*
	 * Saved pool identifier for cleancache (-1 means none)
	 */
	int cleancache_poolid;

	struct shrinker s_shrink;	/* per-sb shrinker handle */

	/* Number of inodes with nlink == 0 but still referenced */
	atomic_long_t s_remove_count;

	/*
	 * Number of inode/mount/sb objects that are being watched, note that
	 * inodes objects are currently double-accounted.
	 */
	atomic_long_t s_fsnotify_connectors;

	/* Being remounted read-only */
	int s_readonly_remount;

	/* per-sb errseq_t for reporting writeback errors via syncfs */
	errseq_t s_wb_err;

	/* AIO completions deferred from interrupt context */
	struct workqueue_struct *s_dio_done_wq;
	struct hlist_head s_pins;

	/*
	 * Owning user namespace and default context in which to
	 * interpret filesystem uids, gids, quotas, device nodes,
	 * xattrs and security labels.
	 */
	struct user_namespace *s_user_ns;

	/*
	 * The list_lru structure is essentially just a pointer to a table
	 * of per-node lru lists, each of which has its own spinlock.
	 * There is no need to put them into separate cachelines.
	 */
	struct list_lru		s_dentry_lru;
	struct list_lru		s_inode_lru;
	struct rcu_head		rcu;
	struct work_struct	destroy_work;

	struct mutex		s_sync_lock;	/* sync serialisation lock */

	/*
	 * Indicates how deep in a filesystem stack this SB is
	 */
	int s_stack_depth;

	/* s_inode_list_lock protects s_inodes */
	spinlock_t		s_inode_list_lock ____cacheline_aligned_in_smp;
	struct list_head	s_inodes;	/* all inodes */ // 所有索引节点的链表

	spinlock_t		s_inode_wblist_lock;
	struct list_head	s_inodes_wb;	/* writeback inodes */
} __randomize_layout;
```

`s_fs_info` 成员变量指向特定文件系统的超级块信息，如果使用的实际文件系统是 Ext2，那么 `s_fs_info` 指向 `ext2_sb_info`，该结构包含磁盘分配位掩码和其他与 VFS 通用文件模型无关的数据。

通常为了效率，`s_fs_info` 指向的数据结构会被复制到内存中，任何基于磁盘的文件系统都需要读写自己的磁盘分配位图，VFS 允许这些文件系统直接对 `s_fs_info` 进行操作，而无需访问磁盘。这样就需要引用 `s_dirt` 位来表示该超级块是否是脏的，这个在之后再分析。

#### inode

```c
struct inode {
	umode_t			i_mode; // 文件类型域访问权限
	unsigned short		i_opflags;
	kuid_t			i_uid; // 所属哪个进程
	kgid_t			i_gid; // 所属哪个组
	unsigned int		i_flags; // 文件系统的安装标志

	...

	const struct inode_operations	*i_op;
	struct super_block	*i_sb; // 这个很好理解，该索引节点所属的超级块
	struct address_space	*i_mapping; // address_space 对象指针（？）

	...

	/* Stat data, not accessed from path walking */
	unsigned long		i_ino; // 索引节点号
	/*
	 * Filesystems may only read i_nlink directly.  They shall use the
	 * following functions for modification:
	 *
	 *    (set|clear|inc|drop)_nlink
	 *    inode_(inc|dec)_link_count
	 */
	union {
		const unsigned int i_nlink; // 硬链接数目
		unsigned int __i_nlink;
	};
	dev_t			i_rdev; // 实设备标识符
	loff_t			i_size; // 文件的字节数
	struct timespec64	i_atime; // 上次访问文件的时间
	struct timespec64	i_mtime; // 上次写文件的时间
	struct timespec64	i_ctime; // 上次修改索引节点的时间
	spinlock_t		i_lock;	/* i_blocks, i_bytes, maybe i_size */
	unsigned short          i_bytes; // 文件中最后一个块的字节数
	u8			i_blkbits; // 块的位数
	u8			i_write_hint;
	blkcnt_t		i_blocks; // 文件的块数

	/* Misc */
	unsigned long		i_state; // 索引节点的状态标志
	struct rw_semaphore	i_rwsem;

	unsigned long		dirtied_when;	/* jiffies of first dirtying */
	unsigned long		dirtied_time_when;

	struct hlist_node	i_hash; // 用于散列链表的指针
	struct list_head	i_io_list;	/* backing dev IO list */
#ifdef CONFIG_CGROUP_WRITEBACK
	struct bdi_writeback	*i_wb;		/* the associated cgroup wb */

	/* foreign inode detection, see wbc_detach_inode() */
	int			i_wb_frn_winner;
	u16			i_wb_frn_avg_time;
	u16			i_wb_frn_history;
#endif
	struct list_head	i_lru;		/* inode LRU list */
	struct list_head	i_sb_list;
	struct list_head	i_wb_list;	/* backing dev writeback list */
	union {
		struct hlist_head	i_dentry; // 引用索引节点的目录项链表的头
		struct rcu_head		i_rcu;
	};
	atomic64_t		i_version;
	atomic64_t		i_sequence; /* see futex */
	atomic_t		i_count;
	atomic_t		i_dio_count;
	atomic_t		i_writecount;
#if defined(CONFIG_IMA) || defined(CONFIG_FILE_LOCKING)
	atomic_t		i_readcount; /* struct files open RO */
#endif
	union {
		const struct file_operations	*i_fop;	/* former ->i_op->default_file_ops */
		void (*free_inode)(struct inode *);
	};
	struct file_lock_context	*i_flctx;
	struct address_space	i_data; //
	struct list_head	i_devices; // 用于具体的字符或块设备索引节点链表的指针
	union {
		struct pipe_inode_info	*i_pipe; // 该文件是个管道（？）
		struct cdev		*i_cdev;
		char			*i_link;
		unsigned		i_dir_seq;
	};

	__u32			i_generation; // 索引节点版本号

	...

	void			*i_private; /* fs or device private pointer */
} __randomize_layout;
```

每个索引节点都会赋值磁盘中的索引节点包含的一些数据，比如分配给文件的磁盘块数等。

每个索引节点总是出现在下列某个双向循环链表中：

- 有效未使用的索引节点链表。这些索引节点不为脏，且它们的 `i_count` 为 0（未被任何进程使用）。这个链表用作磁盘高速缓存。
- 正在使用的索引节点链表。不为脏，`i_count` 为正数。
- 脏索引节点链表。链表中的首元素和尾元素由相应超级块的 `s_dirty` 字段引用。

#### file

该数据结构描述进程怎样与一个打开的文件进行交互（文件描述符？）。`struct file` 在磁盘上没有对应的映像，所以没有 `dirty` 位。

```c
struct file {
	union {
		struct llist_node	fu_llist; // 文件链表指针
		struct rcu_head 	fu_rcuhead;
	} f_u;
	struct path		f_path; // 目录么。并不是，其报刊文件名和 inode 之间的关系和文件所在文件系统的有关信息
	struct inode		*f_inode;	/* cached value */
	const struct file_operations	*f_op; // 该文件的所有操作

	/*
	 * Protects f_ep, f_flags.
	 * Must not be taken from IRQ context.
	 */
	spinlock_t		f_lock;
	enum rw_hint		f_write_hint;
	atomic_long_t		f_count; // 引用计数器
	unsigned int 		f_flags; // 打开文件时所指定的标志
	fmode_t			f_mode; // 进程的访问模式
	struct mutex		f_pos_lock;
	loff_t			f_pos; // 当前的文件偏移量（文件指针）
	struct fown_struct	f_owner; // 通过信号进行 I/O 时间通知的数据
	const struct cred	*f_cred;
	struct file_ra_state	f_ra; // 文件预读状态

	u64			f_version;
#ifdef CONFIG_SECURITY
	void			*f_security;
#endif
	/* needed for tty driver, and maybe others */
	void			*private_data; // 指向特定文件系统或设备驱动程序所需的数据

#ifdef CONFIG_EPOLL
	/* Used by fs/eventpoll.c to link all the hooks to this file */
	struct hlist_head	*f_ep; // 文件的事件轮询等待者链表的头
#endif /* #ifdef CONFIG_EPOLL */
	struct address_space	*f_mapping;
	errseq_t		f_wb_err;
	errseq_t		f_sb_err; /* for syncfs */
} __randomize_layout
```

存放在 `struct file` 中的主要信息是文件指针，即文件当前的位置，下一个操作将在该位置发生。

#### dentry

VFS 把每个目录看作由若干个子目录和文件组成的一个普通的文件，当从实际的磁盘文件系统中读取目录项到内存时，VFS 会将其转换成基于 dentry 结构的一个目录项对象。对于进程查找的路径名中的每个分量，内核都为其**创建一个目录项对象**，目录项对象将每个分量与其对应的索引节点相联系。可以这样理解 `struct dentry` 提供了文件名和 `inode` 之间的关联。

```c
struct dentry {
	/* RCU lookup touched fields */
	unsigned int d_flags;		/* protected by d_lock */ // 目录项高速缓存标志
	seqcount_spinlock_t d_seq;	/* per dentry seqlock */
	struct hlist_bl_node d_hash;	/* lookup hash list */
	struct dentry *d_parent;	/* parent directory */
	struct qstr d_name; // 文件名
	struct inode *d_inode; // 与文件关联的索引节点
	unsigned char d_iname[DNAME_INLINE_LEN];	/* small names */

	/* Ref lookup also touches following */
	struct lockref d_lockref;	/* per-dentry lock and refcount */
	const struct dentry_operations *d_op;
	struct super_block *d_sb;	/* The root of the dentry tree */
	unsigned long d_time;		/* used by d_revalidate */
	void *d_fsdata;			/* fs-specific data */

	union {
		struct list_head d_lru;		/* LRU list */
		wait_queue_head_t *d_wait;	/* in-lookup ones only */
	};
	struct list_head d_child;	/* child of parent list */
	struct list_head d_subdirs;	/* our children */ // 子目录/文件的目录项链表
	/*
	 * d_alias and d_rcu can share memory
	 */
	union {
		struct hlist_node d_alias;	/* inode alias list */
		struct hlist_bl_node d_in_lookup_hash;	/* only for in-lookup ones */
	 	struct rcu_head d_rcu;
	} d_u;
} __randomize_layout;
```

每个目录项可以处于以下四种状态：

- 空闲状态：不包括有效信息，还没有被 VFS 使用；
- 未使用状态：未被内核时用，但 `d_inode` 字段指向关联的索引节点，在回收内存时可能会被丢弃；
- 正在使用状态：正在被使用，包含有效信息，不会被丢弃；
- 负状态：与该目录项关联的索引节点不存在，但该目录项依旧保存在内存中，便于后续操作。

#### fs_struct

该数据结构**维护进程当前的工作目录和根目录**，`struct task_struct` 中的 `fs_struct fs` 就指向该结构。

```c
struct fs_struct {
	int users;
	spinlock_t lock;
	seqcount_spinlock_t seq;
	int umask; // 设置为文件权限的掩码
	int in_exec;
	struct path root, pwd; // 根目录和当前工作目录的目录项
} __randomize_layout;
```

#### files_struct

该数据结构表示进程当前打开的文件。这个和 `struct file` 有什么区别？这个数据结构可以理解为打开文件表，而 `struct file` 则是表中的表项，表示具体的文件信息。

```c
/*
 * Open file table structure
 */
struct files_struct {
  /*
   * read mostly part
   */
	atomic_t count; // 共享该表的进程数
	bool resize_in_progress;
	wait_queue_head_t resize_wait;

	struct fdtable __rcu *fdt;
	struct fdtable fdtab;
  /*
   * written part on a separate cache line in SMP
   */
	spinlock_t file_lock ____cacheline_aligned_in_smp;
	unsigned int next_fd;
	unsigned long close_on_exec_init[1];
	unsigned long open_fds_init[1];
	unsigned long full_fds_bits_init[1];
	struct file __rcu * fd_array[NR_OPEN_DEFAULT]; // 文件指针的初始化数组
};
```

```c
struct fdtable {
	unsigned int max_fds; // 文件对象的当前最大数目
	struct file __rcu **fd;      /* current fd array */
	unsigned long *close_on_exec; // 执行 exec() 时需要关闭的文件描述符的指针
	unsigned long *open_fds; // 打开文件描述符的指针
	unsigned long *full_fds_bits;
	struct rcu_head rcu;
};
```

`fdtable->fd` 通常指向 `files_struct->fd_array`，该数组的索引就是文件描述符（哪个数据结构？），通常第一个元素（索引为 0）时进程的标准输入文件，第二个是标准输出文件（索引为 1），第三个是标准错误文件（索引为 2）。

我们打开了一个文件后，操作系统会跟踪进程打开的所有文件，即**为每个进程维护一个打开文件表**，文件表里的每一项代表「**文件描述符**」，所以说文件描述符是打开文件的标识。

### 文件系统类型

#### 特殊文件系统

我还以为特殊文件是啥呢，原来这些文件系统在内核中非常重要。

网络和磁盘文件系统能够使用户处理存放在内核之外的信息，特殊文件系统能够为系统程序员和管理员提供一种容易的方式来操作内核的数据结构并实现操作系统的特殊特征。下面列出了 linux 内核中常用的特殊文件系统。

| 名字        | 安装点        | 描述                                  |
| ----------- | ------------- | ------------------------------------- |
| bdev        | 无            | 块设备                                |
| binfmt_misc | 任意          | 其他可执行格式                        |
| devpts      | /dev/pts      | 伪终端支持                            |
| eventpollfs | 无            | 由有效事件轮询机制使用                |
| futexfs     | 无            | 由 futex （快速用户空间加锁）机制使用 |
| pipefs      | 无            | 管道                                  |
| proc        | /proc         | 对内核数据结构的常规访问点            |
| rootfs      | 无            | 为启动阶段提供一个空的根目录          |
| shm         | 无            | IPC 共享线性区                        |
| mqueue      | 任意          | 实现 POSIX 消息队列时使用             |
| sockfs      | 无            | 套接字                                |
| sysfs       | /sys          | 对系统数据的常规访问点                |
| tmpfs       | 任意          | 临时文件                              |
| usbfs       | /proc/bus/usb | USB 设备                              |

### 文件系统处理

Linux 使用系统的根文件系统（？），其由内核在引导阶段直接安装，并拥有系统初始化脚本以及最基本的系统程序。其他文件系统要么由初始化脚本安装，要么由用户直接安装在已安装文件系统的目录上。

#### 命名空间

在传统的 Unix 系统中，只有一个已安装文件系统树：从系统的根文件系统开始，每个进程通过指定合适的路径名可以访问已安装文件系统中的任何文件。而从 Linux 2.6 开始，每个进程可以拥有自己的已安装文件系统树——进程的命名空间（namespace）。

通常大多数进程共享一个命名空间，即位于系统的根文件系统且被 init 进程使用的已安装文件系统树。不过如果 `clone` 系统调用以 `CLONE_NEWS` 标志创建一个新进程，那么新进程将获取这个新的命名空间。

#### 文件系统安装

大多数类 Unix 系统中，每个文件系统只能安装一次，例如通过如下指令安装：

```
mount -t ext2 /dev/fd0 /flp // 将存放在 /dev/fd0 软盘上的 ext2 文件系统安装在 /flp 上
```

在没有 umount 之前，无法重复 mount。然后 Linux 不同，同一个文件系统被安装多次是可能的，也就是说该文件系统有多个安装点来访问，但只有一个 `super_block`。

安装的文件系统形成一个层次：一个文件系统的安装点可能称为第二个文件系统的目录，而第二个文件系统的安装点又安装在第三个文件系统上。

![filesystem_structre.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/filesystem_structre.png?raw=true)

#### vfsmount

这个数据结构是记录每个安装点的信息（这个地方没有弄懂）。

```c
struct vfsmount
{
	struct list_head mnt_hash;
	struct vfsmount *mnt_parent;	/* fs we are mounted on */ // 指向父文件系统
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */ // 该文件系统安装点目录的 dentry
	struct dentry *mnt_root;	/* root of the mounted tree */
	struct super_block *mnt_sb;	/* pointer to superblock */
	struct list_head mnt_mounts;	/* list of children, anchored here */
	struct list_head mnt_child;	/* and going through their mnt_child */
	atomic_t mnt_count;
	int mnt_flags;
	int mnt_expiry_mark;		/* true if marked for expiry */ // 该文件系统已过期（什么叫过期？）
	char *mnt_devname;		/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;
	struct list_head mnt_fslink;	/* link in fs-specific expiry list */
	struct namespace *mnt_namespace; /* containing namespace */
};
```

#### 安装普通文件系统

这部分分析内核安装一个文件系统需要执行的操作，首先考虑一个文件系统将被安装在一个已安装文件系统之上的情形。`mount` 系统调用被用来安装一个普通文件系统，它的执行函数是 `sys_mount`。

```c
// 文件系统所在的设备文件的路径名
// 文集呐系统被安装在某个目录的路径名（安装点）
// 文件系统的类型，有 MS_RDONLY, MS_NOSUID, MS_NODEV 等等
// 安装标志
// 指向一个与文件系统相关的数据结构
SYSCALL_DEFINE5(mount, char __user *, dev_name, char __user *, dir_name,
		char __user *, type, unsigned long, flags, void __user *, data)
{
	int ret;
	char *kernel_type;
	char *kernel_dev;
	void *options;

	kernel_type = copy_mount_string(type); // 将参数从用户进程空间拷贝到内核空间

	kernel_dev = copy_mount_string(dev_name);

	options = copy_mount_options(data);

	ret = do_mount(kernel_dev, dir_name, kernel_type, flags, options);

	...

	return ret;
}
```

##### 关键函数do_mount

```c
long do_mount(const char *dev_name, const char __user *dir_name,
		const char *type_page, unsigned long flags, void *data_page)
{
	struct path path; // 这个数据结构是用来干嘛的
	int ret;

	ret = user_path_at(AT_FDCWD, dir_name, LOOKUP_FOLLOW, &path); // 获取路径名
	if (ret)
		return ret;
	ret = path_mount(dev_name, &path, type_page, flags, data_page); // 检查众多标志位
	path_put(&path);
	return ret;
}
```

下面我们看看 `sysfs` 特殊文件系统的挂载过程，

```plain
#0  do_mount (dev_name=dev_name@entry=0xffff888100c26070 "sysfs", // 该特殊文件系统为 sysfs
    dir_name=dir_name@entry=0x7fffd702ff8a "/sys", // 挂载点为 /sys
    type_page=type_page@entry=0xffff888100c26608 "sysfs", flags=flags@entry=32782,
    data_page=data_page@entry=0x0 <fixed_percpu_data>) at fs/namespace.c:3328
#1  0xffffffff8135576b in __do_sys_mount (data=<optimized out>, flags=32782,
    type=<optimized out>, dir_name=0x7fffd702ff8a "/sys", dev_name=<optimized out>)
    at fs/namespace.c:3539
#2  __se_sys_mount (data=<optimized out>, flags=32782, type=<optimized out>,
    dir_name=140736800685962, dev_name=<optimized out>) at fs/namespace.c:3516
#3  __x64_sys_mount (regs=<optimized out>) at fs/namespace.c:3516
#4  0xffffffff81c0711b in do_syscall_x64 (nr=<optimized out>, regs=0xffffc90000473f58)
    at arch/x86/entry/common.c:50
#5  do_syscall_64 (regs=0xffffc90000473f58, nr=<optimized out>) at arch/x86/entry/common.c:80
#6  0xffffffff81e0007c in entry_SYSCALL_64 () at arch/x86/entry/entry_64.S:113
#7  0x0000000000000000 in ?? ()
```

```c
int path_mount(const char *dev_name, struct path *path,
		const char *type_page, unsigned long flags, void *data_page)
{
	unsigned int mnt_flags = 0, sb_flags;
	int ret;

	...

	/* Separate the per-mountpoint flags */
	if (flags & MS_NOSUID)
		mnt_flags |= MNT_NOSUID;
	if (flags & MS_NODEV)
		mnt_flags |= MNT_NODEV;
	if (flags & MS_NOEXEC)
		mnt_flags |= MNT_NOEXEC;
	if (flags & MS_NOATIME)
		mnt_flags |= MNT_NOATIME;
	if (flags & MS_NODIRATIME)
		mnt_flags |= MNT_NODIRATIME;
	if (flags & MS_STRICTATIME)
		mnt_flags &= ~(MNT_RELATIME | MNT_NOATIME);
	if (flags & MS_RDONLY)
		mnt_flags |= MNT_READONLY;
	if (flags & MS_NOSYMFOLLOW)
		mnt_flags |= MNT_NOSYMFOLLOW;

	/* The default atime for remount is preservation */
	if ((flags & MS_REMOUNT) &&
	    ((flags & (MS_NOATIME | MS_NODIRATIME | MS_RELATIME |
		       MS_STRICTATIME)) == 0)) {
		mnt_flags &= ~MNT_ATIME_MASK;
		mnt_flags |= path->mnt->mnt_flags & MNT_ATIME_MASK;
	}

	sb_flags = flags & (SB_RDONLY | // 设置 superblock 的标志位
			    SB_SYNCHRONOUS |
			    SB_MANDLOCK |
			    SB_DIRSYNC |
			    SB_SILENT |
			    SB_POSIXACL |
			    SB_LAZYTIME |
			    SB_I_VERSION);

	if ((flags & (MS_REMOUNT | MS_BIND)) == (MS_REMOUNT | MS_BIND))
		return do_reconfigure_mnt(path, mnt_flags);
	if (flags & MS_REMOUNT) // 该标志位表示改变 sb->s_flags 的安装标志一级已安装文件系统 mnt_flags 字段
		return do_remount(path, flags, sb_flags, mnt_flags, data_page);
	if (flags & MS_BIND) // 在系统目录树的另一个安装点上的文件或目录是可见的
		return do_loopback(path, dev_name, flags & MS_REC);
	if (flags & (MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE))
		return do_change_type(path, flags); // 改变文件系统的类型
	if (flags & MS_MOVE) // 改变已安装文件系统的安装点
		return do_move_mount_old(path, dev_name);

    // 最普通的情况，安装一个特殊文件系统或者存放在磁盘分区中的普通文件系统
	return do_new_mount(path, type_page, sb_flags, mnt_flags, dev_name,
			    data_page);
}
```

##### 关键函数do_new_mount_fc

`do_new_mount` -> `do_new_mount_fc`

```c

 * Create a new mount using a superblock configuration and request it
 * be added to the namespace tree.
 */
static int do_new_mount_fc(struct fs_context *fc, struct path *mountpoint,
			   unsigned int mnt_flags)
{
	struct vfsmount *mnt;
	struct mountpoint *mp;
	struct super_block *sb = fc->root->d_sb;
	int error;

	error = security_sb_kern_mount(sb);
	if (!error && mount_too_revealing(sb, &mnt_flags))
		error = -EPERM;

	if (unlikely(error)) {
		fc_drop_locked(fc);
		return error;
	}

	up_write(&sb->s_umount);

	mnt = vfs_create_mount(fc);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	mnt_warn_timestamp_expiry(mountpoint, mnt);

	mp = lock_mount(mountpoint);
	if (IS_ERR(mp)) {
		mntput(mnt);
		return PTR_ERR(mp);
	}
	error = do_add_mount(real_mount(mnt), mp, mountpoint, mnt_flags);
	unlock_mount(mp);
	if (error < 0)
		mntput(mnt);
	return error;
}
```

##### 关键函数vfs_create_mount

```c
/**
 * vfs_create_mount - Create a mount for a configured superblock
 * @fc: The configuration context with the superblock attached
 *
 * Create a mount to an already configured superblock.  If necessary, the
 * caller should invoke vfs_get_tree() before calling this.
 *
 * Note that this does not attach the mount to anything.
 */
struct vfsmount *vfs_create_mount(struct fs_context *fc)
{
	struct mount *mnt;

	if (!fc->root)
		return ERR_PTR(-EINVAL);

	mnt = alloc_vfsmnt(fc->source ?: "none");
	if (!mnt)
		return ERR_PTR(-ENOMEM);

	if (fc->sb_flags & SB_KERNMOUNT)
		mnt->mnt.mnt_flags = MNT_INTERNAL;

	atomic_inc(&fc->root->d_sb->s_active);
	mnt->mnt.mnt_sb		= fc->root->d_sb;
	mnt->mnt.mnt_root	= dget(fc->root);
	mnt->mnt_mountpoint	= mnt->mnt.mnt_root;
	mnt->mnt_parent		= mnt;

	lock_mount_hash();
	list_add_tail(&mnt->mnt_instance, &mnt->mnt.mnt_sb->s_mounts);
	unlock_mount_hash();
	return &mnt->mnt;
}
EXPORT_SYMBOL(vfs_create_mount);
```

##### 关键函数do_add_mount

```c
/*
 * add a mount into a namespace's mount tree
 */
static int do_add_mount(struct mount *newmnt, struct mountpoint *mp,
			struct path *path, int mnt_flags)
{
	struct mount *parent = real_mount(path->mnt);

	mnt_flags &= ~MNT_INTERNAL_FLAGS;

	if (unlikely(!check_mnt(parent))) {
		/* that's acceptable only for automounts done in private ns */
		if (!(mnt_flags & MNT_SHRINKABLE))
			return -EINVAL;
		/* ... and for those we'd better have mountpoint still alive */
		if (!parent->mnt_ns)
			return -EINVAL;
	}

	/* Refuse the same filesystem on the same mount point */
	if (path->mnt->mnt_sb == newmnt->mnt.mnt_sb &&
	    path->mnt->mnt_root == path->dentry)
		return -EBUSY;

	if (d_is_symlink(newmnt->mnt.mnt_root))
		return -EINVAL;

	newmnt->mnt.mnt_flags = mnt_flags;
	return graft_tree(newmnt, parent, mp);
}
```

### 路径名查找

路径名查找也就是根据给定的文件路径名导出相应的索引节点。执行这一任务的标准过程就是分析路径名并把它们拆分成一个文件名序列。根据第一个字符是不是 “/” 决定从 `current -> fs -> root` 还是 `current -> fs -> pwd` 开始搜索。

整个查找过程是一个循环，内核首先检查与第一个名字匹配的目录项以获取相应的索引节点，然后从磁盘中读取包含哪个索引节点的目录，并检查与第二个名字匹配的目录项，以获取第二个索引节点，如此反复。而反复读取磁盘效率低下，所以有目录项高速缓存，将最近常使用的目录项保存在内存中。

虽然 Linux 使用万物皆文件的思想，但路径名的查找并不想上面描述的那样简单，有很多情况需要考虑：

- 检查每个目录的访问权限；
- 文件名可能是与任意一个路径名对应的符号链接，这样需要扩展到那个路径名的所有分量（是不是符号链接对应的文件？）；
- 符号链接可能导致循环引用；
- 文件名可能是一个已安装的文件系统的安装点，这样需要扩展到新的文件系统（这个很好理解，因为我装的双系统，能够在 Linux 中访问 windows 的文件）；
- 路径名查找应该发生在发起该系统调用的进程所在的命名空间，不同命名空间中两个进程可能使用同一个文件名。

内核的路径名查找是由 `path_lookupat` 完成的，

```c
/* Returns 0 and nd will be valid on success; Retuns error, otherwise. */
static int path_lookupat(struct nameidata *nd, unsigned flags, struct path *path)
{ // nameidata 存放了查找操作的结果
	const char *s = path_init(nd, flags); // 初始化 namaidata 的标志位等等
	int err;

	if (unlikely(flags & LOOKUP_DOWN) && !IS_ERR(s)) { // 暂时不清楚 LOOKUP_DOWN 是干啥的
		err = handle_lookup_down(nd);
		if (unlikely(err < 0))
			s = ERR_PTR(err);
	}

	while (!(err = link_path_walk(s, nd)) &&
	       (s = lookup_last(nd)) != NULL)
		;
	if (!err && unlikely(nd->flags & LOOKUP_MOUNTPOINT)) {
		err = handle_lookup_down(nd);
		nd->state &= ~ND_JUMPED; // no d_weak_revalidate(), please...
	}
	if (!err)
		err = complete_walk(nd);

	if (!err && nd->flags & LOOKUP_DIRECTORY)
		if (!d_can_lookup(nd->path.dentry))
			err = -ENOTDIR;
	if (!err) {
		*path = nd->path;
		nd->path.mnt = NULL;
		nd->path.dentry = NULL;
	}
	terminate_walk(nd);
	return err;
}
```

##### nameidata

变化很大，和 understanding 上。

```c
struct nameidata {
	struct path	path;
	struct qstr	last; // 路径名的最后一个分量
	struct path	root;
	struct inode	*inode; /* path.dentry.d_inode */
	unsigned int	flags, state;
	unsigned	seq, m_seq, r_seq;
	int		last_type;
	unsigned	depth; // 符号链接嵌套的当前级别
	int		total_link_count;
	struct saved {
		struct path link;
		struct delayed_call done;
		const char *name;
		unsigned seq;
	} *stack, internal[EMBEDDED_LEVELS];
	struct filename	*name;
	struct nameidata *saved;
	unsigned	root_seq;
	int		dfd;
	kuid_t		dir_uid;
	umode_t		dir_mode;
} __randomize_layout;
```

#### 关键函数link_path_walk

```c
/*
 * Name resolution.
 * This is the basic name resolution function, turning a pathname into
 * the final dentry. We expect 'base' to be positive and a directory.
 *
 * Returns 0 and nd will have valid dentry and mnt on success.
 * Returns error and drops reference to input namei data on failure.
 */
static int link_path_walk(const char *name, struct nameidata *nd)
{
	int depth = 0; // depth <= nd->depth
	int err;

	nd->last_type = LAST_ROOT;
	nd->flags |= LOOKUP_PARENT;
	if (IS_ERR(name))
		return PTR_ERR(name);
	while (*name=='/')
		name++;
	if (!*name) {
		nd->dir_mode = 0; // short-circuit the 'hardening' idiocy
		return 0;
	}

	/* At this point we know we have a real path component. */
	for(;;) {
		struct user_namespace *mnt_userns;
		const char *link;
		u64 hash_len;
		int type;

		mnt_userns = mnt_user_ns(nd->path.mnt);
		err = may_lookup(mnt_userns, nd);
		if (err)
			return err;

		hash_len = hash_name(nd->path.dentry, name);

		type = LAST_NORM;
		if (name[0] == '.') switch (hashlen_len(hash_len)) {
			case 2:
				if (name[1] == '.') {
					type = LAST_DOTDOT;
					nd->state |= ND_JUMPED;
				}
				break;
			case 1:
				type = LAST_DOT;
		}
		if (likely(type == LAST_NORM)) {
			struct dentry *parent = nd->path.dentry;
			nd->state &= ~ND_JUMPED;
			if (unlikely(parent->d_flags & DCACHE_OP_HASH)) {
				struct qstr this = { { .hash_len = hash_len }, .name = name };
				err = parent->d_op->d_hash(parent, &this);
				if (err < 0)
					return err;
				hash_len = this.hash_len;
				name = this.name;
			}
		}

		nd->last.hash_len = hash_len;
		nd->last.name = name;
		nd->last_type = type;

		name += hashlen_len(hash_len);
		if (!*name)
			goto OK;
		/*
		 * If it wasn't NUL, we know it was '/'. Skip that
		 * slash, and continue until no more slashes.
		 */
		do {
			name++;
		} while (unlikely(*name == '/'));
		if (unlikely(!*name)) {
OK:
			/* pathname or trailing symlink, done */
			if (!depth) {
				nd->dir_uid = i_uid_into_mnt(mnt_userns, nd->inode);
				nd->dir_mode = nd->inode->i_mode;
				nd->flags &= ~LOOKUP_PARENT;
				return 0;
			}
			/* last component of nested symlink */
			name = nd->stack[--depth].name;
			link = walk_component(nd, 0);
		} else {
			/* not the last component */
			link = walk_component(nd, WALK_MORE);
		}
		if (unlikely(link)) {
			if (IS_ERR(link))
				return PTR_ERR(link);
			/* a symlink to follow */
			nd->stack[depth++].name = name;
			name = link;
			continue;
		}
		if (unlikely(!d_can_lookup(nd->path.dentry))) {
			if (nd->flags & LOOKUP_RCU) {
				if (!try_to_unlazy(nd))
					return -ECHILD;
			}
			return -ENOTDIR;
		}
	}
}
```



### VFS 系统调用的实现

文件系统中有几个重要的数据结构（这应该是存储在硬盘中的实际文件系统的数据结构）：

- **superblock**: 记录此 fs 的整体信息，包括 inode/block 的总量、使用量、剩余量，以及文件系统的格式与相关信息等；
- **inode table**: superblock 之后就是 inode table，存储了全部 inode；
- **data block**: inode table 之后就是 data block。文件的内容保存在这个区域，磁盘上所有块的大小都一样；
- **inode**: 记录文件的属性，同时记录此文件的数据所在的 block 号码。**每个 inode 对应一个文件/目录的结构**，这个结构它包含了一个文件的长度、创建及修改时间、权限、所属关系、磁盘中的位置等信息。
- **block**: 实际记录文件的内容。一个较大的文件很容易分布上千个独产的磁盘块中，而且，一般都不连续，太散会导致读写性能急剧下降。

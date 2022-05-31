## File System

### 虚拟文件系统

虚拟文件系统（Virtual Filesystem Switch）所隐含的思想是把表示很多不同种类文件系统的共同信息放入内核，其中有一个字段或函数来支持内核所支持的所有实际文件系统所提供的任何操作。对所调用的每个读、写或者其他函数，内核都能将其替换成本地文件系统的实际函数。

VFS 支持的文件系统可以分成三种主要类型：

- 磁盘文件系统。这些文件系统（Ext2, Ext3, System V, UFS, NTFS 等）管理在本地磁盘分区中可用的存储空间或其他可以起到磁盘作用的设备（如 USB 闪存）
- 网络文件系统。这些文件系统允许轻易的访问属于其他网络计算机的文件系统所包含的文件。如：NFS, AFS, CIFS 等。
- 特殊文件系统。这些文件系统不管理本地或者远程磁盘空间。/proc 是一个典型的特殊文件系统。

#### 通用文件模型

VFS 为了支持尽可能多的文件系统，引入了一个通用的文件模型，要实现每个具体的文件系统，必须将其物理组织结构转换为虚拟文件系统的通用文件模型。

应用程序对 `read()` 的调用引起内核调用相应的 `sys_read()` 系统调用，而文件再内核中是用 `struct file` 表示的，`file` 中包含成员变量 `f_op`，该成员变量包含指向实际文件系统的函数指针，包括读写文件的函数，`sys_read()` 找到该函数的指针，并调用它。

通用文件模型由下列对象类型组成：

- 超级块对象（superblock object）

  存放以安装文件系统的有关信息。基于磁盘的文件系统，这类对象通常对应于存放在磁盘上的文件系统控制块（filesystem control block）。

- 索引节点对象（inode object）

  存放具体文件的信息，每个索引节点都有一个索引节点号，**这个节点号唯一地表示文件系统中的文件**。

- 文件对象（file object）

  存放打开文件域进程之间进行交互的有关信息，这类信息仅当进程访问文件期间存在内核内存中。

- 目录项对象（dentry object）

  存放目录项（文件的特定名称）域对应文件进行链接的有关信息。

下面我们看看进程怎样与文件进行交互。三个不同的进程已经打开同一个文件，其中两个进程使用同一个硬链接，这样每个进程都有自己的文件对象，但只需要两个目录项对象，每个硬链接对应一个目录项，这两个目录项指向同一个索引节点，该索引节点表示超级块以及随后的普通磁盘文件。

![process-VFS.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/process-VFS.png?raw=true)

### VFS 的数据结构

这节介绍 VFS 相关的数据结构及其关系。

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

该数据结构描述进程怎样与一个打开的文件进行交互。`struct file` 在磁盘上没有对应的映像，所以没有 `dirty` 位。

```c
struct file {
	union {
		struct llist_node	fu_llist; // 文件链表指针
		struct rcu_head 	fu_rcuhead;
	} f_u;
	struct path		f_path; // 目录么
	struct inode		*f_inode;	/* cached value */
	const struct file_operations	*f_op;

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

VFS 把每个目录看作由若干个子目录和文件组成的一个普通的文件。对于进程查找的路径名中的每个分量，内核都为其**创建一个目录项对象**，目录项对象将每个分量域器及对应的索引节点联系。

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
	struct list_head d_subdirs;	/* our children */
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

该数据结构维护进程当前的工作目录和根目录，`struct task_struct` 中的 `fs_struct fs` 就指向该结构。

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

该数据结构表示进程当前打开的文件。

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

### 路径名查找

### VFS 系统调用的实现

文件系统中有几个重要的数据结构（这应该是存储在硬盘中的实际文件系统的数据结构）：

- **superblock**: 记录此 fs 的整体信息，包括 inode/block 的总量、使用量、剩余量，以及文件系统的格式与相关信息等；
- **inode table**: superblock 之后就是 inode table，存储了全部 inode；
- **data block**: inode table 之后就是 data block。文件的内容保存在这个区域，磁盘上所有块的大小都一样；
- **inode**: 记录文件的属性，同时记录此文件的数据所在的 block 号码。**每个 inode 对应一个文件/目录的结构**，这个结构它包含了一个文件的长度、创建及修改时间、权限、所属关系、磁盘中的位置等信息。
- **block**: 实际记录文件的内容。一个较大的文件很容易分布上千个独产的磁盘块中，而且，一般都不连续，太散会导致读写性能急剧下降。

## 在内核态访问用户态地址空间

在分析内核加载用户态进程时运到一个函数—— `clear_user`，

```c
/**
 * clear_user: - Zero a block of memory in user space.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long
clear_user(void __user *to, unsigned long n)
{
	might_fault();
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}
```

从注释来看是将用户空间的一段空间的值置为 0，但问题是加载用户态进程时系统还运行在内核态，怎么能访问用户地址空间呢？我们来分析一下这个问题。

开始我只知道通过 `copy_to_user` 、`copy_from_user` 能够将一个数据块从内核复制到用户空间，但很显然这个函数也能。

通过查找资料，发现有如下函数能够在内核态访问用户地址空间：

#### access_ok

`access_ok` 函数用于检查想要访问的用户空间检查指针的有效性。

```c
/**
 * access_ok: - Checks if a user space pointer is valid
 * @type: Type of access: %VERIFY_READ or %VERIFY_WRITE.  Note that
 *        %VERIFY_WRITE is a superset of %VERIFY_READ - if it is safe
 *        to write to a block, it is always safe to read from it.
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 */
#define access_ok(type, addr, size)			\ // type 为 VERIFY_READ 或 VERIFY_WRITE
({									\
	WARN_ON_IN_IRQ();						\
	likely(!__range_not_ok(addr, size, user_addr_max()));		\
})
```

#### get_user

`get_user`  从用户空间读取一个简单变量，其**适用于简单数据类型**，比如 `char` 和 `int`。而像结构体这类较大的数据类型，必须使用 `copy_from_user` 。该原型接受一个变量（存储数据）和一个用户空间地址来进行读操作：

```c
#define get_user(x, ptr)						\
({									\
	int __ret_gu;							\
	register __inttype(*(ptr)) __val_gu asm("%"_ASM_DX);		\
	__chk_user_ptr(ptr);						\
	might_fault();							\
	asm volatile("call __get_user_%P3"				\
		     : "=a" (__ret_gu), "=r" (__val_gu)			\
		     : "0" (ptr), "i" (sizeof(*(ptr))));		\
	(x) = (__force __typeof__(*(ptr))) __val_gu;			\
	__builtin_expect(__ret_gu, 0);					\
})
```

#### put_user

`put_user` 用来将一个简单变量**从内核写入用户空间**。和 `get_user` 一样，它接受一个变量（包含要写的值）和一个用户空间地址作为写目标。

#### clear_user

`clear_user` 用于将用户空间的内存块清零。代码上面有。

#### copy_to_user

`copy_to_user` 将数据块从内核复制到用户空间。该函数接受一个指向用户空间缓冲区的指针、一个指向内存缓冲区的指针、以及一个以字节定义的长度。该函数在成功时，返回 0，否则返回一个非零数，指出不能发送的字节数。

```c
/**
 * copy_to_user: - Copy a block of data into user space.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from kernel space to user space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
static inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	int sz = __compiletime_object_size(from);

	might_fault();

	/* See the comment in copy_from_user() above. */
	if (likely(sz < 0 || sz >= n))
		n = _copy_to_user(to, from, n);
	else if(__builtin_constant_p(n))
		copy_to_user_overflow();
	else
		__copy_to_user_overflow(sz, n);

	return n;
}
```

#### copy_from_user

`copy_from_user` 用于将数据块从用户空间复制到内核缓冲区。它接受一个目的缓冲区（在内核空间）、一个源缓冲区（从用户空间）和一个以字节定义的长度。和 copy_to_user 一样，该函数在成功时，返回 0 ，否则返回一个非零数，指出不能复制的字节数。

```c
static inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	int sz = __compiletime_object_size(to);

	might_fault();

	if (likely(sz < 0 || sz >= n))
		n = _copy_from_user(to, from, n);
	else if(__builtin_constant_p(n))
		copy_from_user_overflow();
	else
		__copy_from_user_overflow(sz, n);

	return n;
}
```

#### strnlen_user

`strnlen_user` 也能像 `strnlen` 那样使用，但前提是缓冲区在用户空间可用。`strnlen_user` 函数带有两个参数：用户空间缓冲区地址和要检查的最大长度。

```c
/**
 * strnlen_user: - Get the size of a user string INCLUDING final NUL.
 * @str: The string to measure.
 * @count: Maximum count (including NUL character)
 */
long strnlen_user(const char __user *str, long count)
{
	unsigned long max_addr, src_addr;

	if (unlikely(count <= 0))
		return 0;

	max_addr = user_addr_max();
	src_addr = (unsigned long)str;
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		return do_strnlen_user(str, count, max);
	}
	return 0;
}
```

#### strncpy_from_user

`strncpy_from_user` 将一个字符串从用户空间复制到一个内核缓冲区，给定一个用户空间源地址和最大长度。

```c
/**
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 */
long strncpy_from_user(char *dst, const char __user *src, long count)
{
	unsigned long max_addr, src_addr;

	if (unlikely(count <= 0))
		return 0;

	max_addr = user_addr_max();
	src_addr = (unsigned long)src;
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		return do_strncpy_from_user(dst, src, count, max);
	}
	return -EFAULT;
}
```

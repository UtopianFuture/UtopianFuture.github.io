## Page forking

The author push a concept named "page forking", It is a response to the [problem](# stable pages) that led to the addition of "stable pages" to the kernel and the [performance regressions](# Optimizing stable pages).

Page forking is a sort of copy-on-write mechanism for pages that are currently being written back to persistent store. Should some process attempt to modify such a page while I/O is active, a copy of the page will be made and the modification will happen in the copy; the old copy remains until the writeback I/O is complete.

But maintainer skepticism about if the performance could promote. Whenever a page is written back, the kernel would have to find every page table entry referring to it and turn that entry read-only so that the copy-on-write semantics could be enforced. Memory-mapped pages are heavily used in Linux, so there could be a real performance cost there.

The page forking concept don't be accepted by all maintainers.

### stable pages

When a process writes to a file-backed page in memory, that page is marked dirty and must eventually be written to its backing store. Current kernels will allow a process to modify a page while the writeback operation is in progress.

Most of time, that works fine, but in the worst case, the second write to the page will happen before the first writeback I/O operation begins, the more recently written data will also be written to disk in the first I/O operation and a second, **redundant disk write** will be queued later.

Some devices can perform integrity checking, meaning that the data written to disk is checksummed by the hardware and compared against a pre-write checksum provided by the kernel. If the data changes after the kernel calculates its checksum, that check will fail, causing a spurious write error. So the kernel needs to **support "stable pages" which are guaranteed not to change while they are under writeback**.

### Optimizing stable pages

The stable pages would cause performance problems, any change that causes processes to block and wait for asynchronous events is unlikely to make things so fast.

So the author add a new patch set, The core idea is add a new flag(`BDI_CAP_STABLE_WRITES`) to the `backing_dev_info` structure used to describe a storage device.  If that flag is set, the memory management code will enforce stable pages as is done in current kernels. Without the flag, attempts to write a page will not be forced to wait for any current writeback activity. So the flag gives the ability to choose between a slow (but maybe safer) mode or a higher-performance mode.
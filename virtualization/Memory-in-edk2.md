## Memory in edk2

这篇文章分析 edk2 提供的 memory allocation。

BMBT 中用到了 mmap 函数，但是 edk2 中没有，遂去找替代的方法，StandalonePkg 提供了 memory allocation 的方法，分析一下看怎么使用。

这个函数似乎符合要求，

```c
/**
  Allocates pages from the memory map.

  @param  Type                   The type of allocation to perform.
  @param  MemoryType             The type of memory to turn the allocated pages
                                 into.
  @param  NumberOfPages          The number of pages to allocate.
  @param  Memory                 A pointer to receive the base allocated memory
                                 address.

  @retval EFI_INVALID_PARAMETER  Parameters violate checking rules defined in spec.
  @retval EFI_NOT_FOUND          Could not allocate pages match the requirement.
  @retval EFI_OUT_OF_RESOURCES   No enough pages to allocate.
  @retval EFI_SUCCESS            Pages successfully allocated.

**/
EFI_STATUS
EFIAPI
MmAllocatePages (
  IN  EFI_ALLOCATE_TYPE     Type,
  IN  EFI_MEMORY_TYPE       MemoryType,
  IN  UINTN                 NumberOfPages,
  OUT EFI_PHYSICAL_ADDRESS  *Memory
  )
{
  EFI_STATUS  Status;

  Status = MmInternalAllocatePages (Type, MemoryType, NumberOfPages, Memory);
  return Status;
}
```

但 `EFI_ALLOCATE_TYPE`，`EFI_MEMORY_TYPE` 两个结构不清楚。

```c
///
/// Enumeration of EFI memory allocation types.
///
typedef enum {
  ///
  /// Allocate any available range of pages that satisfies the request.
  ///
  AllocateAnyPages,
  ///
  /// Allocate any available range of pages whose uppermost address is less than
  /// or equal to a specified maximum address.
  ///
  AllocateMaxAddress,
  ///
  /// Allocate pages at a specified address.
  ///
  AllocateAddress,
  ///
  /// Maximum enumeration value that may be used for bounds checking.
  ///
  MaxAllocateType
} EFI_ALLOCATE_TYPE;
```

看注释 `AllocateAnyPages`，`AllocateAddress` 都满足条件，再看看 edk2 自己的代码是怎样使用的。

```c
///
/// Enumeration of memory types introduced in UEFI.
///
typedef enum {
  EfiReservedMemoryType,
  EfiLoaderCode,
  EfiLoaderData,
  EfiBootServicesCode,
  EfiBootServicesData,
  ///
  /// The code portions of a loaded Runtime Services Driver.
  ///
  EfiRuntimeServicesCode,
  ///
  /// The data portions of a loaded Runtime Services Driver and the default
  /// data allocation type used by a Runtime Services Driver to allocate pool memory.
  ///
  EfiRuntimeServicesData,
  ///
  /// Free (unallocated) memory.
  ///
  EfiConventionalMemory,
  EfiUnusableMemory,
  EfiACPIReclaimMemory,
  EfiACPIMemoryNVS,
  EfiMemoryMappedIO,
  EfiMemoryMappedIOPortSpace,
  EfiPalCode,
  ///
  /// A memory region that operates as EfiConventionalMemory,
  /// however it happens to also support byte-addressable non-volatility.
  ///
  EfiPersistentMemory,
  EfiMaxMemoryType
} EFI_MEMORY_TYPE;
```

貌似只有 `EfiConventionalMemory` 符合条件，但是不清楚 `BootService` 和 `RuntimeService` 到底在内存管理上有什么区别，需要搞清楚。这个需要花时间搞清楚整个 edk2 的执行流程，要花蛮多的时间，暂时跳过。

决定使用 `AllocateAnyPages` 和 `EfiRuntimeServicesData` 其他的都是这样用的。

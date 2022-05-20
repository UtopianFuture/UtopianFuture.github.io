## UEFI

The Unified EFI (UEFI) Specification (previously known as the EFI Specification) defines an interface between an operating system and platform firmware. 所以 [UEFI 和 EFI](https://www.quora.com/unanswered/Whats-the-difference-between-UEFI-and-EFI) 其实是一个东西，都是定义固件和操作系统之间的接口。UEFI 只是一个协议，没有提供实现，具体的实现有 INTEL 开源的 EDK2 等。

### .dsc, .dec, .inf 几个文件的不同作用

#### .inf

.inf 文件是模块的工程文件，其作用相当于 makefile 文件，用于指导 edk2 编译对应的模块。其包含如下几个部分：

- [defines] 块

- [Sources] 块

  列出模块所有的源文件和资源文件。

  ```plain
  [Sources]
    ./src/main.c
  ```

- [Packages] 块

  列出本模块引用到的所有包的 .dec 文件。要移植 bmbt 的话这里就要加上对 BmbtPkg 的引用。

  ```plain
  [Packages]
    StdLib/StdLib.dec
    MdePkg/MdePkg.dec
    ShellPkg/ShellPkg.dec
    BmbtPkg/BmbtPkg.dec
  ```

- [LibraryClasses] 块

  列出本模块要连接的库模块。这些库模块是包含在上面 Packages 引用的包里的。

  ```plain
  [LibraryClasses]
    LibC
    LibStdio
    LibStdLib
    LibSignal
    LibString
    LibMath
    LibTime
    DevShell
    UefiLib
    ShellCEntryLib
  ```

#### .dsc

.inf 用于编译一个模块，而 .dsc 用于编译一个 Package。其包含如下几个部分：

- [define] 块

- [LibraryClasses] 块

  该块定义了库的名字以及库 .inf 文件的路径，这些库可以被 [Components] 块内的模块引用。

  ```plain
  [LibraryClasses]
    #
    # Entry Point Libraries
    #
    UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
    ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf
    UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  ```

  '|' 前面是库名，后面是库的路径。

- [Components] 块

  该块定义所有要编译的模块，

  ```plain
  [Components]
  # Standard C Libraries.
    StdLib/LibC/LibC.inf
    StdLib/LibC/StdLib/StdLib.inf
    StdLib/LibC/String/String.inf
    StdLib/LibC/Wchar/Wchar.inf
    StdLib/LibC/Ctype/Ctype.inf
    StdLib/LibC/Time/Time.inf
    StdLib/LibC/Stdio/Stdio.inf
    StdLib/LibC/Locale/Locale.inf
    StdLib/LibC/Uefi/Uefi.inf
    StdLib/LibC/Math/Math.inf
    StdLib/LibC/Signal/Signal.inf
    StdLib/LibC/NetUtil/NetUtil.inf
  ```

  这些模块都会生成 efi 文件。

#### .dec

该文件定义了公开的数据和接口，供其他模块使用。其包含如下几个部分：

- [defines] 块

- [Include] 块

  该块列出了本 Package 提供的头文件所在的目录。

  对于 BmbtPkg ，其 [include] 块如下：

  ```plain
  [Includes]
    include
  ```

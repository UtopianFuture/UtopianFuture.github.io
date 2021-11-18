Makefile

### makefile的规则

```
target ... : prerequisites ...
    command
    ...
    ...
```

- target

  可以是一个object file（目标文件），也可以是一个执行文件，还可以是一个标签（label）。对于标签这种特性，在后续的“伪目标”章节中会有叙述。

- prerequisites

  生成该target所依赖的文件和/或target

- command

  该target要执行的命令（任意的shell命令）

这是一个文件的依赖关系，也就是说，target这一个或多个的目标文件依赖于prerequisites中的文件，其生成规则定义在command中。说白一点就是说:

```
prerequisites中如果有一个以上的文件比target文件要新的话，command所定义的命令就会被执行。
```

这就是makefile的规则，也就是makefile中最核心的内容。

### makefile的调试工具

1. 将这条命令加在makefile中，然后用类似与`make print-help`的命令可以打印出Makefile中的变量。

   ```
   print-%:
           @echo '$*=$($*)'
   ```

2. -n, -d, -B

   ```
   -n, --just-print, --dry-run, --recon
   	Print the commands that would be executed, but do not execute them (except in certain circumstances).
   ```

   ```
   -d
   	Print debugging information in addition to normal processing.  The debugging information says which files are being considered for remaking, which file-times are being compared  and  with  what results, which files actually need to be remade, which implicit rules are considered and which are applied---everything interesting about how make decides what to do.
   ```

   ```
   -B, --always-make
   	Unconditionally make all targets.
   ```

   即可以用`make -nd install > make.nd`将依赖关系打印出来。

3. [makefile2graph](https://github.com/lindenb/makefile2graph)是一个能将make -nd输出的依赖信息处理成树的工具。

   ```
   cat make_all.nd | ~/gitlab/makefile2graph/make2graph > make_all.dot
   ```


4. dot可以将makefile2graph处理出来的树转化成可视化的树，然后用chromium打开。

   ```
   dot -Tsvg -O make_all.dot
   ```

### 语法

1.  `$@` 表示目标的集合，就像一个数组， `$@` 依次取出目标，并执于命令。

2.  `$<` 表示第一个依赖文件。

3. 通常，make会把其要执行的命令行在命令执行前输出到屏幕上。当我们用 `@` 字符在命令行前，那么，这个命令将不被make显示出来，最具代表性的例子是，我们用这个功能来向屏幕显示一些信息。如:

   ```
   @echo 正在编译XXX模块......
   ```

   当make执行时，输出为

   ```
   正在编译XXX模块……
   ```

   但不会输出命令，如果没有“@”，那么，make将输出:

   ```
   echo 正在编译XXX模块......
   正在编译XXX模块......
   ```

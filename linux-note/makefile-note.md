Makefile

### makefile 的规则

```plain
target ... : prerequisites ...
    command
    ...
    ...
```

- target

  可以是一个 object file（目标文件），也可以是一个执行文件，还可以是一个标签（label）。对于标签这种特性，在后续的“伪目标”章节中会有叙述。

- prerequisites

  生成该 target 所依赖的文件和/或 target

- command

  该 target 要执行的命令（任意的 shell 命令）

这是一个文件的依赖关系，也就是说，target 这一个或多个的目标文件依赖于 prerequisites 中的文件，其生成规则定义在 command 中。说白一点就是说:

```plain
prerequisites中如果有一个以上的文件比target文件要新的话，command所定义的命令就会被执行。
```

这就是 makefile 的规则，也就是 makefile 中最核心的内容。

### makefile 的调试工具

1. 将这条命令加在 makefile 中，然后用类似与`make print-help`的命令可以打印出 Makefile 中的变量。

   ```plain
   print-%:
           @echo '$*=$($*)'
   ```

2. -n, -d, -B

   ```plain
   -n, --just-print, --dry-run, --recon
   	Print the commands that would be executed, but do not execute them (except in certain circumstances).
   ```

   ```plain
   -d
   	Print debugging information in addition to normal processing.  The debugging information says which files are being considered for remaking, which file-times are being compared  and  with  what results, which files actually need to be remade, which implicit rules are considered and which are applied---everything interesting about how make decides what to do.
   ```

   ```plain
   -B, --always-make
   	Unconditionally make all targets.
   ```

   即可以用`make -nd install > make.nd`将依赖关系打印出来。

3. [makefile2graph](https://github.com/lindenb/makefile2graph)是一个能将 make -nd 输出的依赖信息处理成树的工具。

   ```plain
   cat make_all.nd | ~/gitlab/makefile2graph/make2graph > make_all.dot
   ```


4. dot 可以将 makefile2graph 处理出来的树转化成可视化的树，然后用 chromium 打开。

   ```plain
   dot -Tsvg -O make_all.dot
   ```

### 语法

1.  `$@` 表示目标的集合，就像一个数组， `$@` 依次取出目标，并执于命令。

2.  `$<` 表示第一个依赖文件。

3. 通常，make 会把其要执行的命令行在命令执行前输出到屏幕上。当我们用 `@` 字符在命令行前，那么，这个命令将不被 make 显示出来，最具代表性的例子是，我们用这个功能来向屏幕显示一些信息。如:

   ```plain
   @echo 正在编译XXX模块......
   ```

   当 make 执行时，输出为

   ```plain
   正在编译XXX模块……
   ```

   但不会输出命令，如果没有“@”，那么，make 将输出:

   ```plain
   echo 正在编译XXX模块......
   正在编译XXX模块......
   ```

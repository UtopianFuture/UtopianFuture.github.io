### 一、使用[pre-commit](https://pre-commit.com/#intro)在 commit 前进行检查

安装

```plain
pip install pre-commit
```

检查是否安装成功

```plain
pre-commit --version
pre-commit 2.15.0
```

pre-commit 会在进行 commit 之前执行一些脚本做出检查，如果检查不通过， 就会提示出来，如：

```plain
git commit -m "ci: use pre-commit before commit"
[WARNING] Unstaged files detected.
[INFO] Stashing unstaged files to /home/guanshun/.cache/pre-commit/patch1634198521-3555437.
Trim Trailing Whitespace.................................................Passed
Fix End of Files.........................................................Failed
- hook id: end-of-file-fixer                                                                                                         - exit code: 1                                                                                                                       - files were modified by this hook                                                                                                   Fixing script/hook/separate-doc-code.sh
Check Yaml...........................................(no files to check)Skipped                                                       Check for added large files..............................................Passed                                                       Check for merge conflicts................................................Passed                                                       Don't commit to branch...................................................Failed                                                       - hook id: no-commit-to-branch                                                                                                       - exit code: 1                                                                                                                       format code..............................................................Failed                                                       - hook id: format-code                                                                                                               - exit code: 1
```

实际上，很多 hook 都是通用的，于是乎就有了 pre-commit 项目，可以安装一些常用的 hook, 具体配置在 .pre-commit-config.yaml 中，而且，我还添加了一些新的规则:

- format-code.sh : 在提交代码之前保证代码都是被 format 过的，用 git blame 看到的信息如果是 code format, 那相当于什么都没说；
- lint-md.sh : 中文文档需要按照规范书写；
- separate-doc-code.sh : 代码的修改和文档的修改不在在一个 commit 中提交；
- code-test.sh : 每次提交需要保证通过测试；
-  non-ascii-comment.sh : 不要在代码中携带中文注释，最好的代码是没有注释的，如果需要，那么就使用英文，不过表达不清楚，那么就写成一个 blog；

这是正常的 commit 之前的 log，如果有检查不过，那么就会提前 exit。

```plain
guanshun@Jack-ubuntu ~/g/W/doc (note)> git commit -m "docs: analysis trap_init()"
Trim Trailing Whitespace.................................................Passed
Fix End of Files.........................................................Passed
Check Yaml...........................................(no files to check)Skipped
Check for added large files..............................................Passed
Check for merge conflicts................................................Passed
Don't commit to branch...................................................Passed
format code..............................................................Passed
lint-md..................................................................Passed
code test................................................................Passed
separate-doc-code........................................................Passed
[note 8c3a1bb] docs: analysis trap_init()
 1 file changed, 84 insertions(+), 1 deletion(-)
```

### 二、使用[commitlint](https://github.com/conventional-changelog/commitlint)来检查 commit

安装

```plain
sudo npm install -g --save-dev @commitlint/{config-conventional,cli}
sudo npm install -g husky --save-dev
npx husky install
npx husky add .husky/commit-msg 'npx --no-install commitlint --edit "$1"'
```

commit 的使用规则在[这里](https://github.com/conventional-changelog/commitlint/blob/master/%40commitlint/config-conventional/index.js)。

### 三、遇到的问题

安装好 commitlint 后 commit 时 pre-commit 无效。

原因：husky 修改 `git config core.hooksPath`[4](https://github.com/Martins3/What-doesnt-kill-you-makes-you-stronger/blob/note/CONTRIBUTING.md#user-content-fn-6-2aa822bae5d8d8a17e72bcbd4b06d9ba) 的路径为 `.husky`，这会导致 `.git/hooks/pre-commit` 失效。

解决：暂时的方法将 `.git/hooks/pre-commit` 拷贝到 `.husky` 中。

### reference

[1] https://github.com/Martins3/What-doesnt-kill-you-makes-you-stronger/blob/dev/CONTRIBUTING.md#user-content-fn-2-be03cfa97f1a2a862f2ae031730e1238

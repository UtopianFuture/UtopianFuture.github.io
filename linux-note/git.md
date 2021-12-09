# git 使用

1. gitlab 中的 latx “origin”是 rd.loongson.cn，gitlab2 才是 binarytranslation，所以在和公司的库同步时用的是 git pull origin master, 与组里的 gitlab 同步用的是 git pull gitlab2 master/(其他 branch)

2. 将其他仓库的代码直接上传到自己的仓库中：

   （1）cp ***/. .

   （2）git ckeckout -b branch

   （3）git remote add {{remote_name}} {{remote_url}}

   （4）git merge master //将其 merge 到目标 branch

   （5）之后更新本地的仓库都是用的 remote_name，即 git push remote_name ***(branch)

   git remote add {{remote_name}} {{remote_url}}也可以将某个仓库命名为助记符。

3. git remote set-url origin git@github.com:UtopianFuture/xv6-labs.git，将本地仓库的 origin 直接修改成自己的仓库路径，之后 push, pull 就会对自己的仓库进行操作。

   git push origin main:master，会将 main 分支的内容上传到 master 分支，可用于多分支的情况。

4. 遇到 fatal: not a git repository (or any parent up to mount point /)

   是因为没有在 git 文件夹中操作，即没有进行 git init。

5. 记录 git 密码，从而不要每次输入密码：

   在需要的仓库中先输入：

   git config credential.helper store //将密码存储到本地

   git pull

   之后再输入用户名密码就能保存到本地的~/.git-credentials 中，在这里应该可以直接添加或修改用户名和密码。

6. git 最重要的就是使用 man

   ​	(1) -rebase：修改多个提交或旧提交的信息。http://jartto.wang/2018/12/11/git-rebase/

   ​	使用 git rebase -i HEAD~n 命令在默认文本编辑器中显示最近 n 个提交的列表。



   1. *# 	Displays a list of the last 3 commits on the current branch*

   2. *$ 	git rebase -i HEAD~3*

   3.

   ​	 (2) -reset：man git-reset

   ​	 (3) -checkout：man git-commit, git checkout . 删除所有本地修改。

   ​	 (4) --amend：git commit –amend，重写 head 的 commit 消息。

   ​	 (5) format-patch：If you want to format only <commit> itself, you can do this with

   ​	 git format-patch -1 <commit>.

   ​	 (6) stash：The command saves your local modifications away and reverts the working directory to match the HEAD commit. 就是去掉 unstaged modify。

   ​	 (7) restore：Restore specified paths in the working tree with some contents from a restore source.

   ​	 (8) log：·

   ​	 (9) reflog：

   ​	 (10) tig：查看提交的详细记录，包括代码的修改。

   ​	 (11) cherry-pick：对于多分支的代码库，将代码从一个分支转移到另一个分支有两种情况。一种情况是，合并另一个分支的所有代码变动，那么就采用合并（git merge）。另一种情况是，只需要合并部分代码变动（**某几个提交**），这时可以采用 Cherry pick。

   ​	rebase 也可以合并冲突。在 gerrit 中，多人同时开发同一个分支，合并不同的代码修改就用 git pull –rebase。

7. git add 可以加某个目录名从而添加所有该目录下的文件。

8. 在 master-mirror 分支上进行开发，如果 master 分支上有新的 commit，要将该 commit 应用到 master-mirror 上。

   首先 git checkout master-mirror，然后 git rebase master，解决冲突后 git rebase --continue，然后重新 git add, commit。

   用 git merge master 同样能合并 commit，但是这个 merge 会产生一个新的 commit，如果 merge 频繁，会使得 log 丑陋。

9. 本地分支重命名(还没有推送到远程)

   ```plain
   git branch -m oldName newName
   ```

   ​	删除远程分支

   ```plain
   git push --delete origin oldName
   ```

   ​	上传新命名的本地分支

   ```plain
   git push origin newName
   ```

   ​	把修改后的本地分支与远程分支关联

   ```plain
   git branch --set-upstream-to origin/newName
   ```

10. git 需要丢弃本地修改，同步到远程仓库（即遇到 error: You have not concluded your merge (MERGE_HEAD exists).错误时）：

    ```plain
    git fetch –all
    git reset –hard origin/master
    git fetch
    ```

   ​	之后再正常的 pull 就行。

11. git 在 push 是遇到问题

    ```plain
    squash commits first
    ```

    这是因为两个 commit 的 Change-ID 相同，先用 git-rebase -i HEAD~~查看 commit 的情况，然后选择一个 commit 进行 squash 即可。

12. git reset commitid 只是改变 git 的指针，如果要将内容也切换过去，要用 git reset –hard commitid 或者是 git checkout -f commitid, 不过这个命令会将本地的修改丢弃，有风险。

13. merge conflict 在 code 里修改之后要 git add, git commit，这样 conflict 才算解决了。

14. git status 看到的未提交的文件可以用 git checkout + filename 删掉，如果有需要保存的文件，但是库里同步过来又会覆盖的，用 git stash 保存，然后在 pull –rebase，之后再 git stash apply 恢复。

15. git diff 的不同使用：

    ```plain
    git diff            // 工作区 vs 暂存区
    git diff head       // 工作区 vs 版本库
    git diff –cached    // 暂存区 vs 版本库
    ```

16. 在上传修改前一定记得要 git pull，和最新的版本库合并，之后再 git add, commit, push，不然太麻烦了。

17. 如果忘了 pull，则 git reset 恢复到未 push 的 commit 的前一个 commit，然后 pull，解决冲突后再 git stash apply，将所有的修改恢复回来，解决冲突，然后再进行 git push。可以用 git stash list 查看所有的 stash。

18. 之后开发要 checkout 一条新的分支，所有的修改都在这条新的 Fix 分支上进行，修改完后在 push 到主分支，然后删掉新分支。问题：如果 fix 分支落后主分支，可以直接 push 么。

19. 提交代码，push 到 GitHub 上，突然出现这个问题。

    ```plain
    remote: Support for password authentication was removed on August 13, 2021. Please use a personal access token instead.
    remote: Please see https://github.blog/2020-12-15-token-authentication-requirements-for-git-operations/ for more information.
    fatal: unable to access 'https://github.com/zhoulujun/algorithm.git/': The requested URL returned error: 403
    ```

    原因：从 8 月 13 日起不在支持密码验证，改用 personal access token 替代。

    方法：用 ssh clone，设置密钥，将 id_rsa.pub 粘贴到 githab 上即可。

20. 用 git rebase 在本地合并多条 commit 后 push 到远程仓库，出现如下 bug:

    ```plain
    guanshun@Jack-ubuntu ~/g/UFuture.github.io (master)> git push origin maste
    To github.com:UtopianFuture/UFuture.github.io.git
     ! [rejected]        master -> master (non-fast-forward)
    error: failed to push some refs to 'git@github.com:UtopianFuture/UFuture.g
    hint: Updates were rejected because the tip of your current branch is behi
    hint: its remote counterpart. Integrate the remote changes (e.g.
    hint: 'git pull ...') before pushing again.
    hint: See the 'Note about fast-forwards' in 'git push --help' for details.
    ```

原因是 git 检查到当前 head 落后与远程仓库，但这本就是我们想做的，所以用

```plain
git push -f origin master
```

强行推上去，覆盖远程仓库的 commit。

21. git format-patch commit-id 可以生成对应的 patch， git apply xxx.patch 可以将 patch 集成到项目中。

22. git submodule add xxx 可以在一个 git 仓库中引用另一个 git 仓库。

    ```plain
    git submodule add git@github.com:Martins3/bmbt.git BmbtPkg
    ```

    git clone 引用了其他 git 仓库的 git 仓库时，子仓库是空的，这时要用

    ```plain
    git submodule init
    ```

    初始化本地配置文件

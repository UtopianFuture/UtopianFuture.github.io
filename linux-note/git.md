# git使用

1. gitlab中的latx “origin”是rd.loongson.cn，gitlab2才是binarytranslation，所以在和公司的库同步时用的是git pull origin master, 与组里的gitlab同步用的是git pull gitlab2 master/(其他branch)

2. 将其他仓库的代码直接上传到自己的仓库中：

   （1）cp ***/. .

   （2）git ckeckout -b branch

   （3）git remote add {{remote_name}} {{remote_url}}

   （4）git merge master //将其merge到目标branch

   （5）之后更新本地的仓库都是用的remote_name，即git push remote_name ***(branch)

   git remote add {{remote_name}} {{remote_url}}也可以将某个仓库命名为助记符。

3. git remote set-url origin git@github.com:UtopianFuture/xv6-labs.git，将本地仓库的origin直接修改成自己的仓库路径，之后push, pull就会对自己的仓库进行操作。

   git push origin main:master，会将main分支的内容上传到master分支，可用于多分支的情况。

4. 遇到 fatal: not a git repository (or any parent up to mount point /)

   是因为没有在git文件夹中操作，即没有进行git init。

5. 记录git密码，从而不要每次输入密码：

   在需要的仓库中先输入：

   git config credential.helper store //将密码存储到本地

   git pull

   之后再输入用户名密码就能保存到本地的~/.git-credentials中，在这里应该可以直接添加或修改用户名和密码。

6. git 最重要的就是使用man

   ​	(1) -rebase：修改多个提交或旧提交的信息。http://jartto.wang/2018/12/11/git-rebase/

   ​	使用 git rebase -i HEAD~n 命令在默认文本编辑器中显示最近 n 个提交的列表。

   ​

   1. *# 	Displays a list of the last 3 commits on the current branch*

   2. *$ 	git rebase -i HEAD~3*

   3.

   ​	 (2) -reset：man git-reset

   ​	 (3) -checkout：man git-commit, git checkout . 删除所有本地修改。

   ​	 (4) --amend：git commit –amend，重写head的commit消息。

   ​	 (5) format-patch：If you want to format only <commit> itself, you can do this with

   ​	 git format-patch -1 <commit>.

   ​	 (6) stash：The command saves your local modifications away and reverts the working directory to match the HEAD commit. 就是去掉unstaged modify。

   ​	 (7) restore：Restore specified paths in the working tree with some contents from a restore source.

   ​	 (8) log：·

   ​	 (9) reflog：

   ​	 (10) tig：查看提交的详细记录，包括代码的修改。

   ​	 (11) cherry-pick：对于多分支的代码库，将代码从一个分支转移到另一个分支有两种情况。一种情况是，合并另一个分支的所有代码变动，那么就采用合并（git merge）。另一种情况是，只需要合并部分代码变动（**某几个提交**），这时可以采用 Cherry pick。

   ​	rebase也可以合并冲突。在gerrit中，多人同时开发同一个分支，合并不同的代码修改就用git pull –rebase。

7. git add 可以加某个目录名从而添加所有该目录下的文件。

8. 在master-mirror分支上进行开发，如果master分支上有新的commit，要将该commit应用到master-mirror上。

   首先git checkout master-mirror，然后git rebase master，解决冲突后git rebase --continue，然后重新git add, commit。

   用git merge master同样能合并commit，但是这个merge会产生一个新的commit，如果merge频繁，会使得log丑陋。

9. 本地分支重命名(还没有推送到远程)

   ```
   git branch -m oldName newName
   ```

   ​	删除远程分支

   ```
   git push --delete origin oldName
   ```

   ​	上传新命名的本地分支

   ```
   git push origin newName
   ```

   ​	把修改后的本地分支与远程分支关联

   ```
   git branch --set-upstream-to origin/newName
   ```

10. git需要丢弃本地修改，同步到远程仓库（即遇到error: You have not concluded your merge (MERGE_HEAD exists).错误时）：

    ```
    git fetch –all
    git reset –hard origin/master
    git fetch
    ```

   ​	之后再正常的pull就行。

11. git在push是遇到问题

    ```
    squash commits first
    ```

    这是因为两个commit的Change-ID相同，先用git-rebase -i HEAD~~查看commit的情况，然后选择一个commit进行squash即可。

12. fit reset commitid只是改变git的指针，如果要将内容也切换过去，要用git reset –hard commitid或者是git checkout -f commitid, 不过这个命令会将本地的修改丢弃，有风险。

13. ferge conflict在code里修改之后要git add, git commit，这样conflict才算解决了。

14. fit status看到的未提交的文件可以用git checkout + filename删掉，如果有需要保存的文件，但是库里同步过来又会覆盖的，用git stash保存，然后在pull –rebase，之后再git stash apply恢复。

15. git diff的不用使用：

    ```
    git diff            // 工作区 vs 暂存区
    git diff head       // 工作区 vs 版本库
    git diff –cached    // 暂存区 vs 版本库
    ```

16. 在上传修改前一定记得要git pull，和最新的版本库合并，之后再git add, commit, push，不然太麻烦了。

17. 如果忘了pull，则git reset恢复到未push的commit的前一个commit，然后pull，解决冲突后再git stash apply，将所有的修改恢复回来，解决冲突，然后再进行git push。可以用git stash list查看所有的stash。

18. 之后开发要checkout一条新的分支，所有的修改都在这条新的Fix分支上进行，修改完后在push到主分支，然后删掉新分支。问题：如果fix分支落后主分支，可以直接push么。

19. 提交代码，push到GitHub上，突然出现这个问题。

    ```
    remote: Support for password authentication was removed on August 13, 2021. Please use a personal access token instead.
    remote: Please see https://github.blog/2020-12-15-token-authentication-requirements-for-git-operations/ for more information.
    fatal: unable to access 'https://github.com/zhoulujun/algorithm.git/': The requested URL returned error: 403
    ```

    原因：从8月13日起不在支持密码验证，改用personal access token 替代。

    方法：用ssh clone，设置密钥，将id_rsa.pub粘贴到githab上即可。

20. 用git rebase在本地合并多条commit后push到远程仓库，出现如下bug:

    ```
    guanshun@Jack-ubuntu ~/g/UFuture.github.io (master)> git push origin maste
    To github.com:UtopianFuture/UFuture.github.io.git
     ! [rejected]        master -> master (non-fast-forward)
    error: failed to push some refs to 'git@github.com:UtopianFuture/UFuture.g
    hint: Updates were rejected because the tip of your current branch is behi
    hint: its remote counterpart. Integrate the remote changes (e.g.
    hint: 'git pull ...') before pushing again.
    hint: See the 'Note about fast-forwards' in 'git push --help' for details.
    ```

原因是git检查到当前head落后与远程仓库，但这本就是我们想做的，所以用

```
git push -f origin master
```

强行推上去，覆盖远程仓库的commit。

21. git format-patch commit-id 可以生成对应的 patch， git apply xxx.patch 可以将 patch 集成到项目中。

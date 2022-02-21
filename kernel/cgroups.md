## CGROUPS

cgroups 是 Linux 内核提供的**一种可以限制单个进程或者多个进程所使用资源的机制**，可以对 cpu，内存等资源实现精细化的控制，目前越来越火的轻量级容器 Docker 就使用了 cgroups 提供的资源限制能力来完成 cpu，内存等部分的资源控制。

另外，也可以使用 cgroups 提供的精细化控制能力，限制某一个或者某一组进程的资源使用。比如在一个既部署了前端 web 服务，也部署了后端计算模块的八核服务器上，可以使用 cgroups 限制 web server 仅可以使用其中的六个核，把剩下的两个核留给后端计算模块。

### reference

[1] https://tech.meituan.com/2015/03/31/cgroups.html

## Debug Subsystem

### pstore

```
| pstore_register_kmsg // 回调函数为 pstore_dump
|	-> kmsg_dump_register
|		-> list_add_tail_rcu(&dumper->list, &dump_list)

| panic
|	-> kmsg_dump // list_for_each_entry_rcu(dumper, &dump_list, list)
|		-> pstore_dump
|			-> psz_pstore_write
|				-> psz_record_write
|					-> psz_zone_write
|						-> mtdpstore_panic_write
|							-> mtd_panic_write
|								-> block2mtd_panic_write
|									-> do_sync
|										-> ufshcd_queuecommand
```

```c
[    0.619861@14][            init][    T1] Kernel panic - not syncing:
[    0.619863@14][            init][    T1] Attempted to kill init! exitcode=0x00007f00
[    0.619866@14][            init][    T1] CPU: 14 PID: 1 Comm: init Not tainted 5.10.115-rt67+ #1
[    0.619868@14][            init][    T1] Hardware name: xxx
[    0.619869@14][            init][    T1] Call trace:
[    0.619870@14][            init][    T1]  dump_backtrace+0x0/0x1d4
[    0.619879@14][            init][    T1]  show_stack+0x1c/0x2c
[    0.619882@14][            init][    T1]  dump_stack+0xc8/0x120
[    0.619888@14][            init][    T1]  panic+0xcc/0x398
[    0.619889@14][            init][    T1]  do_exit+0x948/0x9c4
[    0.619892@14][            init][    T1]  do_group_exit+0x3c/0xa0
[    0.619894@14][            init][    T1]  __arm64_sys_exit_group+0x1c/0x20
[    0.619896@14][            init][    T1]  el0_svc_common.constprop.0+0x7c/0x1c0
[    0.619899@14][            init][    T1]  do_el0_svc+0x28/0x84
[    0.619901@14][            init][    T1]  el0_svc+0xc/0x14
[    0.619904@14][            init][    T1]  el0_sync_handler+0x100/0x10c
[    0.619906@14][            init][    T1]  el0_sync+0x168/0x180
```

```c
[    1.058767@ 9][            init][    T1] Call trace:
[    1.059392@ 9][            init][    T1]  ufshcd_queuecommand+0x4cc/0x540
[    1.060246@ 9][            init][    T1]  do_sync+0xbc/0x150
[    1.060960@ 9][            init][    T1]  block2mtd_panic_write+0xfc/0x134
[    1.061825@ 9][            init][    T1]  mtd_panic_write+0x94/0xcc
[    1.062615@ 9][            init][    T1]  mtdpstore_panic_write+0xf4/0x1cc
[    1.063478@ 9][            init][    T1]  psz_zone_write+0x184/0x1b0
[    1.064280@ 9][            init][    T1]  psz_pstore_write+0x130/0x330
[    1.065102@ 9][            init][    T1]  pstore_dump+0x1bc/0x360
[    1.065869@ 9][            init][    T1]  kmsg_dump+0xdc/0x240
[    1.066604@ 9][            init][    T1]  panic+0x1d0/0x398
[    1.067305@ 9][            init][    T1]  do_exit+0x948/0x9c4
[    1.068028@ 9][            init][    T1]  do_group_exit+0x3c/0xa0
[    1.068795@ 9][            init][    T1]  __arm64_sys_exit_group+0x1c/0x20
[    1.069659@ 9][            init][    T1]  el0_svc_common.constprop.0+0x7c/0x1c0
[    1.070578@ 9][            init][    T1]  do_el0_svc+0x28/0x84
[    1.071312@ 9][            init][    T1]  el0_svc+0xc/0x14
[    1.072003@ 9][            init][    T1]  el0_sync_handler+0x100/0x10c
[    1.072823@ 9][            init][    T1]  el0_sync+0x168/0x180
```


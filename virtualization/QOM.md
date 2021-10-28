## QOM

### 0. QOM 概念

QOM(QEMU Object Model) 是QEMU中对象的抽象层。QEMU需要模拟很多设备，每个设备天然的具有继承关系，如模拟CPU，需要模拟不同架构的CPU，如intel, arm, sparc等等，虽然架构不同，但是所有的CPU还是有一些共性的东西，如核数，线程数等等。而虽然是同一架构的CPU，不同的版本也有细微不同，不可能对每一种CPU都全部实现一次，所有采用C++中面对对象的概念来实现模拟。QOM就是在QEMU中用C实现的面向对象思想。

QOM整个结构分为3个部分：类型的注册、类型的初始化以及对象的初始化。

```

     / +------------+     type_init
     | |  类型的注册  | -> register_module_init
     | +------------+     type_register
     | +------------+
 QOM | | 类型的初始化 | -> type_initialize
     | +------------+
     | +------------+     object_new
     | | 对象的初始化 | -> object_initialize
     \ +------------+     object_initialize_with_type
```

### 1. 面向对象

面向对象的基本知识:

- 继承(inheritance)
- 静态成员(static field)
- 构造函数和析构函数(constructor and destructor)
- 多态(polymorphic)
  - 动态绑定(override)
  - 静态绑定(overload)
- 抽象类/虚基类(abstract class)
- 动态类型装换(dynamic cast)
- 接口(interface)

下面分析QEMU是怎样实现这些特性。

### 2. 类型的注册

在 QEMU 中用 `TypeInfo` 表示类

```c
struct TypeInfo
{
    const char *name;
    const char *parent;

    size_t instance_size;
    size_t instance_align;
    void (*instance_init)(Object *obj);
    void (*instance_post_init)(Object *obj);
    void (*instance_finalize)(Object *obj);

    bool abstract;
    size_t class_size;

    void (*class_init)(ObjectClass *klass, void *data);
    void (*class_base_init)(ObjectClass *klass, void *data);
    void *class_data;

    InterfaceInfo *interfaces;
};
```

如下面的 `x86_cpu_type_info` 就是声明了一个 x86_cpu 类。

```c
static const TypeInfo x86_cpu_type_info = {
    .name = TYPE_X86_CPU,                     // 类名
    .parent = TYPE_CPU,                       // 父类
    .instance_size = sizeof(X86CPU),          // 实例大小
    .instance_init = x86_cpu_initfn,          // 初始化实例，可以理解为c++中的构造函数
    .abstract = true,
    .class_size = sizeof(X86CPUClass),        // 类大小
    .class_init = x86_cpu_common_class_init,  // 类初始化函数
};
```

可以看到有 `parent` 变量，即 `x86_cpu_type_info` 的父类是 `TYPE_CPU` 类型，而 `TYPE_CPU` 又有父类。也就是说 QOM **通过结构体来实现继承**。

这里**静态成员是所有的对象共享的，而非静态的每一个对象都有一份**，这里 `X86CPU` 就是包含的就是非静态成员，而 `X86CPUClass` 描述的是静态的成员。

类通过 `type_init` 添加到一个哈希表中，之后就可以根据类名获取具体的类信息。

```c
type_init(x86_cpu_register_types)
```

而 `type_init` 是通过 `modult_init` 宏实现的

```c
#define type_init(function) module_init(function, MODULE_INIT_QOM)

#define module_init(function, type)                                         \
static void __attribute__((constructor)) do_qemu_init_ ## function(void)    \
{                                                                           \
    register_module_init(function, type);                               \
}
```

即各个类最终通过函数 `register_module_init` 注册到系统，其中 function 是每个类型都要实现的初始化函数，如上面的 `x86_cpu_register_types`。这里的 constructor 是编译器属性，带有这个属性的函数会早于 main 函数执行，也就是说所有的 QOM 类型注册在 main 之前就已经完成了。这里对 `register_module_init` 不做详细分析，只需要知道它把所有的类都加载到一个 `init_type_list` 表中，然后进行初始化。

我们着重分析每个类的注册函数，这里分析上面的 `register_module_init` ，它会调用 `type_register_static` 注册所有的 `x86_cpu` 类。

```
static void x86_cpu_register_types(void)
{
    int i;

    type_register_static(&x86_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        x86_register_cpudef_types(&builtin_x86_defs[i]);
    }
    type_register_static(&max_x86_cpu_type_info);
    type_register_static(&x86_base_cpu_type_info);
#if defined(CONFIG_KVM) || defined(CONFIG_HVF)
    type_register_static(&host_x86_cpu_type_info);
#endif
}
```

 `builtin_x86_defs` 是 QEMU 定义的一系列的 CPU 类型。

`type_register_static` 最终调用到 `type_register_internal` ，它的功能比较简单， `type_new`  通过 `TypeInfo` 构建出 `TypeImpl`，这两者包含的变量基本相同，其区别是 `TypeInfo` 保存静态注册的数据，而 `TypeImpl` 保存运行时数据。之后通过 `type_table_add` 将 `TypeImpl` 添加到一个哈希表中。

```c
static void type_table_add(TypeImpl *ti)
{
    assert(!enumerating_types);
    g_hash_table_insert(type_table_get(), (void *)ti->name, ti);
}

static TypeImpl *type_table_lookup(const char *name)
{
    return g_hash_table_lookup(type_table_get(), name);
}

static TypeImpl *type_new(const TypeInfo *info)
{
    TypeImpl *ti = g_malloc0(sizeof(*ti));
    int i;

    g_assert(info->name != NULL);

    if (type_table_lookup(info->name) != NULL) {
        fprintf(stderr, "Registering `%s' which already exists\n", info->name);
        abort();
    }

    ti->name = g_strdup(info->name);
    ti->parent = g_strdup(info->parent);

    ti->class_size = info->class_size;
    ti->instance_size = info->instance_size;
    ti->instance_align = info->instance_align;

    ti->class_init = info->class_init;
    ti->class_base_init = info->class_base_init;
    ti->class_data = info->class_data;

    ti->instance_init = info->instance_init;
    ti->instance_post_init = info->instance_post_init;
    ti->instance_finalize = info->instance_finalize;

    ti->abstract = info->abstract;

    for (i = 0; info->interfaces && info->interfaces[i].type; i++) {
        ti->interfaces[i].typename = g_strdup(info->interfaces[i].type);
    }
    ti->num_interfaces = i;

    return ti;
}

static TypeImpl *type_register_internal(const TypeInfo *info)
{
    TypeImpl *ti;
    ti = type_new(info);

    type_table_add(ti);
    return ti;
}
```

上面说到 `TypeImpl` 与 `TypeInfo` 的结构基本相同，这里我们看一下 `TypeImpl` 的结构

```c
struct TypeImpl
{
    const char *name;

    size_t class_size;

    size_t instance_size;
    size_t instance_align;

    void (*class_init)(ObjectClass *klass, void *data);
    void (*class_base_init)(ObjectClass *klass, void *data);

    void *class_data;

    void (*instance_init)(Object *obj);
    void (*instance_post_init)(Object *obj);
    void (*instance_finalize)(Object *obj);

    bool abstract;

    const char *parent;
    TypeImpl *parent_type;

    ObjectClass *class;

    int num_interfaces;
    InterfaceImpl interfaces[MAX_INTERFACES];
};
```

其他的变量都很好理解，在阅读源码的过程中，我发现 `ObjectClass *class` 变量很有用，之后很多地方都会通过这个变量直接获取到 `ObjectClass` ，后面会讲到它是所有类的父类。

到目前为止，QEMU 已经在一个哈希表中保存了所有类信息 `TypeImpl` ，之后就需要对这些类进行初始化。

### 3. 类型与对象的初始化

类的初始化是通过 `type_initialize` 实现的。

```c
static void type_initialize(TypeImpl *ti)
{
    TypeImpl *parent;

    if (ti->class) {
        return;
    }

    ti->class_size = type_class_get_size(ti);
    ti->instance_size = type_object_get_size(ti);
    /* Any type with zero instance_size is implicitly abstract.
     * This means interface types are all abstract.
     */
    if (ti->instance_size == 0) {
        ti->abstract = true;
    }
    if (type_is_ancestor(ti, type_interface)) {
        assert(ti->instance_size == 0);
        assert(ti->abstract);
        assert(!ti->instance_init);
        assert(!ti->instance_post_init);
        assert(!ti->instance_finalize);
        assert(!ti->num_interfaces);
    }
    ti->class = g_malloc0(ti->class_size);

    parent = type_get_parent(ti);
    if (parent) {
        type_initialize(parent);
        GSList *e;
        int i;

        g_assert(parent->class_size <= ti->class_size);
        g_assert(parent->instance_size <= ti->instance_size);
        memcpy(ti->class, parent->class, parent->class_size);
        ti->class->interfaces = NULL;

        for (e = parent->class->interfaces; e; e = e->next) {
            InterfaceClass *iface = e->data;
            ObjectClass *klass = OBJECT_CLASS(iface);

            type_initialize_interface(ti, iface->interface_type, klass->type);
        }

        for (i = 0; i < ti->num_interfaces; i++) {
            TypeImpl *t = type_get_by_name(ti->interfaces[i].typename);
            if (!t) {
                error_report("missing interface '%s' for object '%s'",
                             ti->interfaces[i].typename, parent->name);
                abort();
            }
            for (e = ti->class->interfaces; e; e = e->next) {
                TypeImpl *target_type = OBJECT_CLASS(e->data)->type;

                if (type_is_ancestor(target_type, t)) {
                    break;
                }
            }

            if (e) {
                continue;
            }

            type_initialize_interface(ti, t, t);
        }
    }

    ti->class->properties = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                                  object_property_free);

    ti->class->type = ti;

    while (parent) {
        if (parent->class_base_init) {
            parent->class_base_init(ti->class, ti->class_data);
        }
        parent = type_get_parent(parent);
    }

    if (ti->class_init) {
        ti->class_init(ti->class, ti->class_data);
    }
}
```

这里基本是对 `TypeImpl` 结构的逐一初始化，值的注意的是也要对所有的父类进行初始化。

光看代码理解不深刻，我们来看看执行过程，还是以前面的 `x86_cpu_type_info` 类为例子，每个类有两个初始化函数，分别是非静态变量 `x86_cpu_initfn` 和静态变量 `x86_cpu_common_class_init`。静态成员是所有的对象公用的，其初始化显然要发生在所有的对象初始化之前。

```
#0  x86_cpu_common_class_init (oc=0x555556767080, data=0x0) at ../target/i386/cpu.c:7396
#1  0x0000555555de15ae in type_initialize (ti=0x555556730e70) at ../qom/object.c:364
#2  0x0000555555de1319 in type_initialize (ti=0x555556735e50) at ../qom/object.c:312
#3  0x0000555555de2dbc in object_class_foreach_tramp (key=0x555556735fd0, value=0x555556735e50, opaque=0x7fffffffda90) at ../qom/object.c:1069
#4  0x00007ffff7d7e1b8 in g_hash_table_foreach () from /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
#5  0x0000555555de2e9f in object_class_foreach (fn=0x555555de3008 <object_class_get_list_tramp>, implements_type=0x55555602f9a5 "machine", include_abstract=false, opaque=0x7fffffffdae0)
    at ../qom/object.c:1091
#6  0x0000555555de308b in object_class_get_list (implements_type=0x55555602f9a5 "machine", include_abstract=false) at ../qom/object.c:1148
#7  0x0000555555c759d2 in select_machine () at ../softmmu/vl.c:1616
#8  0x0000555555c7a2d3 in qemu_init (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at ../softmmu/vl.c:3545
#9  0x0000555555818f05 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at ../softmmu/main.c:49
```

QEMU 在 `qemu_init` 时会调用 `select_machine` 来选择 `machine` 类型，然后调用 `object_class_get_list` 来获得所有该 `TYPE_MACHINE` 类型组成的链表，最终对链表中的每个元素都调用 `type_initialize` ，然后 `type_initialize` 根据不同类的 `class_init` 进行调用，如这里的 `x86_cpu_common_class_init` 。也就是说在开始确定 `machine` 类型的时候就会完成所有类型的初始化。注意，第2部分是注册所有的类型，是初始化某一个类型下所有的静态变量。

```c
static void x86_cpu_common_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    FeatureWord w;

    device_class_set_parent_realize(dc, x86_cpu_realizefn,
                                    &xcc->parent_realize);
    device_class_set_parent_unrealize(dc, x86_cpu_unrealizefn,
                                      &xcc->parent_unrealize);
    device_class_set_props(dc, x86_cpu_properties);

    device_class_set_parent_reset(dc, x86_cpu_reset, &xcc->parent_reset);
    cc->reset_dump_flags = CPU_DUMP_FPU | CPU_DUMP_CCOP;

    cc->class_by_name = x86_cpu_class_by_name;
    cc->parse_features = x86_cpu_parse_featurestr;
    cc->has_work = x86_cpu_has_work;

#ifdef CONFIG_TCG
    tcg_cpu_common_class_init(cc);
#endif /* CONFIG_TCG */

    cc->dump_state = x86_cpu_dump_state;
    cc->set_pc = x86_cpu_set_pc;
    cc->gdb_read_register = x86_cpu_gdb_read_register;
    cc->gdb_write_register = x86_cpu_gdb_write_register;
    cc->get_arch_id = x86_cpu_get_arch_id;
    cc->get_paging_enabled = x86_cpu_get_paging_enabled;

#ifndef CONFIG_USER_ONLY
    cc->asidx_from_attrs = x86_asidx_from_attrs;
    cc->get_memory_mapping = x86_cpu_get_memory_mapping;
    cc->get_phys_page_attrs_debug = x86_cpu_get_phys_page_attrs_debug;
    cc->get_crash_info = x86_cpu_get_crash_info;
    cc->write_elf64_note = x86_cpu_write_elf64_note;
    cc->write_elf64_qemunote = x86_cpu_write_elf64_qemunote;
    cc->write_elf32_note = x86_cpu_write_elf32_note;
    cc->write_elf32_qemunote = x86_cpu_write_elf32_qemunote;
    cc->vmsd = &vmstate_x86_cpu;
#endif /* !CONFIG_USER_ONLY */

    cc->gdb_arch_name = x86_gdb_arch_name;
#ifdef TARGET_X86_64
    cc->gdb_core_xml_file = "i386-64bit.xml";
    cc->gdb_num_core_regs = 66;
#else
    cc->gdb_core_xml_file = "i386-32bit.xml";
    cc->gdb_num_core_regs = 50;
#endif
    cc->disas_set_info = x86_disas_set_info;

    dc->user_creatable = true;

    object_class_property_add(oc, "family", "int",
                              x86_cpuid_version_get_family,
                              x86_cpuid_version_set_family, NULL, NULL);
    object_class_property_add(oc, "model", "int",
                              x86_cpuid_version_get_model,
                              x86_cpuid_version_set_model, NULL, NULL);
    object_class_property_add(oc, "stepping", "int",
                              x86_cpuid_version_get_stepping,
                              x86_cpuid_version_set_stepping, NULL, NULL);
    object_class_property_add_str(oc, "vendor",
                                  x86_cpuid_get_vendor,
                                  x86_cpuid_set_vendor);
    object_class_property_add_str(oc, "model-id",
                                  x86_cpuid_get_model_id,
                                  x86_cpuid_set_model_id);
    object_class_property_add(oc, "tsc-frequency", "int",
                              x86_cpuid_get_tsc_freq,
                              x86_cpuid_set_tsc_freq, NULL, NULL);
    /*
     * The "unavailable-features" property has the same semantics as
     * CpuDefinitionInfo.unavailable-features on the "query-cpu-definitions"
     * QMP command: they list the features that would have prevented the
     * CPU from running if the "enforce" flag was set.
     */
    object_class_property_add(oc, "unavailable-features", "strList",
                              x86_cpu_get_unavailable_features,
                              NULL, NULL, NULL);

#if !defined(CONFIG_USER_ONLY)
    object_class_property_add(oc, "crash-information", "GuestPanicInformation",
                              x86_cpu_get_crash_info_qom, NULL, NULL, NULL);
#endif

    for (w = 0; w < FEATURE_WORDS; w++) {
        int bitnr;
        for (bitnr = 0; bitnr < 64; bitnr++) {
            x86_cpu_register_feature_bit_props(xcc, w, bitnr);
        }
    }
}
```

之后是初始化非静态变量，即某个类私有的变量。

```
#0  x86_cpu_initfn (obj=0x55555699d430) at ../target/i386/cpu.c:7107
#1  0x0000555555de1618 in object_init_with_type (obj=0x55555699d430, ti=0x555556730e70) at ../qom/object.c:375
#2  0x0000555555de15fa in object_init_with_type (obj=0x55555699d430, ti=0x555556731080) at ../qom/object.c:371
#3  0x0000555555de1b73 in object_initialize_with_type (obj=0x55555699d430, size=42928, type=0x555556731080) at ../qom/object.c:517
#4  0x0000555555de22a8 in object_new_with_type (type=0x555556731080) at ../qom/object.c:732
#5  0x0000555555de2307 in object_new (typename=0x555556000a5f "qemu64-x86_64-cpu") at ../qom/object.c:747
#6  0x0000555555b27028 in x86_cpu_new (x86ms=0x5555568b9de0, apic_id=0, errp=0x5555566e9a98 <error_fatal>) at ../hw/i386/x86.c:106
#7  0x0000555555b27144 in x86_cpus_init (x86ms=0x5555568b9de0, default_cpu_version=1) at ../hw/i386/x86.c:138
#8  0x0000555555b2047a in pc_init1 (machine=0x5555568b9de0, host_type=0x555556008544 "i440FX-pcihost", pci_type=0x55555600853d "i440FX") at ../hw/i386/pc_piix.c:159
#9  0x0000555555b20efa in pc_init_v6_0 (machine=0x5555568b9de0) at ../hw/i386/pc_piix.c:427
#10 0x0000555555a8c6e9 in machine_run_board_init (machine=0x5555568b9de0) at ../hw/core/machine.c:1232
#11 0x0000555555c77b73 in qemu_init_board () at ../softmmu/vl.c:2514
#12 0x0000555555c77d52 in qmp_x_exit_preconfig (errp=0x5555566e9a98 <error_fatal>) at ../softmmu/vl.c:2588
#13 0x0000555555c7a45f in qemu_init (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at ../softmmu/vl.c:3611
#14 0x0000555555818f05 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at ../softmmu/main.c:49
```

在 smp 结构中初始化所有的 CPU。

```c
void x86_cpus_init(X86MachineState *x86ms, int default_cpu_version)
{
    int i;
    const CPUArchIdList *possible_cpus;
    MachineState *ms = MACHINE(x86ms);
    MachineClass *mc = MACHINE_GET_CLASS(x86ms);

    x86_cpu_set_default_version(default_cpu_version);

    /*
     * Calculates the limit to CPU APIC ID values
     *
     * Limit for the APIC ID value, so that all
     * CPU APIC IDs are < x86ms->apic_id_limit.
     *
     * This is used for FW_CFG_MAX_CPUS. See comments on fw_cfg_arch_create().
     */
    x86ms->apic_id_limit = x86_cpu_apic_id_from_index(x86ms,
                                                      ms->smp.max_cpus - 1) + 1;
    possible_cpus = mc->possible_cpu_arch_ids(ms);
    for (i = 0; i < ms->smp.cpus; i++) {
        x86_cpu_new(x86ms, possible_cpus->cpus[i].arch_id, &error_fatal);
    }
}
```

这里由 `cpu_type` 这个 `char *` 变量就可以找到 `TypeImpl` 类名。

```c
void x86_cpu_new(X86MachineState *x86ms, int64_t apic_id, Error **errp)
{
    Object *cpu = object_new(MACHINE(x86ms)->cpu_type);

    if (!object_property_set_uint(cpu, "apic-id", apic_id, errp)) {
        goto out;
    }
    qdev_realize(DEVICE(cpu), NULL, errp);

out:
    object_unref(cpu);
}
```

```c
Object *object_new(const char *typename)
{
    TypeImpl *ti = type_get_by_name(typename);

    return object_new_with_type(ti);
}
```

```c
static Object *object_new_with_type(Type type)
{
    Object *obj;
    size_t size, align;
    void (*obj_free)(void *);

    g_assert(type != NULL);
    type_initialize(type);

    size = type->instance_size;
    align = type->instance_align;

    /*
     * Do not use qemu_memalign unless required.  Depending on the
     * implementation, extra alignment implies extra overhead.
     */
    if (likely(align <= __alignof__(qemu_max_align_t))) {
        obj = g_malloc(size);
        obj_free = g_free;
    } else {
        obj = qemu_memalign(align, size);
        obj_free = qemu_vfree;
    }

    object_initialize_with_type(obj, size, type);
    obj->free = obj_free;

    return obj;
}
```

```c
static void object_initialize_with_type(Object *obj, size_t size, TypeImpl *type)
{
    type_initialize(type);

    g_assert(type->instance_size >= sizeof(Object));
    g_assert(type->abstract == false);
    g_assert(size >= type->instance_size);

    memset(obj, 0, type->instance_size);
    obj->class = type->class;
    object_ref(obj);
    object_class_property_init_all(obj);
    obj->properties = g_hash_table_new_full(g_str_hash, g_str_equal,
                                            NULL, object_property_free);
    object_init_with_type(obj, type);
    object_post_init_with_type(obj, type);
}
```

这里还要初始化父类，如果有的话。`instance_init` 就是 `x86_cpu_initfn` 。

```c
static void object_init_with_type(Object *obj, TypeImpl *ti)
{
    if (type_has_parent(ti)) {
        object_init_with_type(obj, type_get_parent(ti));
    }

    if (ti->instance_init) {
        ti->instance_init(obj);
    }
}
```

```c
static void x86_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(obj);
    CPUX86State *env = &cpu->env;

    env->nr_dies = 1;
    cpu_set_cpustate_pointers(cpu);

    object_property_add(obj, "feature-words", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)env->features);
    object_property_add(obj, "filtered-features", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)cpu->filtered_features);

    object_property_add_alias(obj, "sse3", obj, "pni");
    object_property_add_alias(obj, "pclmuldq", obj, "pclmulqdq");
    object_property_add_alias(obj, "sse4-1", obj, "sse4.1");
    object_property_add_alias(obj, "sse4-2", obj, "sse4.2");
    object_property_add_alias(obj, "xd", obj, "nx");
    object_property_add_alias(obj, "ffxsr", obj, "fxsr-opt");
    object_property_add_alias(obj, "i64", obj, "lm");

    object_property_add_alias(obj, "ds_cpl", obj, "ds-cpl");
    object_property_add_alias(obj, "tsc_adjust", obj, "tsc-adjust");
    object_property_add_alias(obj, "fxsr_opt", obj, "fxsr-opt");
    object_property_add_alias(obj, "lahf_lm", obj, "lahf-lm");
    object_property_add_alias(obj, "cmp_legacy", obj, "cmp-legacy");
    object_property_add_alias(obj, "nodeid_msr", obj, "nodeid-msr");
    object_property_add_alias(obj, "perfctr_core", obj, "perfctr-core");
    object_property_add_alias(obj, "perfctr_nb", obj, "perfctr-nb");
    object_property_add_alias(obj, "kvm_nopiodelay", obj, "kvm-nopiodelay");
    object_property_add_alias(obj, "kvm_mmu", obj, "kvm-mmu");
    object_property_add_alias(obj, "kvm_asyncpf", obj, "kvm-asyncpf");
    object_property_add_alias(obj, "kvm_asyncpf_int", obj, "kvm-asyncpf-int");
    object_property_add_alias(obj, "kvm_steal_time", obj, "kvm-steal-time");
    object_property_add_alias(obj, "kvm_pv_eoi", obj, "kvm-pv-eoi");
    object_property_add_alias(obj, "kvm_pv_unhalt", obj, "kvm-pv-unhalt");
    object_property_add_alias(obj, "kvm_poll_control", obj, "kvm-poll-control");
    object_property_add_alias(obj, "svm_lock", obj, "svm-lock");
    object_property_add_alias(obj, "nrip_save", obj, "nrip-save");
    object_property_add_alias(obj, "tsc_scale", obj, "tsc-scale");
    object_property_add_alias(obj, "vmcb_clean", obj, "vmcb-clean");
    object_property_add_alias(obj, "pause_filter", obj, "pause-filter");
    object_property_add_alias(obj, "sse4_1", obj, "sse4.1");
    object_property_add_alias(obj, "sse4_2", obj, "sse4.2");

    if (xcc->model) {
        x86_cpu_load_model(cpu, xcc->model);
    }
}
```

经过这样一系列的各种父类，子类的注册，初始化，才能最终向上层提供完整的 `x86_cpu` 的模拟。

### 4. 类型的层次结构

QEMU 的继承关系如下：

```c
struct X86CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
};
```

```c
struct CPUClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
};
```

```c
struct DeviceClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/
};
```

```c
struct ObjectClass
{
    /* private: */
    Type type;
    GSList *interfaces;

    const char *object_cast_cache[OBJECT_CLASS_CAST_CACHE];
    const char *class_cast_cache[OBJECT_CLASS_CAST_CACHE];

    ObjectUnparent *unparent;

    GHashTable *properties;
};
```

**QEMU 中所有的对象的 parent 是 Object 和 ObjectClass**。Object 存储 Non-static 部分，而 ObjectClass 存储 static 部分。

QEMU 中的类型转换是通过 `object_class_dynamic_cast` 完成的。如上面的 `x86_cpu_initfn` ，它的形参是 `Object` ，但它要初始化的是 `X86CPU` 。

```c
ObjectClass *object_class_dynamic_cast(ObjectClass *class,
                                       const char *typename)
{
    ObjectClass *ret = NULL;
    TypeImpl *target_type;
    TypeImpl *type;

    if (!class) {
        return NULL;
    }

    /* A simple fast path that can trigger a lot for leaf classes.  */
    type = class->type;
    if (type->name == typename) {
        return class;
    }

    target_type = type_get_by_name(typename);
    if (!target_type) {
        /* target class type unknown, so fail the cast */
        return NULL;
    }

    if (type->class->interfaces &&
            type_is_ancestor(target_type, type_interface)) {
        int found = 0;
        GSList *i;

        for (i = class->interfaces; i; i = i->next) {
            ObjectClass *target_class = i->data;

            if (type_is_ancestor(target_class->type, target_type)) {
                ret = target_class;
                found++;
            }
         }

        /* The match was ambiguous, don't allow a cast */
        if (found > 1) {
            ret = NULL;
        }
    } else if (type_is_ancestor(type, target_type)) {
        ret = class;
    }

    return ret;
}
```

目前为止，QEMU 完成了类型的注册，即为每个类型指定一个 `TypeInfo` 注册到系统中，然后将 `TypeInfo` 转化成 `TypeImpl` 放到一个哈希表中。之后系统会通过 `type_initialize` 对这个哈希表进行初始化，主要是设置 `TypeImpl` 的一些域以及调用类型的 `class_init` 函数，如上面的 `x86_cpu_common_class_init` 和 `x86_cpu_initfn` 。这里要理清不同的函数是初始化哪个数据结构的。

- `type_initialize` : TypeImpl 。
- `x86_cpu_common_class_init` : CPUClass，因为个是继承来的，数据公共类。
- `x86_cpu_initfn` : X86CPU, 具体的子类。

`x86_cpu_common_class_init` 和 `x86_cpu_initfn` 就是具体的对象初始化，执行流程上面已经给出了。

/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43COMPAT_DEVICE_H_
#define B43COMPAT_DEVICE_H_

/*
 * b43compat_device.h - Linux device model (linux/device.h, linux/pci.h,
 * linux/mod_devicetable.h, linux/io.h), just enough to run drivers/ssb and
 * b43 unmodified.
 *
 * The "driver core" is stripped down to the essentials but follows the same
 * logic as Linux: bus_register(), device_register() and driver_register()
 * match devices and drivers with bus->match() and call bus->probe(). The PCI
 * bus has a single device: the AirPort card (b43compat_pci_host).
 *
 * Included by b43compat.h.
 */

/* =========================================================================
 * linux/mod_devicetable.h (same definitions as Linux 5.15)
 * ========================================================================= */
typedef unsigned long kernel_ulong_t;

#define PCI_ANY_ID (~0)

struct pci_device_id {
    __u32 vendor, device;           /* Vendor and device ID or PCI_ANY_ID*/
    __u32 subvendor, subdevice;     /* Subsystem ID's or PCI_ANY_ID */
    __u32 class, class_mask;        /* (class,subclass,prog-if) triplet */
    kernel_ulong_t driver_data;     /* Data private to the driver */
    __u32 override_only;
};

struct ssb_device_id {
    __u16 vendor;
    __u16 coreid;
    __u8  revision;
    __u8  __pad;
} __attribute__((packed, aligned(2)));
#define SSB_DEVICE(_vendor, _coreid, _revision)  \
    { .vendor = _vendor, .coreid = _coreid, .revision = _revision, }

#define SSB_ANY_VENDOR      0xFFFF
#define SSB_ANY_ID          0xFFFF
#define SSB_ANY_REV         0xFF

#define MODULE_DEVICE_TABLE(type, name) extern int b43compat_decl_dummy

/* =========================================================================
 * linux/pm.h
 * ========================================================================= */
struct device;
typedef struct pm_message { int event; } pm_message_t;
struct dev_pm_ops { int (*suspend)(struct device *dev); int (*resume)(struct device *dev); };
#define SET_SYSTEM_SLEEP_PM_OPS(s, r)
struct dev_pm_info { bool wakeup_path; };

/* =========================================================================
 * linux/module.h
 * ========================================================================= */
struct module { int dummy; };
#define THIS_MODULE             ((struct module *)0)
#define MODULE_AUTHOR(x) extern int b43compat_decl_dummy
#define MODULE_DESCRIPTION(x) extern int b43compat_decl_dummy
#define MODULE_LICENSE(x) extern int b43compat_decl_dummy
#define MODULE_FIRMWARE(x) extern int b43compat_decl_dummy
#define MODULE_ALIAS(x) extern int b43compat_decl_dummy
#define MODULE_ALIAS_GENL_FAMILY(f) extern int b43compat_decl_dummy
#define MODULE_INFO(t, i) extern int b43compat_decl_dummy
#define MODULE_VERSION(v) extern int b43compat_decl_dummy
#define MODULE_SOFTDEP(s) extern int b43compat_decl_dummy
#define MODULE_IMPORT_NS(n) extern int b43compat_decl_dummy
#define EXPORT_SYMBOL_GPL(x) extern int b43compat_decl_dummy

/* Module init functions become functions with a known name, which the
 * Mac OS 9 glue calls in the order Linux would run them:
 *   b43compat_initcall_<function>(), b43compat_exitcall_<function>()
 * They are function definitions, so the trailing ';' (which may or may not
 * be present in Linux) changes nothing. */
#define b43compat_initcall(fn)  int b43compat_initcall_##fn(void) { return fn(); }
#define fs_initcall(fn)         b43compat_initcall(fn)
#define subsys_initcall(fn)     b43compat_initcall(fn)
#define device_initcall(fn)     b43compat_initcall(fn)
#define module_init(fn)         b43compat_initcall(fn)
#define module_exit(fn)         void b43compat_exitcall_##fn(void) { fn(); }

/* =========================================================================
 * linux/device.h, linux/sysfs.h
 * ========================================================================= */
typedef unsigned short umode_t;
typedef long ssize_t;
typedef long long loff_t;

struct device;
struct device_driver;
struct bus_type;
struct kobj_uevent_env { int dummy; };

struct attribute {
    const char *name;
    umode_t mode;
};

struct attribute_group {
    const char *name;
    struct attribute **attrs;
};

struct device_attribute {
    struct attribute attr;
    ssize_t (*show)(struct device *dev, struct device_attribute *attr, char *buf);
    ssize_t (*store)(struct device *dev, struct device_attribute *attr,
                     const char *buf, size_t count);
};

#define __ATTR(_name, _mode, _show, _store) \
    { .attr = { .name = #_name, .mode = _mode }, .show = _show, .store = _store }
#define __ATTR_RO(_name)        __ATTR(_name, 0444, _name##_show, NULL)
#define __ATTR_RW(_name)        __ATTR(_name, 0644, _name##_show, _name##_store)
#define DEVICE_ATTR_RO(_name)   struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define DEVICE_ATTR_RW(_name)   struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#define DEVICE_ATTR_ADMIN_RW(_name) \
    struct device_attribute dev_attr_##_name = __ATTR(_name, 0600, _name##_show, _name##_store)
#define ATTRIBUTE_GROUPS(_name)                                         \
    static const struct attribute_group _name##_group = {               \
        .attrs = _name##_attrs,                                         \
    };                                                                  \
    static const struct attribute_group *_name##_groups[] = {           \
        &_name##_group,                                                 \
        NULL,                                                           \
    }

struct bus_type {
    const char *name;
    const struct attribute_group **dev_groups;
    int  (*match)(struct device *dev, struct device_driver *drv);
    int  (*uevent)(struct device *dev, struct kobj_uevent_env *env);
    int  (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    void (*shutdown)(struct device *dev);
    int  (*suspend)(struct device *dev, pm_message_t state);
    int  (*resume)(struct device *dev);
    /* Mac OS 9: lists of the bus's devices and drivers */
    struct device *b43c_devices;
    struct device_driver *b43c_drivers;
};

struct device_driver {
    const char *name;
    struct bus_type *bus;
    struct module *owner;
    const struct dev_pm_ops *pm;
    /* Mac OS 9 */
    struct device_driver *b43c_next;
};

struct device_type;
struct class;
struct device {
    struct device *parent;
    struct kobject_b43c { const char *name; } kobj;
    const struct device_type *type;
    struct class *class;
    const struct attribute_group **groups;
    void *platform_data;
    const char *init_name;
    struct bus_type *bus;
    struct device_driver *driver;
    void *driver_data;
    u64 *dma_mask;
    u64 coherent_dma_mask;
    struct dev_pm_info power;
    void (*release)(struct device *dev);
    /* Mac OS 9 */
    char b43c_name[32];
    int b43c_registered;
    struct device *b43c_next;
};

int  bus_register(struct bus_type *bus);
void bus_unregister(struct bus_type *bus);
int  device_register(struct device *dev);
void device_unregister(struct device *dev);
struct device *get_device(struct device *dev);
void put_device(struct device *dev);
int  dev_set_name(struct device *dev, const char *fmt, ...);
const char *dev_name(const struct device *dev);
int  driver_register(struct device_driver *drv);
void driver_unregister(struct device_driver *drv);
#define device_is_registered(dev)       ((dev)->b43c_registered)
#define device_create_file(dev, attr)   0
#define device_remove_file(dev, attr)   do { } while (0)
#define device_set_wakeup_enable(d, e)  0
int  add_uevent_var(struct kobj_uevent_env *env, const char *fmt, ...);
static inline void *dev_get_drvdata(const struct device *dev) { return dev->driver_data; }
static inline void dev_set_drvdata(struct device *dev, void *data) { dev->driver_data = data; }

/* Device logging: same formats as Linux, prefixed with the name */
void b43compat_dev_printk(const char *level, const struct device *dev, const char *fmt, ...);
#define dev_emerg(dev, ...)     b43compat_dev_printk(KERN_EMERG, dev, __VA_ARGS__)
#define dev_err(dev, ...)       b43compat_dev_printk(KERN_ERR, dev, __VA_ARGS__)
#define dev_warn(dev, ...)      b43compat_dev_printk(KERN_WARNING, dev, __VA_ARGS__)
#define dev_notice(dev, ...)    b43compat_dev_printk(KERN_NOTICE, dev, __VA_ARGS__)
#define dev_info(dev, ...)      b43compat_dev_printk(KERN_INFO, dev, __VA_ARGS__)
#define dev_dbg(dev, ...)       b43compat_dev_printk(KERN_DEBUG, dev, __VA_ARGS__)

/* linux/platform_device.h: used only by embedded configurations */
struct platform_device { const char *name; int id; struct device dev; };
#define platform_device_register(p)     0
#define platform_device_unregister(p)   do { } while (0)

/* linux/dma-mapping.h */
#define DMA_BIT_MASK(n)         (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))

/* =========================================================================
 * linux/pci.h
 * ========================================================================= */
#include <linux/pci_ids.h>     /* Linux, unmodified */

#define PCI_DEVICE(vend,dev)     .vendor = (vend), .device = (dev),     .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/* linux/pci_regs.h */
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define  PCI_COMMAND_IO         0x1
#define  PCI_COMMAND_MEMORY     0x2
#define  PCI_COMMAND_MASTER     0x4
#define PCI_STATUS              0x06
#define  PCI_STATUS_SIG_TARGET_ABORT 0x800
#define PCI_REVISION_ID         0x08
#define PCI_LATENCY_TIMER       0x0d
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c
#define PCI_SUBSYSTEM_ID        0x2e
#define PCI_INTERRUPT_LINE      0x3c

typedef int pci_power_t;
#define PCI_D0                  0
#define PCI_D3hot               3

struct pci_dev;
struct pci_driver {
    const char *name;
    const struct pci_device_id *id_table;
    int  (*probe)(struct pci_dev *dev, const struct pci_device_id *id);
    void (*remove)(struct pci_dev *dev);
    struct device_driver driver;
};

struct pci_dev {
    unsigned short vendor;
    unsigned short device;
    unsigned short subsystem_vendor;
    unsigned short subsystem_device;
    unsigned int class;
    u8 revision;
    struct pci_driver *driver;
    struct device dev;
    unsigned int irq;
    /* Mac OS 9: BAR0 already mapped by Mac OS ("AAPL,address") */
    void __iomem *b43c_bar0;
};
#define to_pci_dev(n)           container_of(n, struct pci_dev, dev)

/* The card's PCI device, filled in by the Mac OS 9 glue */
extern struct pci_dev b43compat_pci_host;

int  pci_register_driver(struct pci_driver *drv);
void pci_unregister_driver(struct pci_driver *drv);
int  pci_enable_device(struct pci_dev *dev);
void pci_disable_device(struct pci_dev *dev);
void pci_set_master(struct pci_dev *dev);
#define pci_request_regions(dev, name)  0
#define pci_release_regions(dev)        do { } while (0)
#define pci_set_power_state(dev, s)     ((void)0)
#define pci_save_state(dev)             0
#define pci_restore_state(dev)          do { } while (0)
#define pci_is_pcie(dev)                false
#define pci_name(dev)                   dev_name(&(dev)->dev)
#define pci_get_drvdata(pdev)           dev_get_drvdata(&(pdev)->dev)
#define pci_set_drvdata(pdev, data)     dev_set_drvdata(&(pdev)->dev, data)
int  pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val);
int  pci_read_config_word(const struct pci_dev *dev, int where, u16 *val);
int  pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val);
int  pci_write_config_dword(const struct pci_dev *dev, int where, u32 val);
int  pci_write_config_word(const struct pci_dev *dev, int where, u16 val);
int  pci_write_config_byte(const struct pci_dev *dev, int where, u8 val);
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen);
#define pci_iounmap(dev, addr)          do { } while (0)

/* =========================================================================
 * linux/io.h: little-endian PCI bus registers (lhbrx/sthbrx + eieio)
 * ========================================================================= */
u8   ioread8(const void __iomem *addr);
u16  ioread16(const void __iomem *addr);
u32  ioread32(const void __iomem *addr);
void iowrite8(u8 value, void __iomem *addr);
void iowrite16(u16 value, void __iomem *addr);
void iowrite32(u32 value, void __iomem *addr);
/* The _rep versions do not swap bytes: memory receives the same byte stream
 * that is on the bus (as on Linux PowerPC) */
void ioread8_rep(const void __iomem *addr, void *buf, unsigned long count);
void ioread16_rep(const void __iomem *addr, void *buf, unsigned long count);
void ioread32_rep(const void __iomem *addr, void *buf, unsigned long count);
void iowrite8_rep(void __iomem *addr, const void *buf, unsigned long count);
void iowrite16_rep(void __iomem *addr, const void *buf, unsigned long count);
void iowrite32_rep(void __iomem *addr, const void *buf, unsigned long count);
/* Only for system SSB buses (MIPS SoCs): never used on PCI */
/* readw/writew & co. (little-endian accesses like ioread/iowrite) */
#define readb(a)                ioread8(a)
#define readw(a)                ioread16(a)
#define readl(a)                ioread32(a)
#define writeb(v, a)            iowrite8(v, a)
#define writew(v, a)            iowrite16(v, a)
#define writel(v, a)            iowrite32(v, a)
#define ioremap(addr, size)     ((void __iomem *)NULL)
#define iounmap(addr)           do { } while (0)

#endif /* B43COMPAT_DEVICE_H_ */

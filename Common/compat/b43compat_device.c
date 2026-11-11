/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43compat_device.c - Driver core, PCI and register access for Mac OS 9.
 *
 * Reproduces the behavior of drivers/base (buses, devices, drivers) and of
 * drivers/pci to the extent used by drivers/ssb and b43. There is a single
 * PCI device, the AirPort card: b43compat_pci_host.
 */
#include <linux/types.h>
#include "b43compat_os9.h"

/* =========================================================================
 * Driver core (drivers/base/bus.c, dd.c, core.c)
 * ========================================================================= */

/* dd.c: really_probe() */
static int b43c_really_probe(struct device *dev, struct device_driver *drv)
{
    int err;

    dev->driver = drv;
    if (dev->bus && dev->bus->probe)
        err = dev->bus->probe(dev);
    else
        err = 0;
    if (err) {
        dev->driver = NULL;
        printk("%s: probe of %s failed (%d)\n", drv->name, dev_name(dev), err);
    }
    return err;
}

/* bus.c/dd.c: __driver_attach() / __device_attach() */
static void b43c_try_attach(struct device *dev, struct device_driver *drv)
{
    if (dev->driver || !dev->b43c_registered)
        return;
    if (dev->bus->match && !dev->bus->match(dev, drv))
        return;
    b43c_really_probe(dev, drv);
}

int bus_register(struct bus_type *bus)
{
    bus->b43c_devices = NULL;
    bus->b43c_drivers = NULL;
    return 0;
}

void bus_unregister(struct bus_type *bus)
{
    (void)bus;
}

int device_register(struct device *dev)
{
    struct device_driver *drv;
    struct device **pp;

    if (!dev->b43c_name[0] && dev->init_name)
        dev_set_name(dev, "%s", dev->init_name);
    dev->b43c_registered = 1;
    if (!dev->bus)
        return 0;
    /* append: same enumeration order as Linux */
    dev->b43c_next = NULL;
    for (pp = &dev->bus->b43c_devices; *pp; pp = &(*pp)->b43c_next)
        ;
    *pp = dev;
    for (drv = dev->bus->b43c_drivers; drv && !dev->driver; drv = drv->b43c_next)
        b43c_try_attach(dev, drv);
    return 0;
}

void device_unregister(struct device *dev)
{
    struct device **pp;

    if (dev->bus) {
        if (dev->driver && dev->bus->remove)
            dev->bus->remove(dev);
        for (pp = &dev->bus->b43c_devices; *pp; pp = &(*pp)->b43c_next) {
            if (*pp == dev) {
                *pp = dev->b43c_next;
                break;
            }
        }
    }
    dev->driver = NULL;
    dev->b43c_registered = 0;
    put_device(dev);
}

/* Reference counting is not implemented: release() is only called by
 * device_unregister(), as in the normal Linux case */
struct device *get_device(struct device *dev)
{
    return dev;
}

void put_device(struct device *dev)
{
    if (dev && !dev->b43c_registered && dev->release)
        dev->release(dev);
}

int dev_set_name(struct device *dev, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    b43c_vsnprintf(dev->b43c_name, sizeof(dev->b43c_name), fmt, ap);
    va_end(ap);
    return 0;
}

const char *dev_name(const struct device *dev)
{
    if (dev->b43c_name[0])
        return dev->b43c_name;
    return dev->init_name ? dev->init_name : "";
}

int driver_register(struct device_driver *drv)
{
    struct device_driver **pp;
    struct device *dev;

    if (!drv->bus)
        return -EINVAL;
    drv->b43c_next = NULL;
    for (pp = &drv->bus->b43c_drivers; *pp; pp = &(*pp)->b43c_next)
        ;
    *pp = drv;
    for (dev = drv->bus->b43c_devices; dev; dev = dev->b43c_next)
        b43c_try_attach(dev, drv);
    return 0;
}

void driver_unregister(struct device_driver *drv)
{
    struct device_driver **pp;
    struct device *dev;

    if (!drv->bus)
        return;
    for (dev = drv->bus->b43c_devices; dev; dev = dev->b43c_next) {
        if (dev->driver == drv) {
            if (dev->bus->remove)
                dev->bus->remove(dev);
            dev->driver = NULL;
        }
    }
    for (pp = &drv->bus->b43c_drivers; *pp; pp = &(*pp)->b43c_next) {
        if (*pp == drv) {
            *pp = drv->b43c_next;
            break;
        }
    }
}

int add_uevent_var(struct kobj_uevent_env *env, const char *fmt, ...)
{
    (void)env;
    (void)fmt;
    return 0;
}

void b43compat_dev_printk(const char *level, const struct device *dev, const char *fmt, ...)
{
    char prefix[40];
    va_list ap;

    (void)level;
    b43c_snprintf(prefix, sizeof(prefix), "%s: ", dev ? dev_name(dev) : "(NULL)");
    va_start(ap, fmt);
    b43compat_log_va(prefix, fmt, ap);
    va_end(ap);
}

/* =========================================================================
 * PCI (drivers/pci/pci-driver.c, access.c)
 * ========================================================================= */
struct pci_dev b43compat_pci_host;

void b43compat_pci_host_init(void __iomem *bar0, const char *name)
{
    struct pci_dev *p = &b43compat_pci_host;
    u32 v;

    memset(p, 0, sizeof(*p));
    pci_read_config_dword(p, PCI_VENDOR_ID, &v);
    p->vendor = (unsigned short)(v & 0xFFFF);
    p->device = (unsigned short)(v >> 16);
    pci_read_config_dword(p, PCI_REVISION_ID, &v);
    p->revision = (u8)(v & 0xFF);
    p->class = v >> 8;
    pci_read_config_dword(p, PCI_SUBSYSTEM_VENDOR_ID, &v);
    p->subsystem_vendor = (unsigned short)(v & 0xFFFF);
    p->subsystem_device = (unsigned short)(v >> 16);
    pci_read_config_dword(p, PCI_INTERRUPT_LINE, &v);
    p->irq = v & 0xFF;
    p->b43c_bar0 = bar0;
    p->dev.init_name = name;
    dev_set_name(&p->dev, "%s", name);
}

/* pci-driver.c: pci_match_one_device() */
static const struct pci_device_id *b43c_pci_match(const struct pci_device_id *id,
                                                  const struct pci_dev *dev)
{
    for (; id->vendor || id->subvendor || id->class_mask; id++) {
        if ((id->vendor == PCI_ANY_ID || id->vendor == dev->vendor) &&
            (id->device == PCI_ANY_ID || id->device == dev->device) &&
            (id->subvendor == PCI_ANY_ID || id->subvendor == dev->subsystem_vendor) &&
            (id->subdevice == PCI_ANY_ID || id->subdevice == dev->subsystem_device) &&
            !((id->class ^ dev->class) & id->class_mask))
            return id;
    }
    return NULL;
}

int pci_register_driver(struct pci_driver *drv)
{
    struct pci_dev *dev = &b43compat_pci_host;
    const struct pci_device_id *id;
    int err;

    drv->driver.name = drv->name;
    if (dev->driver)
        return 0;
    id = b43c_pci_match(drv->id_table, dev);
    if (!id)
        return 0;
    dev->driver = drv;
    err = drv->probe(dev, id);
    if (err) {
        dev->driver = NULL;
        printk("%s: probe of %s failed (%d)\n", drv->name, pci_name(dev), err);
    }
    return 0;
}

void pci_unregister_driver(struct pci_driver *drv)
{
    struct pci_dev *dev = &b43compat_pci_host;

    if (dev->driver == drv) {
        if (drv->remove)
            drv->remove(dev);
        dev->driver = NULL;
    }
}

static void b43c_pci_command_set(struct pci_dev *dev, u16 bits, int on)
{
    u16 cmd;

    pci_read_config_word(dev, PCI_COMMAND, &cmd);
    if (on)
        cmd |= bits;
    else
        cmd &= (u16)~bits;
    pci_write_config_word(dev, PCI_COMMAND, cmd);
}

int pci_enable_device(struct pci_dev *dev)
{
    b43c_pci_command_set(dev, PCI_COMMAND_MEMORY, 1);
    return 0;
}

void pci_disable_device(struct pci_dev *dev)
{
    b43c_pci_command_set(dev, PCI_COMMAND_MASTER, 0);
}

void pci_set_master(struct pci_dev *dev)
{
    b43c_pci_command_set(dev, PCI_COMMAND_MASTER, 1);
}

/* The configuration space is only accessible in 32-bit words
 * (ExpMgrConfigReadLong/WriteLong): bytes and halfwords are derived from them.
 * Halfwords and bytes are little-endian within the word, as on PCI. */
int pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val)
{
    (void)dev;
    *val = (u32)b43compat_env->pci_cfg_read32(b43compat_env->ctx,
                                              (unsigned short)(where & ~3));
    return 0;
}

int pci_write_config_dword(const struct pci_dev *dev, int where, u32 val)
{
    (void)dev;
    b43compat_env->pci_cfg_write32(b43compat_env->ctx,
                                   (unsigned short)(where & ~3), val);
    return 0;
}

int pci_read_config_word(const struct pci_dev *dev, int where, u16 *val)
{
    u32 v;
    pci_read_config_dword(dev, where, &v);
    *val = (u16)(v >> ((where & 2) * 8));
    return 0;
}

int pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val)
{
    u32 v;
    pci_read_config_dword(dev, where, &v);
    *val = (u8)(v >> ((where & 3) * 8));
    return 0;
}

/* Partial write: the rest of the word is rewritten with the value read.
 * For PCI_COMMAND/PCI_STATUS ("write 1 to clear" bits) the untouched
 * halfword is written as zero, so nothing gets cleared. */
int pci_write_config_word(const struct pci_dev *dev, int where, u16 val)
{
    int shift = (where & 2) * 8;
    u32 v;

    pci_read_config_dword(dev, where, &v);
    if ((where & ~3) == PCI_COMMAND)
        v &= (shift ? 0x0000FFFFUL : 0x00000000UL);
    v &= ~(0xFFFFUL << shift);
    v |= (u32)val << shift;
    return pci_write_config_dword(dev, where, v);
}

int pci_write_config_byte(const struct pci_dev *dev, int where, u8 val)
{
    int shift = (where & 3) * 8;
    u32 v;

    pci_read_config_dword(dev, where, &v);
    if ((where & ~3) == PCI_COMMAND && shift < 16)
        v &= 0x0000FFFFUL;
    v &= ~(0xFFUL << shift);
    v |= (u32)val << shift;
    return pci_write_config_dword(dev, where, v);
}

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
    (void)maxlen;
    return (bar == 0) ? dev->b43c_bar0 : NULL;
}

/* =========================================================================
 * Registers (arch/powerpc/kernel/iomap.c): little-endian + eieio
 * ========================================================================= */
static void io_barrier(void)
{
#if defined(__MWERKS__)
    __eieio();
#elif defined(__GNUC__) && (defined(__PPC__) || defined(__powerpc__))
    __asm__ __volatile__("eieio" : : : "memory");
#endif
}

u8 ioread8(const void __iomem *addr)
{
    u8 v = *(const volatile u8 *)addr;
    io_barrier();
    return v;
}

u16 ioread16(const void __iomem *addr)
{
    u16 v = *(const volatile u16 *)addr;
    io_barrier();
    b43compat_entropy_add(v);
    return le16_to_cpu(v);
}

u32 ioread32(const void __iomem *addr)
{
    u32 v = *(const volatile u32 *)addr;
    io_barrier();
    b43compat_entropy_add(v);
    return le32_to_cpu(v);
}

void iowrite8(u8 value, void __iomem *addr)
{
    *(volatile u8 *)addr = value;
    io_barrier();
}

void iowrite16(u16 value, void __iomem *addr)
{
    *(volatile u16 *)addr = cpu_to_le16(value);
    io_barrier();
}

void iowrite32(u32 value, void __iomem *addr)
{
    *(volatile u32 *)addr = cpu_to_le32(value);
    io_barrier();
}

/* _rep: same register read/written count times, without byte swapping.
 * The buffer may be unaligned: copy byte by byte. */
void ioread8_rep(const void __iomem *addr, void *buf, unsigned long count)
{
    u8 *b = (u8 *)buf;
    while (count--)
        *b++ = ioread8(addr);
}

/* Diagnostics: block reads = PIO receive (frame headers and data), so this
 * counts what the chip hands over, before any filtering or decryption */
unsigned long b43compat_pio_reads;

void ioread16_rep(const void __iomem *addr, void *buf, unsigned long count)
{
    u8 *b = (u8 *)buf;
    u16 w;
    b43compat_pio_reads++;
    while (count--) {
        w = *(const volatile u16 *)addr;
        io_barrier();
        b43compat_entropy_add(w);
        b[0] = (u8)(w >> 8);
        b[1] = (u8)w;
        b += 2;
    }
}

void ioread32_rep(const void __iomem *addr, void *buf, unsigned long count)
{
    u8 *b = (u8 *)buf;
    u32 w;
    while (count--) {
        w = *(const volatile u32 *)addr;
        io_barrier();
        b43compat_entropy_add(w);
        b[0] = (u8)(w >> 24);
        b[1] = (u8)(w >> 16);
        b[2] = (u8)(w >> 8);
        b[3] = (u8)w;
        b += 4;
    }
}

void iowrite8_rep(void __iomem *addr, const void *buf, unsigned long count)
{
    const u8 *b = (const u8 *)buf;
    while (count--)
        iowrite8(*b++, addr);
}

void iowrite16_rep(void __iomem *addr, const void *buf, unsigned long count)
{
    const u8 *b = (const u8 *)buf;
    while (count--) {
        *(volatile u16 *)addr = (u16)(((u16)b[0] << 8) | b[1]);
        io_barrier();
        b += 2;
    }
}

void iowrite32_rep(void __iomem *addr, const void *buf, unsigned long count)
{
    const u8 *b = (const u8 *)buf;
    while (count--) {
        *(volatile u32 *)addr = ((u32)b[0] << 24) | ((u32)b[1] << 16) |
                                ((u32)b[2] << 8) | (u32)b[3];
        io_barrier();
        b += 4;
    }
}

/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43os9_fw.c - request_firmware() (drivers/base/firmware_loader) with the
 * firmware built into the program, like CONFIG_EXTRA_FIRMWARE in Linux.
 *
 * b43 requests "b43/ucode5.fw", "b43/pcm5.fw", "b43/b0g0initvals5.fw",
 * "b43/b0g0bsinitvals5.fw"; cfg80211 requests "regulatory.db" (not included:
 * cfg80211 then falls back to the basic rules, domain "00").
 */
#include "b43os9_priv.h"

static int find_builtin(const char *name, const u8 **data, size_t *size)
{
    const b43os9_firmware *fw;

    if (!b43os9_instance)
        return -ENOENT;
    fw = &b43os9_instance->fw;
    if (!strcmp(name, "b43/ucode5.fw")) {
        *data = fw->ucode;
        *size = fw->ucode_len;
    } else if (!strcmp(name, "b43/pcm5.fw")) {
        *data = fw->pcm;
        *size = fw->pcm_len;
    } else if (!strcmp(name, "b43/b0g0initvals5.fw")) {
        *data = fw->iv;
        *size = fw->iv_len;
    } else if (!strcmp(name, "b43/b0g0bsinitvals5.fw")) {
        *data = fw->bsiv;
        *size = fw->bsiv_len;
    } else {
        return -ENOENT;
    }
    return (*data && *size) ? 0 : -ENOENT;
}

int firmware_request_nowarn(const struct firmware **fw, const char *name, struct device *device)
{
    struct firmware *f;
    const u8 *data;
    size_t size;

    (void)device;
    *fw = NULL;
    if (find_builtin(name, &data, &size))
        return -ENOENT;
    f = kzalloc(sizeof(*f), GFP_KERNEL);
    if (!f)
        return -ENOMEM;
    f->data = data;
    f->size = size;
    *fw = f;
    return 0;
}

int request_firmware(const struct firmware **fw, const char *name, struct device *device)
{
    int err = firmware_request_nowarn(fw, name, device);
    if (err)
        printk("firmware: %s not available (%d)\n", name, err);
    return err;
}

/* The answer arrives immediately instead of from a kernel thread: the callers
 * (b43, cfg80211) wait for the callback before going on anyway */
int request_firmware_nowait(struct module *module, bool uevent, const char *name,
                            struct device *device, gfp_t gfp, void *context,
                            void (*cont)(const struct firmware *fw, void *context))
{
    const struct firmware *fw;

    (void)module; (void)uevent; (void)gfp;
    firmware_request_nowarn(&fw, name, device);
    cont(fw, context);
    return 0;
}

void release_firmware(const struct firmware *fw)
{
    kfree(fw);
}

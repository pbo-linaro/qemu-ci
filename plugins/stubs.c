/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Stubs for when CONFIG_PLUGIN is enabled generally, but
 * CONFIG_TCG_TARGET is disabled for a specific target.
 * This will only be the case for 64-bit guests on 32-bit hosts
 * when an alternate accelerator is enabled.
 */

#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "qapi/error.h"


void qemu_plugin_add_opts(void)
{
}

void qemu_plugin_opt_parse(const char *optstr, QemuPluginList *head)
{
    error_report("plugin interface not enabled in this build");
    exit(1);
}

int qemu_plugin_load_list(QemuPluginList *head, Error **errp)
{
    return 0;
}

void qemu_plugin_vcpu_init_hook(CPUState *cpu)
{
}

void qemu_plugin_vcpu_exit_hook(CPUState *cpu)
{
}

void qemu_plugin_vcpu_idle_cb(CPUState *cpu)
{
}

void qemu_plugin_vcpu_resume_cb(CPUState *cpu)
{
}

CPUPluginState *qemu_plugin_create_vcpu_state(void)
{
    /* Protected by tcg_enabled() */
    g_assert_not_reached();
}

/*
 * Copyright (C) 2024, Julian Ganz <neither@nut.email>
 *
 * Traps - count traps
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <stdio.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t interrupts;
    uint64_t exceptions;
    uint64_t hostcalls;
    bool active;
} TrapCounters;

static struct qemu_plugin_scoreboard *traps;
static size_t max_vcpus;

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    TrapCounters* rec = qemu_plugin_scoreboard_find(traps, vcpu_index);
    rec->active = true;
}

static void vcpu_discon(qemu_plugin_id_t id, unsigned int vcpu_index,
                        enum qemu_plugin_discon_type type, uint64_t from_pc,
                        uint64_t to_pc)
{
    TrapCounters* rec = qemu_plugin_scoreboard_find(traps, vcpu_index);
    switch (type) {
    case QEMU_PLUGIN_DISCON_INTERRUPT:
        rec->interrupts++;
        break;
    case QEMU_PLUGIN_DISCON_EXCEPTION:
        rec->exceptions++;
        break;
    case QEMU_PLUGIN_DISCON_HOSTCALL:
        rec->hostcalls++;
        break;
    default:
        /* unreachable */
        break;
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report;
    report = g_string_new("VCPU, interrupts, exceptions, hostcalls\n");
    int vcpu;

    for (vcpu = 0; vcpu < max_vcpus; vcpu++) {
        TrapCounters *rec = qemu_plugin_scoreboard_find(traps, vcpu);
        if (rec->active) {
            g_string_append_printf(report,
                                   "% 4d, % 10"PRId64", % 10"PRId64", % 10"
                                   PRId64"\n",
                                   vcpu,
                                   rec->interrupts, rec->exceptions,
                                   rec->hostcalls);
        }
    }

    qemu_plugin_outs(report->str);
    qemu_plugin_scoreboard_free(traps);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    if (!info->system_emulation) {
        fputs("trap plugin can only be used in system emulation mode.\n",
              stderr);
        return -1;
    }

    max_vcpus = info->system.max_vcpus;
    traps = qemu_plugin_scoreboard_new(sizeof(TrapCounters));
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_vcpu_for_each(id, vcpu_init);

    qemu_plugin_register_vcpu_discon_cb(id, QEMU_PLUGIN_DISCON_ALL,
                                        vcpu_discon);

    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}

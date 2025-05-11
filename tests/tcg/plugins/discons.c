/*
 * Copyright (C) 2025, Julian Ganz <neither@nut.email>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * This plugin exercises the discontinuity plugin API and asserts some
 * of its behaviour regarding reported program counters.
 */
#include <stdio.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

struct cpu_state {
    uint64_t last_pc;
    uint64_t from_pc;
    uint64_t next_pc;
    bool has_last;
    bool has_from;
    bool has_next;
    enum qemu_plugin_discon_type next_type;
};

struct insn_data {
    uint64_t addr;
    uint64_t next_pc;
    bool next_valid;
};

static struct qemu_plugin_scoreboard *states;

static bool abort_on_mismatch;
static bool trace_all_insns;
static uint64_t compare_addr_mask;

static bool addr_eq(uint64_t a, uint64_t b)
{
    return ((a ^ b) & compare_addr_mask) == 0;
}

static void report_mismatch(const char *pc_name, unsigned int vcpu_index,
                            enum qemu_plugin_discon_type type, uint64_t last,
                            uint64_t expected, uint64_t encountered)
{
    GString *report;
    const char *discon_type_name = "unknown";

    if (addr_eq(expected, encountered)) {
        return;
    }

    switch (type) {
    case QEMU_PLUGIN_DISCON_INTERRUPT:
        discon_type_name = "interrupt";
        break;
    case QEMU_PLUGIN_DISCON_EXCEPTION:
        discon_type_name = "exception";
        break;
    case QEMU_PLUGIN_DISCON_HOSTCALL:
        discon_type_name = "hostcall";
        break;
    default:
        break;
    }

    report = g_string_new(NULL);
    g_string_append_printf(report,
                           "Discon %s PC mismatch on VCPU %d\nExpected:      %"
                           PRIx64"\nEncountered:   %"PRIx64"\nExecuted Last: %"
                           PRIx64"\nEvent type:    %s\n",
                           pc_name, vcpu_index, expected, encountered, last,
                           discon_type_name);
    qemu_plugin_outs(report->str);
    if (abort_on_mismatch) {
        g_abort();
    }
    g_string_free(report, true);
}

static void vcpu_discon(qemu_plugin_id_t id, unsigned int vcpu_index,
                        enum qemu_plugin_discon_type type, uint64_t from_pc,
                        uint64_t to_pc)
{
    struct cpu_state *state = qemu_plugin_scoreboard_find(states, vcpu_index);

    switch (type) {
    case QEMU_PLUGIN_DISCON_EXCEPTION:
        /*
         * For some types of exceptions, insn_exec will be called for the
         * instruction that caused the exception.
         */
        if (addr_eq(state->last_pc, from_pc)) {
            break;
        }
        __attribute__((fallthrough));
    default:
        if (state->has_next) {
            /*
             * We may encounter discontinuity chains without any instructions
             * being executed in between.
             */
            report_mismatch("source", vcpu_index, type, state->last_pc,
                            state->next_pc, from_pc);
        } else if (state->has_from) {
            report_mismatch("source", vcpu_index, type, state->last_pc,
                            state->from_pc, from_pc);
        }
    }

    state->has_from = false;

    state->next_pc = to_pc;
    state->next_type = type;
    state->has_next = true;
}

static void insn_exec(unsigned int vcpu_index, void *userdata)
{
    struct cpu_state *state = qemu_plugin_scoreboard_find(states, vcpu_index);
    struct insn_data* insn = (struct insn_data *) userdata;

    state->last_pc = insn->addr;
    state->has_last = true;

    if (insn->next_valid) {
        state->from_pc = insn->next_pc;
    }
    state->has_from = insn->next_valid;

    if (state->has_next) {
        report_mismatch("target", vcpu_index, state->next_type, state->last_pc,
                        state->next_pc, insn->addr);
        state->has_next = false;
    }

    if (trace_all_insns) {
        g_autoptr(GString) report = g_string_new(NULL);
        g_string_append_printf(report, "Exec insn at %"PRIx64" on VCPU %d\n",
                               insn->addr, vcpu_index);
        qemu_plugin_outs(report->str);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t i;
    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    struct insn_data *udata = calloc(n_insns, sizeof(struct insn_data));

    for (i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
        udata[i].addr = pc;
        udata[i].next_pc = pc + qemu_plugin_insn_size(insn);
        udata[i].next_valid = true;
        qemu_plugin_register_vcpu_insn_exec_cb(insn, insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS,
                                               &udata[i]);
    }

    udata[n_insns - 1].next_valid = false;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    /* Set defaults */
    abort_on_mismatch = true;
    trace_all_insns = false;
    compare_addr_mask = -1;

    int i;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "abort") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1],
                                        &abort_on_mismatch)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "trace-all") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1],
                                        &trace_all_insns)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "compare-addr-bits") == 0) {
            if (g_strcmp0(tokens[1], "full") == 0) {
                compare_addr_mask = -1;
            } else {
                char *end = tokens[1];
                guint64 bits = g_ascii_strtoull(tokens[1], &end, 10);
                if (bits == 0 || bits > 64 || *end) {
                    fprintf(stderr,
                            "integer parsing failed or out of range: %s\n",
                            opt);
                    return -1;
                }
                compare_addr_mask = ~(((uint64_t) -1) << bits);
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    states = qemu_plugin_scoreboard_new(sizeof(struct cpu_state));

    qemu_plugin_register_vcpu_discon_cb(id, QEMU_PLUGIN_DISCON_ALL,
                                        vcpu_discon);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    return 0;
}

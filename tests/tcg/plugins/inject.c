/*
 * Copyright (C) 2024, Rowan Hart <rowanbhart@gmail.com>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "glib.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qemu-plugin.h>

/*
 * Specifies a Hypercall for an architecture:
 *
 * - Architecture name
 * - Whether it is enabled
 * - The hypercall instruction
 * - The register names to pass the hypercall # and args
 */
struct HypercallSpec {
    const char *name;
    const bool enabled;
    const char *hypercall;
    const bool little_endian;
    const char *num_reg;
    const char *arg0_reg;
    const char *arg1_reg;
};

static const struct HypercallSpec *hypercall_spec;

static const struct HypercallSpec hypercall_specs[] = {
    { "aarch64", false, NULL, true, 0, 0, 0 },
    { "aarch64_be", false, NULL, false, 0, 0, 0 },
    { "alpha", false, NULL, true, 0, 0, 0 },
    { "arm", false, NULL, true, 0, 0, 0 },
    { "armeb", false, NULL, false, 0, 0, 0 },
    { "avr", false, NULL, true, 0, 0, 0 },
    { "hexagon", false, NULL, true, 0, 0, 0 },
    { "hppa", false, NULL, false, 0, 0, 0 },
    { "i386", false, NULL, true, 0, 0, 0 },
    { "loongarch64", false, NULL, true, 0, 0, 0 },
    { "m68k", false, NULL, false, 0, 0, 0 },
    { "microblaze", false, NULL, false, 0, 0, 0 },
    { "microblazeel", false, NULL, true, 0, 0, 0 },
    { "mips", false, NULL, false, 0, 0, 0 },
    { "mips64", false, NULL, false, 0, 0, 0 },
    { "mips64el", false, NULL, true, 0, 0, 0 },
    { "mipsel", false, NULL, true, 0, 0, 0 },
    { "mipsn32", false, NULL, false, 0, 0, 0 },
    { "mipsn32el", false, NULL, true, 0, 0, 0 },
    { "or1k", false, NULL, false, 0, 0, 0 },
    { "ppc", false, NULL, false, 0, 0, 0 },
    { "ppc64", false, NULL, false, 0, 0, 0 },
    { "ppc64le", false, NULL, true, 0, 0, 0 },
    { "riscv32", false, NULL, true, 0, 0, 0 },
    { "riscv64", false, NULL, true, 0, 0, 0 },
    { "rx", false, NULL, true, 0, 0, 0 },
    { "s390x", false, NULL, false, 0, 0, 0 },
    { "sh4", false, NULL, true, 0, 0, 0 },
    { "sh4eb", false, NULL, false, 0, 0, 0 },
    { "sparc", false, NULL, false, 0, 0, 0 },
    { "sparc32plus", false, NULL, false, 0, 0, 0 },
    { "sparc64", false, NULL, false, 0, 0, 0 },
    { "tricore", false, NULL, true, 0, 0, 0 },
    { "x86_64", true, "\x0f\xa2", true, "rax", "rdi", "rsi" },
    { "xtensa", false, NULL, true, 0, 0, 0 },
    { "xtensaeb", false, NULL, false, 0, 0, 0 },
    { NULL, false, NULL, false, 0, 0, 0 },
};

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/*
 * Returns a handle to a register with a given name, or NULL if there is no
 * such register.
 */
static struct qemu_plugin_register *get_register(const char *name)
{
    GArray *registers = qemu_plugin_get_registers();

    struct qemu_plugin_register *handle = NULL;

    qemu_plugin_reg_descriptor *reg_descriptors =
        (qemu_plugin_reg_descriptor *)registers->data;

    for (size_t i = 0; i < registers->len; i++) {
        if (!strcmp(reg_descriptors[i].name, name)) {
            handle = reg_descriptors[i].handle;
        }
    }

    g_array_free(registers, true);

    return handle;
}

/*
 * Transforms a byte array with at most 8 entries into a uint64_t
 * depending on the target machine's endianness.
 */
static uint64_t byte_array_to_uint64(GByteArray *buf)
{
    uint64_t value = 0;
    if (hypercall_spec->little_endian) {
        for (int i = 0; i < buf->len && i < sizeof(uint64_t); i++) {
            value |= ((uint64_t)buf->data[i]) << (i * 8);
        }
    } else {
        for (int i = 0; i < buf->len && i < sizeof(uint64_t); i++) {
            value |= ((uint64_t)buf->data[i]) << ((buf->len - 1 - i) * 8);
        }
    }
    return value;
}

/*
 * Handle a "hyperacll" instruction, which has some special meaning for this
 * plugin.
 */
static void hypercall(unsigned int vcpu_index, void *userdata)
{
    uint64_t num = 0, arg0 = 0, arg1 = 0;
    GByteArray *buf = g_byte_array_new();
    qemu_plugin_read_register(get_register(hypercall_spec->num_reg), buf);
    num = byte_array_to_uint64(buf);

    g_byte_array_set_size(buf, 0);
    qemu_plugin_read_register(get_register(hypercall_spec->arg0_reg), buf);
    arg0 = byte_array_to_uint64(buf);

    g_byte_array_set_size(buf, 0);
    qemu_plugin_read_register(get_register(hypercall_spec->arg1_reg), buf);
    arg1 = byte_array_to_uint64(buf);

    switch (num) {
    /*
     * The write hypercall (#0x13371337) tells the plugin to write random bytes
     * of a given size into the memory of the emulated system at a particular
     * vaddr
     */
    case 0x13371337: {
        GByteArray *data = g_byte_array_new();
        g_byte_array_set_size(data, arg1);
        for (uint64_t i = 0; i < arg1; i++) {
            data->data[i] = (uint8_t)g_random_int();
        }
        qemu_plugin_write_memory_vaddr(arg0, data);
        break;
    }
    default:
        break;
    }

    g_byte_array_free(buf, TRUE);
}

/*
 * Callback on translation of a translation block.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    for (size_t i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        GByteArray *insn_data = g_byte_array_new();
        size_t insn_len = qemu_plugin_insn_size(insn);
        g_byte_array_set_size(insn_data, insn_len);
        qemu_plugin_insn_data(insn, insn_data->data, insn_data->len);
        if (!memcmp(insn_data->data, hypercall_spec->hypercall, insn_data->len)) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, hypercall,
                                                   QEMU_PLUGIN_CB_R_REGS, NULL);
        }
        g_byte_array_free(insn_data, true);
    }
}


/*
 * Called when the plugin is installed
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    hypercall_spec = &hypercall_specs[0];
    while (hypercall_spec->name != NULL) {
        if (!strcmp(hypercall_spec->name, info->target_name)) {
            break;
        }
        hypercall_spec++;
    }

    if (hypercall_spec->name == NULL) {
        qemu_plugin_outs("Error: no hypercall spec.");
        return -1;
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    return 0;
}

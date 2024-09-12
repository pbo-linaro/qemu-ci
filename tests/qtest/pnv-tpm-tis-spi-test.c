/*
 * QTest testcase for PowerNV 10 TPM with SPI interface
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "libqtest-single.h"
#include "hw/acpi/tpm.h"
#include "hw/pci/pci_ids.h"
#include "qtest_aspeed.h"
#include "tpm-emu.h"
#include "hw/ssi/pnv_spi_regs.h"
#include "pnv-xscom.h"

#define SPI_TPM_BASE            0xc0080

#define CFG_COUNT_COMPARE_1     0x0000000200000000
#define MM_REG_RDR_MATCH        0x00000000ff01ff00
#define SEQ_OP_REG_BASIC        0x1134416200100000

#define TPM_REG_LOC_0  0xd40000

static void pnv_spi_tpm_write(const PnvChip *chip,
                              uint32_t reg,
                              uint64_t val)
{
    uint32_t pcba = SPI_TPM_BASE + reg;
    qtest_writeq(global_qtest, pnv_xscom_addr(chip, pcba), val);
}

static uint64_t pnv_spi_tpm_read(const PnvChip *chip, uint32_t reg)
{
    uint32_t pcba = SPI_TPM_BASE + reg;
    return qtest_readq(global_qtest, pnv_xscom_addr(chip, pcba));
}

static void spi_access_start(const PnvChip *chip,
                             bool n2,
                             uint8_t bytes,
                             uint8_t tpm_op,
                             uint32_t tpm_reg)
{
    uint64_t cfg_reg;
    uint64_t reg_op;
    uint64_t seq_op = SEQ_OP_REG_BASIC;

    cfg_reg = pnv_spi_tpm_read(chip, SPI_CLK_CFG_REG);
    if (cfg_reg != CFG_COUNT_COMPARE_1) {
        pnv_spi_tpm_write(chip, SPI_CLK_CFG_REG, CFG_COUNT_COMPARE_1);
    }
    if (n2) {
        seq_op |= 0x40000000 | (bytes << 0x18);
    } else {
        seq_op |= 0x30000000 | (bytes << 0x18);
    }
    pnv_spi_tpm_write(chip, SPI_SEQ_OP_REG, seq_op);
    pnv_spi_tpm_write(chip, SPI_MM_REG, MM_REG_RDR_MATCH);
    pnv_spi_tpm_write(chip, SPI_CTR_CFG_REG, (uint64_t)0);
    reg_op = bswap64(tpm_op) | ((uint64_t)tpm_reg << 0x20);
    pnv_spi_tpm_write(chip, SPI_XMIT_DATA_REG, reg_op);
}

static void spi_op_complete(const PnvChip *chip)
{
    uint64_t cfg_reg;

    cfg_reg = pnv_spi_tpm_read(chip, SPI_CLK_CFG_REG);
    g_assert_cmpuint(CFG_COUNT_COMPARE_1, ==, cfg_reg);
    pnv_spi_tpm_write(chip, SPI_CLK_CFG_REG, 0);
}

static void spi_write_reg(const PnvChip *chip, uint64_t val)
{
    int i;
    uint64_t spi_sts;

    for (i = 0; i < 10; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_TDR_FULL, spi_sts) == 1) {
            sleep(0.5);
        } else {
            break;
        }
    }
    /* cannot write if SPI_STS_TDR_FULL bit is still set */
    g_assert_cmpuint(0, ==, GETFIELD(SPI_STS_TDR_FULL, spi_sts));
    pnv_spi_tpm_write(chip, SPI_XMIT_DATA_REG, val);

    for (i = 0; i < 3; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_SHIFTER_FSM, spi_sts) & FSM_DONE) {
            break;
        } else {
            sleep(0.1);
        }
    }
    /* it should be done given the amount of time */
    g_assert_cmpuint(0, ==, GETFIELD(SPI_STS_SHIFTER_FSM, spi_sts) & FSM_DONE);
    spi_op_complete(chip);
}

static uint64_t spi_read_reg(const PnvChip *chip)
{
    int i;
    uint64_t spi_sts, val = 0;

    for (i = 0; i < 10; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_RDR_FULL, spi_sts) == 1) {
            val = pnv_spi_tpm_read(chip, SPI_RCV_DATA_REG);
            break;
        }
        sleep(0.5);
    }
    for (i = 0; i < 3; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_RDR_FULL, spi_sts) == 1) {
            sleep(0.1);
        } else {
            break;
        }
    }
    /* SPI_STS_RDR_FULL bit should be reset after read */
    g_assert_cmpuint(0, ==, GETFIELD(SPI_STS_RDR_FULL, spi_sts));
    spi_op_complete(chip);
    return val;
}

static void tpm_set_verify_loc0(const PnvChip *chip)
{
    uint8_t access;

    g_test_message("TPM locality 0 test");
    spi_access_start(chip, false, 1, 0, TPM_REG_LOC_0 | TPM_TIS_REG_ACCESS);
    spi_write_reg(chip, 0);
    spi_access_start(chip, false, 1, 0, TPM_REG_LOC_0 | TPM_TIS_REG_ACCESS);
    spi_write_reg(chip, bswap64(TPM_TIS_ACCESS_REQUEST_USE));

    spi_access_start(chip, true, 1, 0x80, TPM_REG_LOC_0 | TPM_TIS_REG_ACCESS);
    access = (uint8_t)spi_read_reg(chip);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    g_test_message("ACCESS REG = 0x%x checked", access);
}

static void test_spi_tpm(const void *data)
{
    const PnvChip *chip = data;
    uint32_t tpm_sts;

    /* vendor ID and device ID ... check against the known value*/
    spi_access_start(chip, true, 4, 0x83, TPM_REG_LOC_0 | TPM_TIS_REG_DID_VID);
    g_test_message("DID_VID = 0x%lx", spi_read_reg(chip));

    /* set locality 0 */
    tpm_set_verify_loc0(chip);

    g_test_message("TPM status register tests");
    /* test tpm status register */
    spi_access_start(chip, true, 4, 0x80, TPM_REG_LOC_0 | TPM_TIS_REG_STS);
    tpm_sts = (uint32_t)spi_read_reg(chip);
    g_assert_cmpuint(0, ==, tpm_sts);
    /* tpm cmd_ready is a read/write bit */
    /* set the cmd_ready bit */
    spi_access_start(chip, false, 1, 0, TPM_REG_LOC_0 | TPM_TIS_REG_STS);
    spi_write_reg(chip, bswap64(TPM_TIS_STS_COMMAND_READY));
    /* check the cmd_ready bit */
    spi_access_start(chip, true, 1, 0x80, TPM_REG_LOC_0 | TPM_TIS_REG_STS);
    tpm_sts = (uint8_t)spi_read_reg(chip);
    g_assert_cmpuint(TPM_TIS_STS_COMMAND_READY, ==,
                    (TPM_TIS_STS_COMMAND_READY | tpm_sts));
}

int main(int argc, char **argv)
{
    int ret;
    char *tname;
    char args[512];
    GThread *thread;
    TPMTestState test;
    g_autofree char *tmp_path = g_dir_make_tmp("qemu-tpm-tis-spi-test.XXXXXX",
                                                NULL);

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);
    test.data_cond_signal = false;
    test.tpm_version = TPM_VERSION_2_0;

    thread = g_thread_new(NULL, tpm_emu_ctrl_thread, &test);
    tpm_emu_test_wait_cond(&test);

    tname = g_strdup_printf("pnv-xscom/spi-tpm-tis/%s",
                             pnv_chips[3].cpu_model);

    sprintf(args, "-m 2G -machine powernv10 -smp 2,cores=2,"
                      "threads=1 -accel tcg,thread=single -nographic "
                      "-chardev socket,id=chrtpm,path=%s "
                      "-tpmdev emulator,id=tpm0,chardev=chrtpm "
                      "-device tpm-tis-spi,tpmdev=tpm0,bus=pnv-spi-bus.4",
                      test.addr->u.q_unix.path);
    qtest_start(args);
    qtest_add_data_func(tname, &pnv_chips[3], test_spi_tpm);
    ret = g_test_run();

    qtest_end();
    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(tname);
    return ret;
}


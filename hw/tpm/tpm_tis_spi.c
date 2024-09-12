/*
 * QEMU PowerPC SPI TPM 2.0 model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "hw/pci/pci_ids.h"
#include "hw/acpi/tpm.h"
#include "tpm_prop.h"
#include "qemu/log.h"
#include "tpm_tis.h"
#include "hw/ssi/ssi.h"

typedef struct TPMStateSPI {
    /*< private >*/
    SSIPeripheral parent_object;

    uint8_t     offset;       /* offset into data[] */
    uint8_t     spi_state;    /* READ / WRITE / IDLE */
#define SPI_STATE_IDLE   0
#define SPI_STATE_WRITE  1
#define SPI_STATE_READ   2

    bool        command;

    uint8_t     loc_sel;      /* Current locality */
    uint32_t    tis_addr;     /* tis address including locty */

    /*< public >*/
    TPMState    tpm_state; /* not a QOM object */

} TPMStateSPI;

typedef struct xfer_buffer xfer_buffer;

#ifdef SPI_DEBUG_ENABLED
#define SPI_DEBUG(x) (x)
#else
#define SPI_DEBUG(x)
#endif

DECLARE_INSTANCE_CHECKER(TPMStateSPI, TPM_TIS_SPI, TYPE_TPM_TIS_SPI)

static inline void tpm_tis_spi_clear_data(TPMStateSPI *spist)
{
    spist->spi_state = 0;
    spist->offset = 0;
    spist->tis_addr = 0xffffffff;

    return;
}

/* Callback from TPM to indicate that response is copied */
static void tpm_tis_spi_request_completed(TPMIf *ti, int ret)
{
    TPMStateSPI *spist = TPM_TIS_SPI(ti);
    TPMState *s = &spist->tpm_state;

    /* Inform the common code. */
    tpm_tis_request_completed(s, ret);
}

static enum TPMVersion tpm_tis_spi_get_tpm_version(TPMIf *ti)
{
    TPMStateSPI *spist = TPM_TIS_SPI(ti);
    TPMState *s = &spist->tpm_state;

    return tpm_tis_get_tpm_version(s);
}

/*
 * TCG PC Client Platform TPM Profile Specification for TPM 2.0 ver 1.05 rev 14
 *
 * For system Software, the TPM has a 64-bit address of 0x0000_0000_FED4_xxxx.
 * On SPI, the chipset passes the least significant 24 bits to the TPM.
 * The upper bytes will be used by the chipset to select the TPM’s SPI CS#
 * signal. Table 9 shows the locality based on the 16 least significant address
 * bits and assume that either the LPC TPM sync or SPI TPM CS# is used.
 *
 */
static void tpm_tis_spi_write(TPMStateSPI *spist, uint32_t addr, uint8_t val)
{
    SPI_DEBUG(qemu_log("tpm_tis_spi_write addr:0x%8.8x, value:%2.2x\n",
                       addr, val));
    TPMState *tpm_st = &spist->tpm_state;
    tpm_tis_write_data(tpm_st, addr, val, 1);
}

static uint8_t tpm_tis_spi_read(TPMStateSPI *spist, uint32_t addr)
{
    uint16_t offset = addr & 0xffc;
    TPMState *tpm_st = &spist->tpm_state;
    uint8_t data;
    uint32_t did_vid;

    SPI_DEBUG(qemu_log("tpm_tis_spi_read addr:0x%8.8x .... ", addr));
    if (offset == TPM_TIS_REG_DID_VID) {
        did_vid = (TPM_TIS_TPM_DID << 16) | TPM_TIS_TPM_VID;
        data = (did_vid >> ((addr & 0x3) * 8)) & 0xff;
    } else {
        data = tpm_tis_read_data(tpm_st, addr, 1);
    }

    return data;
}

static Property tpm_tis_spi_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", TPMStateSPI, tpm_state.be_driver),
    DEFINE_PROP_UINT32("irq", TPMStateSPI, tpm_state.irq_num, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_spi_reset(DeviceState *dev)
{
    TPMStateSPI *spist = TPM_TIS_SPI(dev);
    TPMState *s = &spist->tpm_state;

    tpm_tis_spi_clear_data(spist);

    spist->loc_sel = 0x00;

    return tpm_tis_reset(s);
}

static uint32_t tpm_transfer(SSIPeripheral *ss, uint32_t tx)
{
    uint32_t rx = 0;
    /* static variables are automatically initialized to zero */
    static uint8_t byte_offset;       /* byte offset in transfer */
    static uint8_t wait_state_count;  /* wait state counter */
    static uint8_t xfer_size;         /* data size of transfer */
    static uint32_t reg_addr;         /* register address of transfer */

    TPMStateSPI *spist = TPM_TIS_SPI(ss);

    uint8_t byte;       /* reversed byte value */
    uint8_t offset = 0; /* offset of byte in payload */
    uint8_t index;      /* index of data byte in transfer */

    SPI_DEBUG(qemu_log("TPM SPI request from controller\n"));

    /* new transfer or not */
    if (spist->command) {   /* new transfer start */
        if (spist->spi_state != SPI_STATE_IDLE) {
            qemu_log_mask(LOG_GUEST_ERROR, "unexpected new transfer\n");
        }
        byte_offset = 0;
        wait_state_count = 0;
    }
    /*
     * Explanation of wait_state:
     * The original TPM model did not have wait state or "flow control" support
     * built in.  If you wanted to read a TPM register through SPI you sent
     * the first byte with the read/write bit and size, then three address bytes
     * and any additional bytes after that were don't care bytes for reads and
     * the model would begin returning byte data to the SPI reader from the
     * register address provided.  In the real world this would mean that a
     * TPM device had only the time between the 31st clock and the 32nd clock
     * to fetch the register data that it had to provide to SPI MISO starting
     * with the 32nd clock.
     *
     * In reality the TPM begins introducing a wait state at the 31st clock
     * by holding MISO low.  This is how it controls the "flow" of the
     * operation. Once the data the TPM needs to return is ready it will
     * select bit 31 + (8*N) to send back a 1 which indicates that it will
     * now start returning data on MISO.
     *
     * The same wait states are applied to writes.  In either the read or write
     * case the wait state occurs between the command+address (4 bytes) and the
     * data (1-n bytes) sections of the SPI frame.  The code below introduces
     * the support for a 32 bit wait state for P10.  All reads and writes
     * through the SPI interface MUST now be aware of the need to do flow
     * control in order to use the TPM via SPI.
     *
     * In conjunction with these changes there were changes made to the SPIM
     * engine that was introduced in P10 to support the 6x op code which is
     * used to receive wait state 0s on the MISO line until it sees the b'1'
     * come back before continuing to read real data from the SPI device(TPM).
     */

    SPI_DEBUG(qemu_log("Processing new payload current byte_offset=%d\n",
                            byte_offset));
    /* process payload data */
    while (offset < 4) {
        spist->command = false;
        byte = (tx >> (24 - 8 * offset)) & 0xFF;
        SPI_DEBUG(qemu_log("Extracted byte=0x%2.2x from payload offset=%d\n",
                  byte, offset));
        switch (byte_offset) {
        case 0:    /* command byte */
            if ((byte >> 7) == 0) {    /* bit-7 */
                spist->spi_state = SPI_STATE_WRITE;
                SPI_DEBUG(qemu_log("spi write\n"));
            } else {
                spist->spi_state = SPI_STATE_READ;
                SPI_DEBUG(qemu_log("spi read\n"));
            }
            xfer_size = (byte & 0x1f) + 1;  /* bits 5:0 */
            SPI_DEBUG(qemu_log("xfer_size=%d\n", xfer_size));
            break;
        case 1:     /* 1st address byte */
            if (byte != 0xd4) {
                qemu_log_mask(LOG_GUEST_ERROR, "incorrect high address 0x%x\n",
                              byte);
            }
            reg_addr = byte << 16;
            SPI_DEBUG(qemu_log("first addr byte=0x%x, reg_addr now 0x%8.8x\n",
                      byte, reg_addr));
            break;
        case 2:     /* 2nd address byte */
            reg_addr |= byte << 8;
            SPI_DEBUG(qemu_log("second addr byte=0x%x, reg_addr now 0x%8.8x\n",
                      byte, reg_addr));
            break;
        case 3:     /* 3rd address byte */
            reg_addr |= byte;
            SPI_DEBUG(qemu_log("third addr byte=0x%x, reg_addr now 0x%8.8x\n",
                      byte, reg_addr));
            break;
        default:    /* data bytes */
            if (wait_state_count < 4) {
                wait_state_count++;
                if (wait_state_count == 4) {
                    SPI_DEBUG(qemu_log("wait complete, wait_state_count=0x%x\n",
                              wait_state_count));
                    rx = rx | (0x01 << (24 - offset * 8));
                    return rx;
                } else {
                    SPI_DEBUG(qemu_log("in wait state, wait_state_count=0x%x\n",
                              wait_state_count));
                    rx = 0;
                }
            } else {
                index = byte_offset - 4;
                SPI_DEBUG(qemu_log("data byte=0x%x for index=%d, "
                                    "reg_addr now 0x%8.8x\n",
                                    byte, index, reg_addr));

                if (index >= xfer_size) {
                    /*
                     * SPI SSI framework limits both rx and tx
                     * to fixed 4-byte with each xfer
                     */
                    SPI_DEBUG(qemu_log("data exceeds expected amount %u\n",
                              xfer_size));
                    return rx;
                }
                spist->tis_addr = reg_addr + (index % 4);
                if (spist->spi_state == SPI_STATE_WRITE) {
                    tpm_tis_spi_write(spist, spist->tis_addr, byte);
                } else {
                    byte = tpm_tis_spi_read(spist, spist->tis_addr);
                    rx = rx | (byte << (24 - offset * 8));
                    SPI_DEBUG(qemu_log("added byte=0x%2.2x to response payload"
                             " at offset=%d\n", byte, offset));
                }
            }
            break;
        }
        if ((wait_state_count == 0) || (wait_state_count == 4)) {
            offset++;
            byte_offset++;
        } else {
            break;
        }
    }
    return rx;
}

static int tpm_cs(SSIPeripheral *ss, bool select)
{
    TPMStateSPI *spist = TPM_TIS_SPI(ss);
    if (select) {
        spist->command = false;
        spist->spi_state = SPI_STATE_IDLE;
    } else {
        spist->command = true;
    }
    return 0;
}


static void tpm_realize(SSIPeripheral *dev, Error **errp)
{
    TPMStateSPI *spist = TPM_TIS_SPI(dev);
    TPMState *s = &spist->tpm_state;

    spist->command = true;
    spist->spi_state = SPI_STATE_IDLE;

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    s->be_driver = qemu_find_tpm_be("tpm0");

    if (!s->be_driver) {
        error_setg(errp, "unable to find tpm backend device");
        return;
    }
}

static void tpm_tis_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->transfer = tpm_transfer;
    k->realize = tpm_realize;
    k->set_cs = tpm_cs;
    k->cs_polarity = SSI_CS_LOW;

    dc->reset = tpm_tis_spi_reset;
    device_class_set_props(dc, tpm_tis_spi_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    dc->desc = "PowerNV SPI TPM";

    tc->model = TPM_MODEL_TPM_TIS;
    tc->request_completed = tpm_tis_spi_request_completed;
    tc->get_version = tpm_tis_spi_get_tpm_version;
}

static const TypeInfo tpm_tis_spi_info = {
    .name          = TYPE_TPM_TIS_SPI,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(TPMStateSPI),
    .class_init    = tpm_tis_spi_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_spi_register_types(void)
{
    type_register_static(&tpm_tis_spi_info);
}

type_init(tpm_tis_spi_register_types)

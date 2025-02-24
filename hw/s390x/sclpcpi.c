/*
 * SCLP event type 11 - Control-Program Identification(CPI):
 *    CPI is used to send program identifiers from the control-program to the
 *    SCLP. The program identifiers provide data about the guest instance. It
 *    is not sent by the SCLP.
 *
 *    The program identifiers are system type, system name, sysplex name and
 *    system level. The system type, system name, and sysplex name use EBCDIC
 *    ucharacters from this set: capital A-Z, 0-9, $, @, #, and blank. The
 *    system level is a hex value.
 *
 * Copyright IBM, Corp. 2024
 *
 * Authors:
 *  Shalini Chellathurai Saroja <shalini@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"
#include "hw/s390x/ebcdic.h"
#include "hw/s390x/s390-virtio-ccw.h"

typedef struct Data {
    uint8_t id_format;
    uint8_t reserved0;
    uint8_t system_type[8];
    uint64_t reserved1;
    uint8_t system_name[8];
    uint64_t reserved2;
    uint64_t system_level;
    uint64_t reserved3;
    uint8_t sysplex_name[8];
    uint8_t reserved4[16];
} QEMU_PACKED Data;

typedef struct ControlProgramIdMsg {
    EventBufferHeader ebh;
    Data data;
} QEMU_PACKED ControlProgramIdMsg;

static bool can_handle_event(uint8_t type)
{
    return type == SCLP_EVENT_CPI;
}

static sccb_mask_t send_mask(void)
{
    return 0;
}

/* Enable SCLP to accept buffers of event type CPI from the control-program. */
static sccb_mask_t receive_mask(void)
{
    return SCLP_EVENT_MASK_CPI;
}

static int write_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr)
{
    ControlProgramIdMsg *cpi = container_of(evt_buf_hdr, ControlProgramIdMsg,
                                            ebh);
    S390CcwMachineState *ms = S390_CCW_MACHINE(qdev_get_machine());

    ascii_put(ms->cpi.system_type, (char *)cpi->data.system_type, 8);
    ascii_put(ms->cpi.system_name, (char *)cpi->data.system_name, 8);
    ascii_put(ms->cpi.sysplex_name, (char *)cpi->data.sysplex_name, 8);
    ms->cpi.system_level = be64_to_cpu(cpi->data.system_level);
    ms->cpi.timestamp = qemu_clock_get_ns(QEMU_CLOCK_HOST);

    cpi->ebh.flags = SCLP_EVENT_BUFFER_ACCEPTED;
    return SCLP_RC_NORMAL_COMPLETION;
}

static void cpi_class_init(ObjectClass *klass, void *data)
{
    SCLPEventClass *sclp_cpi = SCLP_EVENT_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sclp_cpi->can_handle_event = can_handle_event;
    sclp_cpi->get_send_mask = send_mask;
    sclp_cpi->get_receive_mask = receive_mask;
    sclp_cpi->write_event_data = write_event_data;
    dc->user_creatable = false;
}

static const TypeInfo sclp_cpi_info = {
    .name          = TYPE_SCLP_CPI,
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPEvent),
    .class_init    = cpi_class_init,
    .class_size    = sizeof(SCLPEventClass),
};

static void sclp_cpi_register_types(void)
{
    type_register_static(&sclp_cpi_info);
}

type_init(sclp_cpi_register_types)


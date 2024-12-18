/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "hw/net/e1000_regs.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"
#include "../libqtest.h"
#include "pci-pc.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "libqos-malloc.h"
#include "qgraph.h"
#include "e1000e.h"

#define E1000E_IVAR_TEST_CFG \
    (((E1000E_RX0_MSG_ID | E1000_IVAR_INT_ALLOC_VALID) << E1000_IVAR_RXQ0_SHIFT) | \
     ((E1000E_TX0_MSG_ID | E1000_IVAR_INT_ALLOC_VALID) << E1000_IVAR_TXQ0_SHIFT) | \
     E1000_IVAR_TX_INT_EVERY_WB)

#define E1000E_RING_LEN (0x1000)

void e1000e_tx_ring_push(QE1000E *d, void *descr)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    uint32_t tail = e1000e_macreg_read(d, E1000_TDT);
    uint32_t len = e1000e_macreg_read(d, E1000_TDLEN) / E1000_RING_DESC_LEN;

    qtest_memwrite(d_pci->pci_dev.bus->qts,
                   d->tx_ring + tail * E1000_RING_DESC_LEN,
                   descr, E1000_RING_DESC_LEN);
    e1000e_macreg_write(d, E1000_TDT, (tail + 1) % len);

    /* Read WB data for the packet transmitted */
    qtest_memread(d_pci->pci_dev.bus->qts,
                  d->tx_ring + tail * E1000_RING_DESC_LEN,
                  descr, E1000_RING_DESC_LEN);
}

void e1000e_rx_ring_push(QE1000E *d, void *descr)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    uint32_t tail = e1000e_macreg_read(d, E1000_RDT);
    uint32_t len = e1000e_macreg_read(d, E1000_RDLEN) / E1000_RING_DESC_LEN;

    qtest_memwrite(d_pci->pci_dev.bus->qts,
                   d->rx_ring + tail * E1000_RING_DESC_LEN,
                   descr, E1000_RING_DESC_LEN);
    e1000e_macreg_write(d, E1000_RDT, (tail + 1) % len);

    /* Read WB data for the packet received */
    qtest_memread(d_pci->pci_dev.bus->qts,
                  d->rx_ring + tail * E1000_RING_DESC_LEN,
                  descr, E1000_RING_DESC_LEN);
}

static void e1000e_foreach_callback(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice *res = data;
    memcpy(res, dev, sizeof(QPCIDevice));
    g_free(dev);
}

static bool e1000e_test_msix_irq(QE1000E *d, uint16_t msg_id,
                                 uint64_t guest_msix_addr,
                                 uint32_t msix_data)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    QPCIDevice *pci_dev = &d_pci->pci_dev;

    if (msg_id == E1000E_RX0_MSG_ID) {
        g_assert(!d_pci->msix_found_rx0_pending);
    } else if (msg_id == E1000E_TX0_MSG_ID) {
        g_assert(!d_pci->msix_found_tx0_pending);
    } else {
        /* Must enable MSI-X vector to test multiple messages */
        g_assert_not_reached();
    }

    if (pci_dev->msix_enabled) {
        if (qpci_msix_masked(pci_dev, msg_id)) {
            /* No ISR checking should be done if masked, but read anyway */
            bool p = qpci_msix_pending(pci_dev, msg_id);
            if (p) {
                if (msg_id == E1000E_RX0_MSG_ID) {
                    d_pci->msix_found_rx0_pending = true;
                } else if (msg_id == E1000E_TX0_MSG_ID) {
                    d_pci->msix_found_tx0_pending = true;
                } else {
                    g_assert_not_reached();
                }
            }
            return p;
        } else {
            uint32_t data = qtest_readl(pci_dev->bus->qts, guest_msix_addr);
            if (data == msix_data) {
                qtest_writel(pci_dev->bus->qts, guest_msix_addr, 0);
                return true;
            } else if (data == 0) {
                return false;
            } else {
                g_assert_not_reached();
            }
        }
    } else {
        g_assert_not_reached();
    }
}

void e1000e_wait_isr(QE1000E *d, uint16_t msg_id)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    QPCIDevice *pci_dev = &d_pci->pci_dev;
    uint64_t end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    uint64_t guest_msix_addr;
    uint32_t msix_data;

    assert(pci_dev->msix_enabled);

    if (msg_id == E1000E_RX0_MSG_ID) {
        guest_msix_addr = d_pci->msix_rx0_addr;
        msix_data = E1000E_RX0_MSIX_DATA;
    } else if (msg_id == E1000E_TX0_MSG_ID) {
        guest_msix_addr = d_pci->msix_tx0_addr;
        msix_data = E1000E_TX0_MSIX_DATA;
    } else {
        g_assert_not_reached();
    }

    do {
        if (e1000e_test_msix_irq(d, msg_id, guest_msix_addr, msix_data)) {
            return;
        }
        qtest_clock_step(pci_dev->bus->qts, 10000);
    } while (g_get_monotonic_time() < end_time);

    g_error("Timeout expired");
}

static void e1000e_pci_destructor(QOSGraphObject *obj)
{
    QE1000E_PCI *epci = (QE1000E_PCI *) obj;
    qpci_iounmap(&epci->pci_dev, epci->mac_regs);
    qpci_msix_disable(&epci->pci_dev);
}

static void e1000e_pci_msix_enable_vector(QE1000E *d, uint16_t msg_id,
                                          uint64_t guest_msix_addr,
                                          uint32_t msix_data)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    QPCIDevice *pci_dev = &d_pci->pci_dev;
    uint32_t control;
    uint64_t off;

    g_assert_cmpint(msg_id , >=, 0);
    g_assert_cmpint(msg_id , <, qpci_msix_table_size(pci_dev));

    off = pci_dev->msix_table_off + (msg_id * 16);

    qpci_io_writel(pci_dev, pci_dev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_LOWER_ADDR, guest_msix_addr & ~0UL);
    qpci_io_writel(pci_dev, pci_dev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_UPPER_ADDR,
                   (guest_msix_addr >> 32) & ~0UL);
    qpci_io_writel(pci_dev, pci_dev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_DATA, msix_data);

    control = qpci_io_readl(pci_dev, pci_dev->msix_table_bar,
                            off + PCI_MSIX_ENTRY_VECTOR_CTRL);
    qpci_io_writel(pci_dev, pci_dev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_VECTOR_CTRL,
                   control & ~PCI_MSIX_ENTRY_CTRL_MASKBIT);
}

void e1000e_pci_msix_enable_rxtxq_vectors(QE1000E *d, QGuestAllocator *alloc)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    QPCIDevice *pci_dev = &d_pci->pci_dev;

    g_assert(pci_dev->msix_enabled);

    d_pci->msix_rx0_addr = guest_alloc(alloc, 4);
    d_pci->msix_tx0_addr = guest_alloc(alloc, 4);

    e1000e_pci_msix_enable_vector(d, E1000E_RX0_MSG_ID,
                                  d_pci->msix_rx0_addr, E1000E_RX0_MSIX_DATA);
    e1000e_pci_msix_enable_vector(d, E1000E_TX0_MSG_ID,
                                  d_pci->msix_tx0_addr, E1000E_TX0_MSIX_DATA);
}

static void e1000e_pci_start_hw(QOSGraphObject *obj)
{
    QE1000E_PCI *d = (QE1000E_PCI *) obj;
    uint32_t val;

    /* Enable the device */
    qpci_device_enable(&d->pci_dev);

    /* Reset the device */
    val = e1000e_macreg_read(&d->e1000e, E1000_CTRL);
    e1000e_macreg_write(&d->e1000e, E1000_CTRL, val | E1000_CTRL_RST | E1000_CTRL_SLU);

    /* Enable and configure MSI-X */
    qpci_msix_enable(&d->pci_dev);
    e1000e_macreg_write(&d->e1000e, E1000_IVAR, E1000E_IVAR_TEST_CFG);

    /* Check the device status - link and speed */
    val = e1000e_macreg_read(&d->e1000e, E1000_STATUS);
    g_assert_cmphex(val & (E1000_STATUS_LU | E1000_STATUS_ASDV_1000),
        ==, E1000_STATUS_LU | E1000_STATUS_ASDV_1000);

    /* Initialize TX/RX logic */
    e1000e_macreg_write(&d->e1000e, E1000_RCTL, 0);
    e1000e_macreg_write(&d->e1000e, E1000_TCTL, 0);

    /* Notify the device that the driver is ready */
    val = e1000e_macreg_read(&d->e1000e, E1000_CTRL_EXT);
    e1000e_macreg_write(&d->e1000e, E1000_CTRL_EXT,
        val | E1000_CTRL_EXT_DRV_LOAD);

    e1000e_macreg_write(&d->e1000e, E1000_TDBAL,
                           (uint32_t) d->e1000e.tx_ring);
    e1000e_macreg_write(&d->e1000e, E1000_TDBAH,
                           (uint32_t) (d->e1000e.tx_ring >> 32));
    e1000e_macreg_write(&d->e1000e, E1000_TDLEN, E1000E_RING_LEN);
    e1000e_macreg_write(&d->e1000e, E1000_TDT, 0);
    e1000e_macreg_write(&d->e1000e, E1000_TDH, 0);

    /* Enable transmit */
    e1000e_macreg_write(&d->e1000e, E1000_TCTL, E1000_TCTL_EN);

    e1000e_macreg_write(&d->e1000e, E1000_RDBAL,
                           (uint32_t)d->e1000e.rx_ring);
    e1000e_macreg_write(&d->e1000e, E1000_RDBAH,
                           (uint32_t)(d->e1000e.rx_ring >> 32));
    e1000e_macreg_write(&d->e1000e, E1000_RDLEN, E1000E_RING_LEN);
    e1000e_macreg_write(&d->e1000e, E1000_RDT, 0);
    e1000e_macreg_write(&d->e1000e, E1000_RDH, 0);

    /* Enable receive */
    e1000e_macreg_write(&d->e1000e, E1000_RFCTL, E1000_RFCTL_EXTEN);
    e1000e_macreg_write(&d->e1000e, E1000_RCTL, E1000_RCTL_EN  |
                                        E1000_RCTL_UPE |
                                        E1000_RCTL_MPE);

    /* Enable all interrupts */
    e1000e_macreg_write(&d->e1000e, E1000_IMS, 0xFFFFFFFF);

}

static void *e1000e_pci_get_driver(void *obj, const char *interface)
{
    QE1000E_PCI *epci = obj;
    if (!g_strcmp0(interface, "e1000e-if")) {
        return &epci->e1000e;
    }

    /* implicit contains */
    if (!g_strcmp0(interface, "pci-device")) {
        return &epci->pci_dev;
    }

    fprintf(stderr, "%s not present in e1000e\n", interface);
    g_assert_not_reached();
}

static void *e1000e_pci_create(void *pci_bus, QGuestAllocator *alloc,
                               void *addr)
{
    QE1000E_PCI *d = g_new0(QE1000E_PCI, 1);
    QPCIBus *bus = pci_bus;
    QPCIAddress *address = addr;

    qpci_device_foreach(bus, address->vendor_id, address->device_id,
                        e1000e_foreach_callback, &d->pci_dev);

    /* Map BAR0 (mac registers) */
    d->mac_regs = qpci_iomap(&d->pci_dev, 0, NULL);

    /* Allocate and setup TX ring */
    d->e1000e.tx_ring = guest_alloc(alloc, E1000E_RING_LEN);
    g_assert(d->e1000e.tx_ring != 0);

    /* Allocate and setup RX ring */
    d->e1000e.rx_ring = guest_alloc(alloc, E1000E_RING_LEN);
    g_assert(d->e1000e.rx_ring != 0);

    d->obj.get_driver = e1000e_pci_get_driver;
    d->obj.start_hw = e1000e_pci_start_hw;
    d->obj.destructor = e1000e_pci_destructor;

    return &d->obj;
}

static void e1000e_register_nodes(void)
{
    QPCIAddress addr = {
        .vendor_id = PCI_VENDOR_ID_INTEL,
        .device_id = E1000_DEV_ID_82574L,
    };

    /*
     * FIXME: every test using this node needs to setup a -netdev socket,id=hs0
     * otherwise QEMU is not going to start
     */
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "netdev=hs0",
    };
    add_qpci_address(&opts, &addr);

    qos_node_create_driver("e1000e", e1000e_pci_create);
    qos_node_consumes("e1000e", "pci-bus", &opts);
}

libqos_init(e1000e_register_nodes);

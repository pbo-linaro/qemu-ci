#ifndef TEST_PNV_XIVE2_COMMON_H
#define TEST_PNV_XIVE2_COMMON_H

/*
 * sizing:
 * 128 interrupts
 *   => ESB BAR range: 16M
 * 256 ENDs
 *   => END BAR range: 16M
 * 256 VPs
 *   => NVPG,NVC BAR range: 32M
 */
#define MAX_IRQS                128
#define MAX_ENDS                256
#define MAX_VPS                 256

#define XIVE_PAGE_SHIFT         16

#define XIVE_TRIGGER_PAGE       0
#define XIVE_EOI_PAGE           1

#define XIVE_IC_ADDR            0x0006030200000000ull
#define XIVE_IC_TM_INDIRECT     (XIVE_IC_ADDR + (256 << XIVE_PAGE_SHIFT))
#define XIVE_IC_BAR             ((0x3ull << 62) | XIVE_IC_ADDR)
#define XIVE_TM_BAR             0xc006030203180000ull
#define XIVE_ESB_ADDR           0x0006050000000000ull
#define XIVE_ESB_BAR            ((0x3ull << 62) | XIVE_ESB_ADDR)
#define XIVE_END_BAR            0xc006060000000000ull
#define XIVE_NVPG_ADDR          0x0006040000000000ull
#define XIVE_NVPG_BAR           ((0x3ull << 62) | XIVE_NVPG_ADDR)
#define XIVE_NVC_ADDR           0x0006030208000000ull
#define XIVE_NVC_BAR            ((0x3ull << 62) | XIVE_NVC_ADDR)

/*
 * Memory layout
 * A check is done when a table is configured to ensure that the max
 * size of the resource fits in the table.
 */
#define XIVE_VST_SIZE           0x10000ull /* must be at least 4k */

#define XIVE_MEM_START          0x10000000ull
#define XIVE_ESB_MEM            XIVE_MEM_START
#define XIVE_EAS_MEM            (XIVE_ESB_MEM + XIVE_VST_SIZE)
#define XIVE_END_MEM            (XIVE_EAS_MEM + XIVE_VST_SIZE)
#define XIVE_NVP_MEM            (XIVE_END_MEM + XIVE_VST_SIZE)
#define XIVE_NVG_MEM            (XIVE_NVP_MEM + XIVE_VST_SIZE)
#define XIVE_NVC_MEM            (XIVE_NVG_MEM + XIVE_VST_SIZE)
#define XIVE_SYNC_MEM           (XIVE_NVC_MEM + XIVE_VST_SIZE)
#define XIVE_QUEUE_MEM          (XIVE_SYNC_MEM + XIVE_VST_SIZE)
#define XIVE_QUEUE_SIZE         4096 /* per End */
#define XIVE_REPORT_MEM         (XIVE_QUEUE_MEM + XIVE_QUEUE_SIZE * MAX_VPS)
#define XIVE_REPORT_SIZE        256 /* two cache lines per NVP */
#define XIVE_MEM_END            (XIVE_REPORT_MEM + XIVE_REPORT_SIZE * MAX_VPS)

#define P10_XSCOM_BASE          0x000603fc00000000ull
#define XIVE_XSCOM              0x2010800ull

#define XIVE_ESB_RESET          0b00
#define XIVE_ESB_OFF            0b01
#define XIVE_ESB_PENDING        0b10
#define XIVE_ESB_QUEUED         0b11

#define XIVE_ESB_GET            0x800
#define XIVE_ESB_SET_PQ_00      0xc00 /* Load */
#define XIVE_ESB_SET_PQ_01      0xd00 /* Load */
#define XIVE_ESB_SET_PQ_10      0xe00 /* Load */
#define XIVE_ESB_SET_PQ_11      0xf00 /* Load */

#define XIVE_ESB_STORE_EOI      0x400 /* Store */

static uint64_t pnv_xscom_addr(uint32_t pcba)
{
    return P10_XSCOM_BASE | ((uint64_t) pcba << 3);
}

static uint64_t pnv_xive_xscom_addr(uint32_t reg)
{
    return pnv_xscom_addr(XIVE_XSCOM + reg);
}

static uint64_t pnv_xive_xscom_read(QTestState *qts, uint32_t reg)
{
    return qtest_readq(qts, pnv_xive_xscom_addr(reg));
}

static void pnv_xive_xscom_write(QTestState *qts, uint32_t reg, uint64_t val)
{
    qtest_writeq(qts, pnv_xive_xscom_addr(reg), val);
}


static void get_struct(QTestState *qts, uint64_t src, void *dest, size_t size)
{
    uint8_t *destination = (uint8_t *)dest;
    size_t i;

    for (i = 0; i < size; i++) {
        *(destination + i) = qtest_readb(qts, src + i);
    }
}

static void copy_struct(QTestState *qts, void *src, uint64_t dest, size_t size)
{
    uint8_t *source = (uint8_t *)src;
    size_t i;

    for (i = 0; i < size; i++) {
        qtest_writeb(qts, dest + i, *(source + i));
    }
}

static uint64_t get_queue_addr(uint32_t end_index)
{
    return XIVE_QUEUE_MEM + end_index * XIVE_QUEUE_SIZE;
}

static uint8_t get_esb(QTestState *qts, uint32_t index, uint8_t page,
                       uint32_t offset)
{
    uint64_t addr;

    addr = XIVE_ESB_ADDR + (index << (XIVE_PAGE_SHIFT + 1));
    if (page == 1) {
        addr += 1 << XIVE_PAGE_SHIFT;
    }
    return qtest_readb(qts, addr + offset);
}

static void set_esb(QTestState *qts, uint32_t index, uint8_t page,
                    uint32_t offset, uint32_t val)
{
    uint64_t addr;

    addr = XIVE_ESB_ADDR + (index << (XIVE_PAGE_SHIFT + 1));
    if (page == 1) {
        addr += 1 << XIVE_PAGE_SHIFT;
    }
    return qtest_writel(qts, addr + offset, cpu_to_be32(val));
}

static void get_nvp(QTestState *qts, uint32_t index, Xive2Nvp* nvp)
{
    uint64_t addr = XIVE_NVP_MEM + index * sizeof(Xive2Nvp);
    get_struct(qts, addr, nvp, sizeof(Xive2Nvp));
}

static uint64_t get_cl_pair_addr(Xive2Nvp *nvp)
{
    uint64_t upper = xive_get_field32(0x0fffffff, nvp->w6);
    uint64_t lower = xive_get_field32(0xffffff00, nvp->w7);
    return (upper << 32) | (lower << 8);
}

static void set_cl_pair(QTestState *qts, Xive2Nvp *nvp, uint8_t *cl_pair)
{
    uint64_t addr = get_cl_pair_addr(nvp);
    copy_struct(qts, cl_pair, addr, XIVE_REPORT_SIZE);
}

static void get_cl_pair(QTestState *qts, Xive2Nvp *nvp, uint8_t *cl_pair)
{
    uint64_t addr = get_cl_pair_addr(nvp);
    get_struct(qts, addr, cl_pair, XIVE_REPORT_SIZE);
}

static void set_nvp(QTestState *qts, uint32_t index, uint8_t first)
{
    uint64_t nvp_addr;
    Xive2Nvp nvp;
    uint64_t report_addr;

    nvp_addr = XIVE_NVP_MEM + index * sizeof(Xive2Nvp);
    report_addr = (XIVE_REPORT_MEM + index * XIVE_REPORT_SIZE) >> 8;

    memset(&nvp, 0, sizeof(nvp));
    nvp.w0 = xive_set_field32(NVP2_W0_VALID, 0, 1);
    nvp.w0 = xive_set_field32(NVP2_W0_PGOFIRST, nvp.w0, first);
    nvp.w6 = xive_set_field32(NVP2_W6_REPORTING_LINE, nvp.w6,
                              (report_addr >> 24) & 0xfffffff);
    nvp.w7 = xive_set_field32(NVP2_W7_REPORTING_LINE, nvp.w7,
                              report_addr & 0xffffff);
    copy_struct(qts, &nvp, nvp_addr, sizeof(nvp));
}

static void set_nvg(QTestState *qts, uint32_t index, uint8_t next)
{
    uint64_t nvg_addr;
    Xive2Nvgc nvg;

    nvg_addr = XIVE_NVG_MEM + index * sizeof(Xive2Nvgc);

    memset(&nvg, 0, sizeof(nvg));
    nvg.w0 = xive_set_field32(NVGC2_W0_VALID, 0, 1);
    nvg.w0 = xive_set_field32(NVGC2_W0_PGONEXT, nvg.w0, next);
    copy_struct(qts, &nvg, nvg_addr, sizeof(nvg));
}

static void set_eas(QTestState *qts, uint32_t index, uint32_t end_index,
                    uint32_t data)
{
    uint64_t eas_addr;
    Xive2Eas eas;

    eas_addr = XIVE_EAS_MEM + index * sizeof(Xive2Eas);

    memset(&eas, 0, sizeof(eas));
    eas.w = xive_set_field64(EAS2_VALID, 0, 1);
    eas.w = xive_set_field64(EAS2_END_INDEX, eas.w, end_index);
    eas.w = xive_set_field64(EAS2_END_DATA, eas.w, data);
    copy_struct(qts, &eas, eas_addr, sizeof(eas));
}

static void set_end(QTestState *qts, uint32_t index, uint32_t nvp_index,
                    uint8_t priority, bool i)
{
    uint64_t end_addr, queue_addr, queue_hi, queue_lo;
    uint8_t queue_size;
    Xive2End end;

    end_addr = XIVE_END_MEM + index * sizeof(Xive2End);
    queue_addr = get_queue_addr(index);
    queue_hi = (queue_addr >> 32) & END2_W2_EQ_ADDR_HI;
    queue_lo = queue_addr & END2_W3_EQ_ADDR_LO;
    queue_size = __builtin_ctz(XIVE_QUEUE_SIZE) - 12;

    memset(&end, 0, sizeof(end));
    end.w0 = xive_set_field32(END2_W0_VALID, 0, 1);
    end.w0 = xive_set_field32(END2_W0_ENQUEUE, end.w0, 1);
    end.w0 = xive_set_field32(END2_W0_UCOND_NOTIFY, end.w0, 1);
    end.w0 = xive_set_field32(END2_W0_BACKLOG, end.w0, 1);

    end.w1 = xive_set_field32(END2_W1_GENERATION, 0, 1);

    end.w2 = cpu_to_be32(queue_hi);

    end.w3 = cpu_to_be32(queue_lo);
    end.w3 = xive_set_field32(END2_W3_QSIZE, end.w3, queue_size);

    end.w6 = xive_set_field32(END2_W6_IGNORE, 0, i);
    end.w6 = xive_set_field32(END2_W6_VP_OFFSET, end.w6, nvp_index);

    end.w7 = xive_set_field32(END2_W7_F0_PRIORITY, 0, priority);
    copy_struct(qts, &end, end_addr, sizeof(end));
}

#endif /* TEST_PNV_XIVE2_COMMON_H */

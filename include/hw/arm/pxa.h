/*
 * Intel XScale PXA255/270 processor support.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */

#ifndef PXA_H
#define PXA_H

#include "exec/memory.h"
#include "target/arm/cpu-qom.h"
#include "hw/pcmcia.h"
#include "qom/object.h"

/* Interrupt numbers */
# define PXA2XX_PIC_SSP3	0
# define PXA2XX_PIC_USBH2	2
# define PXA2XX_PIC_USBH1	3
# define PXA2XX_PIC_KEYPAD	4
# define PXA2XX_PIC_PWRI2C	6
# define PXA25X_PIC_HWUART	7
# define PXA27X_PIC_OST_4_11	7
# define PXA2XX_PIC_GPIO_0	8
# define PXA2XX_PIC_GPIO_1	9
# define PXA2XX_PIC_GPIO_X	10
# define PXA2XX_PIC_I2S 	13
# define PXA26X_PIC_ASSP	15
# define PXA25X_PIC_NSSP	16
# define PXA27X_PIC_SSP2	16
# define PXA2XX_PIC_LCD		17
# define PXA2XX_PIC_I2C		18
# define PXA2XX_PIC_ICP		19
# define PXA2XX_PIC_STUART	20
# define PXA2XX_PIC_BTUART	21
# define PXA2XX_PIC_FFUART	22
# define PXA2XX_PIC_MMC		23
# define PXA2XX_PIC_SSP		24
# define PXA2XX_PIC_DMA		25
# define PXA2XX_PIC_OST_0	26
# define PXA2XX_PIC_RTC1HZ	30
# define PXA2XX_PIC_RTCALARM	31

/* DMA requests */
# define PXA2XX_RX_RQ_I2S	2
# define PXA2XX_TX_RQ_I2S	3
# define PXA2XX_RX_RQ_BTUART	4
# define PXA2XX_TX_RQ_BTUART	5
# define PXA2XX_RX_RQ_FFUART	6
# define PXA2XX_TX_RQ_FFUART	7
# define PXA2XX_RX_RQ_SSP1	13
# define PXA2XX_TX_RQ_SSP1	14
# define PXA2XX_RX_RQ_SSP2	15
# define PXA2XX_TX_RQ_SSP2	16
# define PXA2XX_RX_RQ_ICP	17
# define PXA2XX_TX_RQ_ICP	18
# define PXA2XX_RX_RQ_STUART	19
# define PXA2XX_TX_RQ_STUART	20
# define PXA2XX_RX_RQ_MMCI	21
# define PXA2XX_TX_RQ_MMCI	22
# define PXA2XX_USB_RQ(x)	((x) + 24)
# define PXA2XX_RX_RQ_SSP3	66
# define PXA2XX_TX_RQ_SSP3	67

# define PXA2XX_SDRAM_BASE	0xa0000000
# define PXA2XX_INTERNAL_BASE	0x5c000000
# define PXA2XX_INTERNAL_SIZE	0x40000

/* pxa2xx_pic.c */
DeviceState *pxa2xx_pic_init(hwaddr base, ARMCPU *cpu);

#endif /* PXA_H */

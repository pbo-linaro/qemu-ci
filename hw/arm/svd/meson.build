if get_option('mcux-soc-svd')
  mcux_soc_svd = subproject('mcux-soc-svd')
  rt595 = mcux_soc_svd.get_variable('rt595')
  run_target('svd-flexcomm', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/flexcomm.h',
    '-p', 'FLEXCOMM0', '-t', 'FLEXCOMM'])
  run_target('svd-flexcomm-usart', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/flexcomm_usart.h',
     '-p', 'USART0', '-t', 'FLEXCOMM_USART',
     '--fields', 'CFG:ENABLE FIFOCFG:ENABLE*,EMPTY* FIFO*:* *:'])
  run_target('svd-flexcomm-i2c', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/flexcomm_i2c.h',
      '-p', 'I2C0', '-t', 'FLEXCOMM_I2C',
      '--fields', 'CFG TIMEOUT:TOMIN MSTCTL MSTDAT STAT:MSTPENDING,MSTSTATE INT*:MSTPENDING* SLV*:'])
  run_target('svd-flexcomm-spi', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/flexcomm_spi.h',
      '-p', 'SPI0', '-t', 'FLEXCOMM_SPI',
      '--fields', 'CFG FIFOCFG:ENABLE*,EMPTY* FIFO*:* INT*: STAT'])
  run_target('svd-rt500-clkctl0', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/rt500_clkctl0.h',
      '-p', 'CLKCTL0', '-t', 'RT500_CLKCTL0',
      '--fields', 'PSCCTL*:ROM_CTRLR_CLK,DSP_CLK SYSPLL0PFD SYSTICKFCLKSEL SYSTICKFCLKDIV FROCLKSTATUS:'])
  run_target('svd-rt500-clkctl1', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/rt500_clkctl1.h',
      '-p', 'CLKCTL1', '-t', 'RT500_CLKCTL1',
      '--fields', 'PSCCTL*:FlexIO AUDIOPLL0PFD OSEVENTTFCLKSEL'])
  run_target('svd-flexspi', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/flexspi.h',
      '-p', 'FLEXSPI0', '-t', 'FLEXSPI',
      '--fields', 'STS0:SEQIDLE MCR0:SWRESET INTR:IPCMDDONE'])
endif

if get_option('mcux-soc-svd')
  mcux_soc_svd = subproject('mcux-soc-svd')
  rt595 = mcux_soc_svd.get_variable('rt595')
  run_target('svd-flexcomm', command: svd_gen_header +
    [ '-i', rt595, '-o', '@SOURCE_ROOT@/include/hw/arm/svd/flexcomm.h',
    '-p', 'FLEXCOMM0', '-t', 'FLEXCOMM'])
endif

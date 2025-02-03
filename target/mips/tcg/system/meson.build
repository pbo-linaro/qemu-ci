mips_system_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cp0_helper.c',
  'special_helper.c',
  'tlb_helper.c',
))
mips_system_ss.add(when: ['CONFIG_TCG', 'CONFIG_SEMIHOSTING'],
  if_true: files('mips-semi.c'),
  if_false: files('semihosting-stub.c')
)
mips_system_ss.add(when: ['CONFIG_TCG', 'TARGET_MIPS64'], if_true: files(
  'lcsr_helper.c',
))

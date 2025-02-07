#ifndef ARM_CPU_SYSREGS_H
#define ARM_CPU_SYSREGS_H

/*
 * Following is similar to the coprocessor regs encodings, but with an argument
 * ordering that matches the ARM ARM. We also reuse the various CP_REG_ defines
 * that actually are the same as the equivalent KVM_REG_ values.
 */
#define ENCODE_ID_REG(op0, op1, crn, crm, op2)          \
    (((op0) << CP_REG_ARM64_SYSREG_OP0_SHIFT) |         \
     ((op1) << CP_REG_ARM64_SYSREG_OP1_SHIFT) |         \
     ((crn) << CP_REG_ARM64_SYSREG_CRN_SHIFT) |         \
     ((crm) << CP_REG_ARM64_SYSREG_CRM_SHIFT) |         \
     ((op2) << CP_REG_ARM64_SYSREG_OP2_SHIFT))

/* include generated definitions */
#include "cpu-sysregs.h.inc"

int get_sysreg_idx(ARMSysRegs sysreg);
uint64_t idregs_sysreg_to_kvm_reg(ARMSysRegs sysreg);

#endif /* ARM_CPU_SYSREGS_H */

#ifndef ARM_CPU_CUSTOM_H
#define ARM_CPU_CUSTOM_H

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "cpu-sysregs.h"

typedef struct ARM64SysRegField {
    const char *name; /* name of the field, for instance CTR_EL0_IDC */
    int index;
    int lower;
    int upper;
} ARM64SysRegField;

typedef struct ARM64SysReg {
    const char *name;   /* name of the sysreg, for instance CTR_EL0 */
    ARMSysReg *sysreg;
    int index;
    GList *fields; /* list of named fields, excluding RES* */
} ARM64SysReg;

void initialize_cpu_sysreg_properties(void);

/*
 * List of exposed ID regs (automatically populated from linux
 * arch/arm64/tools/sysreg)
 */
extern ARM64SysReg arm64_id_regs[NR_ID_REGS];

/* Allocate a new field and insert it at the head of the @reg list */
static inline GList *arm64_sysreg_add_field(ARM64SysReg *reg, const char *name,
                                     uint8_t min, uint8_t max) {

     ARM64SysRegField *field = g_new0(ARM64SysRegField, 1);

     field->name = name;
     field->lower = min;
     field->upper = max;
     field->index = reg->index;

     reg->fields = g_list_append(reg->fields, field);
     return reg->fields;
}

static inline ARM64SysReg *
arm64_sysreg_get(int op0, int op1, int crn, int crm, int op2)
{
        uint64_t index = ARM_FEATURE_ID_RANGE_IDX(op0, op1, crn, crm, op2);
        ARM64SysReg *reg = &arm64_id_regs[index];

        reg->index = index;
        reg->sysreg = g_new(ARMSysReg, 1);
        *reg->sysreg = sys_reg(op0, op1, crn, crm, op2);
        return reg;
}

#endif

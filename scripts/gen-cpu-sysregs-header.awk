#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
# gen-cpu-sysregs-header.awk: arm64 sysreg header generator
#
# Usage: awk -f gen-cpu-sysregs-header.awk -- -v structure=xxx $LINUX_PATH/arch/arm64/tools/sysreg
# where structure is one of "idx" "reg" "table"

BEGIN {
    print ""
    if (structure == "idx") {
	print "typedef enum ARMIDRegisterIdx {"
    }
    if (structure == "reg") {
	print "typedef enum ARMSysRegs {"
    }
    if (structure == "table") {
	print "static const uint32_t id_register_sysreg[NUM_ID_IDX] = {"
    }
} END {
    if (structure == "idx") {
	print "    NUM_ID_IDX,"
	print "} ARMIDRegisterIdx;"
    }
    if (structure == "reg") {
	print "} ARMSysRegs;"
    }
    if (structure == "table") {
	print "};"
    }
    print ""
}

# skip blank lines and comment lines
/^$/ { next }
/^[\t ]*#/ { next }

/^Sysreg\t/ || /^Sysreg /{

	reg = $2
	op0 = $3
	op1 = $4
	crn = $5
	crm = $6
	op2 = $7

	if (op0 == 3 && (op1>=0 && op1<=3) && crn==0 && (crm>=0 && crm<=7) && (op2>=0 && op2<=7)) {
	    idreg = 1
        } else {
	    idreg = 0
	}

	if (idreg) {
	    if (structure == "idx") {
		print "    "reg"_IDX,"
	    }
	    if (structure == "reg") {
		print "    SYS_"reg" = ENCODE_ID_REG("op0", "op1", "crn", "crm", "op2"),"
	    }
	    if (structure == "table") {
		print "    ["reg"_IDX] = SYS_"reg","
	    }
	}

	next
}

{
	/* skip all other lines */
	next
}

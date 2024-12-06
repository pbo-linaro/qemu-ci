#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
# gen-sysreg.awk: arm64 sysreg header generator
#
# Usage: awk -f gen-custom-sysreg.awk $LINUX_PATH/arch/arm64/tools/sysreg

# Sanity check the number of records for a field makes sense. If not, produce
# an error and terminate.

# Print a CPP macro definition, padded with spaces so that the macro bodies
# line up in a column
function define(name, val) {
	printf "%-56s%s\n", "#define " name, val
}

BEGIN {
	print "#ifndef ARM_CPU_SYSREGS_H"
	print "#define ARM_CPU_SYSREGS_H"
	print ""
	print "/* Generated file - do not edit */"
	print ""
} END {
	print ""
	print "#endif /* ARM_CPU_SYSREGS_H */"
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

	define("SYS_" reg, "sys_reg(" op0 ", " op1 ", " crn ", " crm ", " op2 ")")
	next
}

{
	/* skip all other lines */
	next
}

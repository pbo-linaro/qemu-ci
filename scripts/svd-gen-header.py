#!/usr/bin/env python3

# Copyright 2024 Google LLC
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# Use this script to generate a C header file from an SVD xml
#
# Two mode of operations are supported: peripheral and system.
#
# When running in peripheral mode a header for a specific peripheral
# is going to be generated. It will define a type and structure with
# all of the available registers at the bitfield level. An array that
# contains the reigster names indexed by address is also going to be
# generated as well as a function to initialize registers to their
# reset values.
#
# Invocation example:
#
# svd_gen_header -i MIMXRT595S_cm33.xml -o flexcomm.h -p FLEXCOMM0 -t FLEXCOMM
#
# When running in system mode a header for a specific system /
# platform will be generated. It will define register base addresses
# and interrupt numbers for selected peripherals.
#
# Invocation example:
#
# svd_gen_header -i MIMXRT595S_cm33.xml -o rt500.h -s RT500 -p FLEXCOMM0 \
#                -p CLKCTL0 -p CLKCTL1
#

import argparse
import fnmatch
import re
import os
import sys
import xml.etree.ElementTree
import pysvd

data_type_by_bits = {
    8: "uint8_t",
    16: "uint16_t",
    32: "uint32_t",
}


def get_register_array_name_and_size(reg):
    """Return register name and register array size.

    The SVD can define register arrays and pysvd encodes the whole set
    as as regular register with their name prepended by [<array size>].

    Returns a tuple with the register name and the size of the array or
    zero if this is not a register set.

    """

    split = re.split(r"[\[\]]", reg.name)
    return (split[0], int(split[1]) if len(split) > 1 else 0)


def generate_comment(indent, text):
    """Generate a comment block with for the given text with the given
    indentation level.

    If possible, use a single line /* */ comment block, otherwise use
    a multiline comment block.

    Newlines are preseved but tabs are not.

    """

    # preserve new lines
    text = text.replace("\n", " \n ")
    text = text.replace("  ", " ")

    if len(text) + len("/*  */") + len(" " * indent) <= 80 and "\n" not in text:
        return f"{' '* indent}/* {text} */\n"

    out = " " * indent + "/*\n"
    line = " " * indent + " *"
    for word in re.split(r"[ ]", text):
        if len(line) + len(word) >= 79 or word == "\n":
            out += line + "\n"
            line = " " * indent + " *"
            if word != "\n":
                line += " " + word
        else:
            line += " " + word

    out += line + "\n"

    out += " " * indent + " */\n"
    return out


def get_fields(reg, dictionary):
    """Return a list of fields from a register indexed dictionary.

    The dictionary keys may contain wildcards.

    """

    for key in dictionary.keys():
        if fnmatch.fnmatch(reg, key):
            return dictionary[key]
    return None


def generate_reg(reg, dictionary):
    """Check if the register should be generated"""

    if get_fields(reg, dictionary):
        return True
    return False


def skip_reg(reg, dictionary):
    """Check if the register should be skipped"""

    for key in dictionary.keys():
        if fnmatch.fnmatch(reg, key) and dictionary[key] is None:
            return True
    return False


def match_field(reg, field, dictionary):
    """Match a register and field in a dictionary indexed by registers
    that contains a list of fields.

    Both the dictionary keys and the list of fields may contain wildcards.

    """
    fields = get_fields(reg, dictionary)
    if not fields:
        return False
    for f in fields:
        if fnmatch.fnmatch(field, f):
            return True
    return False


def generate_field(name, reg_name, field, shared):
    """Generate register field."""

    out = generate_comment(0, field.description)
    if shared:
        out += "SHARED_"
    out += f"FIELD({name}_{reg_name}, {field.name}, "
    out += f"{field.bitOffset}, {field.bitWidth});\n"
    if hasattr(field, "enumeratedValues") and field.bitWidth > 1:
        for enum in field.enumeratedValues.enumeratedValues:
            enum_name = f"{name}_{reg_name}_{field.name}_{enum.name}"
            out += generate_comment(0, enum.description)
            out += f"#define {enum_name} {enum.value}\n"
    return out


def generate_registers(name, periph, generate, skip):
    """Generate register offsets and fields

    Use registerfield macros to define register offsets and fields for
    a given peripheral.

    """

    regs = sorted(periph.registers, key=lambda reg: reg.addressOffset)
    out = generate_comment(0, periph.description)
    out += f"#define {name}_REGS_NO ({regs[-1].addressOffset // 4 + 1})\n\n"
    for reg in regs:
        reg_name, reg_array_size = get_register_array_name_and_size(reg)
        if not generate_reg(reg_name, generate):
            continue
        if skip_reg(reg_name, skip):
            continue
        out += generate_comment(0, reg.description)
        if reg_array_size > 1:
            for idx in range(0, reg_array_size):
                addr = reg.addressOffset + idx * reg.size // 8
                out += f"REG32({name}_{reg_name}{idx}, 0x{addr:X});\n"
        else:
            addr = reg.addressOffset
            out += f"REG32({name}_{reg_name}, 0x{addr:X});\n"
        for field in reg.fields:
            if not match_field(reg_name, field.name, generate):
                continue
            if match_field(reg_name, field.name, skip):
                continue
            out += generate_field(
                name, reg_name, field, True if reg_array_size > 1 else False
            )
        out += "\n"

    return out


def create_wmask(reg):
    """Generate write mask for a register.

    Generate a mask with all bits that are writable set to 1
    """

    wmask = 0
    fields = sorted(reg.fields, key=lambda field: field.bitOffset)
    if len(fields) > 0:
        for field in fields:
            if field.access != pysvd.type.access.read_only:
                wmask |= ((1 << field.bitWidth) - 1) << field.bitOffset
    else:
        if reg.access != pysvd.type.access.read_only:
            wmask = 0xFFFFFFFF
    return wmask


def create_romask(reg):
    """Generate write mask for a register.

    Generate a mask with all bits that are readonly set to 1
    """

    return ~create_wmask(reg) & 0xFFFFFFFF


def generate_register_access_info(name, periph):
    """Generate RegisterAccessInfo array macro"""

    out = f"\n#define {name}_REGISTER_ACCESS_INFO_ARRAY(_name) \\\n"
    out += f"    struct RegisterAccessInfo _name[{name}_REGS_NO] = {{ \\\n"
    out += f"        [0 ... {name}_REGS_NO - 1] = {{ \\\n"
    out += '            .name = "", \\\n'
    out += "            .addr = -1, \\\n"
    out += "        }, \\\n"
    for reg in periph.registers:
        reg_name, reg_array_size = get_register_array_name_and_size(reg)
        if reg_array_size > 1:
            for idx in range(0, reg_array_size):
                addr = reg.addressOffset + idx * reg.size // 8
                out += f"        [0x{addr // 4:X}] = {{ \\\n"
                out += f'            .name = "{reg_name}{idx}", \\\n'
                out += f"            .addr = 0x{addr:X}, \\\n"
                out += f"            .ro = 0x{create_romask(reg):X}, \\\n"
                out += f"            .reset = 0x{reg.resetValue:X}, \\\n"
                out += "        }, \\\n"
        else:
            out += f"        [0x{reg.addressOffset // 4:X}] = {{ \\\n"
            out += f'            .name = "{reg_name}", \\\n'
            out += f"            .addr = 0x{reg.addressOffset:X}, \\\n"
            out += f"            .ro = 0x{create_romask(reg):X}, \\\n"
            out += f"            .reset = 0x{reg.resetValue:X}, \\\n"
            out += "        }, \\\n"
    out += "    }\n"

    return out


def generate_peripheral_header(periph, name, args):
    """Generate peripheral header

    The following information is generated:

    * typedef with all of the available registers and register fields,
    position and mask defines for register fields.

    * enum values that encode register fields options.

    * a macro that defines the register names indexed by the relative
    address of the register.

    * a function that sets the registers to their reset values

    """

    generate = {}
    for reg in args.fields.split():
        if reg.find(":") > 0:
            reg, fields = reg.split(":")
            generate[reg] = fields.split(",")
        else:
            generate[reg] = ["*"]

    skip = {}
    for reg in args.no_fields.split():
        if reg.find(":") > 0:
            reg, fields = reg.split(":")
            skip[reg] = fields.split(",")
        else:
            skip[reg] = None

    out = generate_registers(name, periph, generate, skip)

    out += generate_register_access_info(name, periph)

    return out


def get_same_class_peripherals(svd, periph):
    """Get a list of peripherals that are instances of the same class."""

    return [periph] + [
        p
        for p in svd.peripherals
        if p.derivedFrom and p.derivedFrom.name == periph.name
    ]


def generate_system_header(system, svd, periph):
    """Generate base and irq defines for given list of peripherals"""

    out = ""

    for p in get_same_class_peripherals(svd, periph):
        out += f"#define {system}_{p.name}_BASE 0x{p.baseAddress:X}UL\n"
    out += "\n"

    for p in get_same_class_peripherals(svd, periph):
        for irq in p.interrupts:
            out += f"#define {system}_{irq.name}_IRQn 0x{irq.value}UL\n"
    out += "\n"

    return out


def main():
    """Script to generate C header file from an SVD file"""

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-i", "--input", type=str, help="Input SVD file", required=True
    )
    parser.add_argument(
        "-o", "--output", type=str, help="Output .h file", required=True
    )
    parser.add_argument(
        "-p",
        "--peripheral",
        action="append",
        help="peripheral name from the SVD file",
        required=True,
    )
    parser.add_argument(
        "-t",
        "--type-name",
        type=str,
        help="name to be used for peripheral definitions",
        required=False,
    )
    parser.add_argument(
        "-s",
        "--system",
        type=str,
        help="name to be used for the system definitions",
        required=False,
    )
    parser.add_argument(
        "--fields",
        help="list of registers and fields that should be generated "
        "in the following format 'REG[:FIELDS] ...' "
        "where FIELDS is a list of comma separated fields that can be "
        "empty; both regsiters and fields can be matched with wildcards; "
        "'REG' is an alias for 'REG:*';",
        required=False,
        default="*:*",
    )
    parser.add_argument(
        "--no-fields",
        type=str,
        help="list of register and fields that should not be generated "
        "in the following format 'REG[:FIELDS] ...' "
        "where FIELDS is a list of comma separated fields that can be "
        "empty; both regsiters and fields can be matched with wildcards; "
        "note that 'REG' will not generate neither the register nor its "
        "fields while REG: will generate the register but none of its fields",
        required=False,
        default=":",
    )

    args = parser.parse_args()

    node = xml.etree.ElementTree.parse(args.input).getroot()
    svd = pysvd.element.Device(node)

    # Write license header
    header = svd.licenseText.strip()
    header += f"\n\nAutomatically generated by {os.path.basename(__file__)} "
    header += f"from {os.path.basename(args.input)}"
    out = generate_comment(0, header)

    # Write some generic defines
    out += "#pragma once\n\n"

    for name in args.peripheral:
        periph = svd.find(name)
        if periph:
            if args.system:
                out += generate_system_header(args.system, svd, periph)
            else:
                out += '#include "hw/register.h"\n\n'
                out += generate_peripheral_header(
                    periph,
                    args.type_name if args.type_name else periph.name,
                    args,
                )
        else:
            print(f"No such peripheral: {name}")
            return 1

    with open(args.output, "w", encoding="ascii") as output:
        output.write(out)

    return 0


if __name__ == "__main__":
    sys.exit(main())

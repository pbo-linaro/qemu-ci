#!/usr/bin/env python3
#
# check-units.py: check the number of compilation units and identify
#                 those that are rebuilt multiple times
#
# Copyright (C) 2025 Linaro Ltd.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from os import access, R_OK, path
from subprocess import check_output, CalledProcessError
from sys import argv, exit
import re


def extract_build_units(cc_path):
    """
    Extract the build units and their counds from compile_commands.json file.

    Returns:
        Hash table of ["unit"] = count
    """

    # Make jq/shell do the heavy lifting
    cmd = f"jq < {cc_path} '.[] | .file' | sort | uniq -c | sort -rn"

    try:
        # Execute the shell command and capture the output
        result = check_output(cmd, shell=True)
    except CalledProcessError as exp:
        print(f"Error executing {cmd}: {exp}")
        exit(1)

    lines = result.decode().strip().split('\n')

    # Create a dictionary to store the build unit frequencies
    build_units = {}

    # extract from string of form: ' 65 "../../fpu/softfloat.c"'
    ext_pat = re.compile(r'^\s*(\d+)\s+"([^"]+)"')

    # strip leading ../
    norm_pat = re.compile(r'^((\.\./)+|/+)')

    # Process each line of the output
    for line in lines:
        match = re.match(ext_pat, line)
        if match:
            count = int(match.group(1))
            unit_path = re.sub(norm_pat, '', match.group(2))

            # Store the count in the dictionary
            build_units[unit_path] = count
        else:
            print(f"couldn't process {line}")

    return build_units


def analyse_units(build_units):
    """
    Analyse the build units and report stats and the top 10 rebuilds
    """

    print(f"Total source files: {len(build_units.keys())}")
    print(f"Total build units: {sum(units.values())}")

    # Create a sorted list by number of rebuilds
    sorted_build_units = sorted(build_units.items(),
                                key=lambda item: item[1],
                                reverse=True)

    print("Most rebuilt units:")
    for unit, count in sorted_build_units[:10]:
        print(f"  {unit} built {count} times")

    print("Least rebuilt units:")
    for unit, count in sorted_build_units[-10:]:
        print(f"  {unit} built {count} times")


if __name__ == "__main__":
    if len(argv) != 2:
        script_name = path.basename(argv[0])
        print(f"Usage: {script_name} <path_to_compile_commands.json>")
        exit(1)

    cc_path = argv[1]
    if path.isfile(cc_path) and access(cc_path, R_OK):
        units = extract_build_units(cc_path)
        analyse_units(units)
        exit(0)
    else:
        print(f"{cc_path} doesn't exist or isn't readable")
        exit(1)

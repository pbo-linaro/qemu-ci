#!/usr/bin/env python3
#
# Copyright (c) 2022 by Rivos Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.

"""Processes BBV info files from QEMU"""

import argparse
import gzip
import json
import pathlib
import sys
import os
import shutil
import subprocess


if sys.stdout.isatty():
    # ANSI Escape sequence for colors
    RED = "\033[31;1m"
    BOLD = "\033[1m"
    BBLUE = "\033[94m"
    GREEN = "\033[32m"
    NORM = "\033[0m"
else:
    # No color for logging to files
    RED = ""
    BOLD = ""
    BBLUE = ""
    GREEN = ""
    NORM = ""


def find_tool(arch, tool):
    patterns = (f"{arch}-unknown-linux-gnu-{tool}", f"{arch}-linux-gnu-{tool}")
    return next(filter(lambda p: shutil.which(p) is not None, patterns), None)


def find_objdump(arch):
    return find_tool(arch, "objdump")


def find_addr2line(arch):
    return find_tool(arch, "addr2line")


def find_sources(arch, dump_lines, binary):
    a2ltool = find_addr2line(arch)
    if a2ltool is None:
        raise Exception(f'Can\'t find addr2line for "{arch}" in $PATH')

    addrs = {
        int(a.split(":")[0].strip(), 16): a.strip()
        for a in dump_lines
        if a.strip() != "..."
    }
    if len(addrs) == 0:
        return {}
    # Get function names and file/line numbers
    ret = subprocess.run(
        [a2ltool, "-C", "-f", "-e", binary] + [hex(a) for a in addrs.keys()],
        capture_output=True,
        check=True,
    )
    lines = [l.strip() for l in ret.stdout.decode("utf-8", "strict").split("\n")]
    functions = {}
    for ad, func, file_line in zip(addrs.keys(), lines[0::2], lines[1::2]):
        path, line = file_line.split(":")
        if line == "?":
            continue
        if "discriminator" in line:
            line = line.split(" ")[0]
        line_num = int(line) - 1
        key = (path, func)
        if key in functions:
            functions[key][0] = min(functions[key][0], line_num)
            functions[key][1] = max(functions[key][1], line_num)
            functions[key][2].setdefault(line_num, []).append(addrs[ad])
        else:
            functions[key] = [line_num, line_num, {line_num: [addrs[ad]]}]
    return functions


# Example nix build path: /build/build/benchspec/CPU/999.specrand_ir/build/build_base_rivos-m64.0000/specrand-common/specrand.c
# Want: /nix/store/dbklj8316vq409839k5dfs4gqv1554qp-cpu2017-1.1.7/benchspec/CPU/999.specrand_ir/src/specrand-common/specrand.c
def guess_correct_source_path(src_path, src_paths):
    src_path = pathlib.Path(src_path)

    if src_path.exists():
        return src_path

    src_paths = src_paths.split(":")
    src_paths = [pathlib.Path(p) for p in src_paths]

    for candidate_src in src_paths:
        # Keep on stripping off parts of the build dir until we find the right
        # subtree in the src path.
        src_path_relative = src_path.parts[1:]
        while len(src_path_relative) >= 1:
            combined_path = candidate_src.joinpath(*src_path_relative)
            if combined_path.exists():
                return combined_path

            src_path_relative = src_path_relative[1:]

    # Not found, fallback on original (missing) path
    return src_path


# number of lines of context around the dumped sources
CONTEXT = 3


def dump_sources(arch, disassembly, binary, src_paths):
    functions = find_sources(arch, disassembly, binary)

    lines = []
    for (fpath, func), (l_min, l_max, asm) in functions.items():
        fname = os.path.basename(fpath)
        lines.append("")
        lines.append(f"{BBLUE}/* ... {func} in {fname} ... */{NORM}")
        fpath = guess_correct_source_path(fpath, src_paths)
        try:
            with open(fpath, "r") as f:
                contents = f.readlines()
        except Exception as e:
            lines.append(str(fpath))
            lines.append(f"/* --- NOT FOUND --- */ /* at {fpath} */")
            continue
        for lnum in range(
            max(0, l_min - CONTEXT), min(l_max + CONTEXT, len(contents) - 1) + 1
        ):
            disasm = asm.setdefault(lnum, [""])
            code = contents[lnum][:-1].replace("\t", "  ")
            color = BOLD if lnum >= l_min and lnum <= l_max else NORM
            lines.append(
                "{:4}:{}{:<82}{} > {}{}{}".format(
                    lnum + 1, color, code, NORM, GREEN, disasm[0], NORM
                )
            )
            if len(disasm) > 1:
                lines.extend([" " * 88 + f"| {GREEN}" + d + NORM for d in disasm[1:]])

    return lines


def disass_range(binary, start_addr, count, arch, pfxlines=6):
    distool = find_objdump(arch)
    if distool is None:
        raise Exception(f'Can\'t find objdump for "{arch}" in $PATH')

    try:
        # Reduce unneeded output by constructing a pessimistic end
        # address, considering the "fanciest" architectures we might
        # be dealing with.
        end_addr = start_addr + 15 * count
        ret = subprocess.run(
            [
                distool,
                "-d",
                "-C",
                "--start-address",
                hex(start_addr),
                "--stop-address",
                hex(end_addr),
                binary,
            ],
            capture_output=True,
            check=True,
        )
    except:
        raise Exception("Failure during disassembly")

    # Trim down to just the label and the basic block;
    # hopefully safe assumption about objdump output format.
    lines = ret.stdout.decode("utf-8", "strict").splitlines()[pfxlines:]
    # It's not safe (in some awesome architectures) to assume each
    # line after the prefix represents an instruction.
    # GNU objdump goes to a new line after 7 bytes of opcodes.
    # We need to compensate for multi-line instructions.
    # Keep only the line with the disassembly which has 3 columns.
    inst_lines = [x for x in lines[1:] if len(x.split("\t")) > 2]
    return lines[0:1] + inst_lines[:count]


def process_weights(weights, bbvinfo):
    weighted_blocks = {}
    for interval, weight in weights:
        blocks = bbvinfo["intervals"][interval]["blocks"]
        for block in blocks:
            index = block["pc"]
            if index not in weighted_blocks:
                weighted_blocks[index] = 0.0
            weighted_blocks[index] += weight * block["icount"]
    return sorted(weighted_blocks.items(), key=lambda item: item[1], reverse=True)


def load_simpoint_info(simpoints_path, weights_path):
    regions = []
    weights = []

    # Like gem5, we ignore the 'index' column in both files. The only
    # error is if the files have different lengths; the lines are
    # assumed to correspond, and the "region" indexing starts at 0 and
    # increments for each line.
    if simpoints_path and simpoints_path.exists():
        with open(simpoints_path) as fs:
            for line in fs:
                (interval, index) = line.split(" ")
                regions.append(int(interval))

        if weights_path and weights_path.exists():
            with open(weights_path) as fw:
                for line in fw:
                    (weight, index) = line.split(" ")
                    weights.append((int(interval), float(weight)))

            if len(regions) > 0 and len(regions) != len(weights):
                raise Exception("Mismatched length of simpoints and weights files")

    return (regions, weights)


def print_one_stats_line(label, misses, accesses):
    percent = 0 if accesses == 0 else (misses * 100 / accesses)
    print(f"%-10s miss rate: %10.6f%% {misses}/{accesses}" % (label, percent))


def print_cache_stats(stats):
    print_one_stats_line(
        "l1-inst", stats["l1-inst"]["misses"], stats["l1-inst"]["accesses"]
    )
    print_one_stats_line(
        "l1-data", stats["l1-data"]["misses"], stats["l1-data"]["accesses"]
    )
    if "l2-inst" in stats and "l2-data" in stats:
        print_one_stats_line(
            "l2-inst", stats["l2-inst"]["misses"], stats["l2-inst"]["accesses"]
        )
        print_one_stats_line(
            "l2-data", stats["l2-data"]["misses"], stats["l2-data"]["accesses"]
        )
        print_one_stats_line(
            "l2-total",
            stats["l2-data"]["misses"] + stats["l2-inst"]["misses"],
            stats["l2-data"]["accesses"] + stats["l2-inst"]["accesses"],
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simpoints", help="simpoints file", type=pathlib.Path)
    parser.add_argument("--cache", help="cache stats", action="store_true")
    parser.add_argument("--weights", help="simpoints weights file", type=pathlib.Path)
    parser.add_argument(
        "--bin", help="binary file to use for disassembly", type=pathlib.Path
    )
    parser.add_argument(
        "--arch", help="architecture of the binary", type=str, default="riscv64"
    )
    parser.add_argument(
        "--region", help="process the blocks for a single region", type=int, default=-1
    )
    parser.add_argument(
        "--simpoint",
        help="process the blocks for a single simpoint",
        type=int,
        default=-1,
    )
    parser.add_argument(
        "--srcs_path",
        help="paths to use for source code lookup, separated by colons",
        type=str,
        default="",
    )
    parser.add_argument("bbvi", help="BBV Info gzip file", type=pathlib.Path)
    args = parser.parse_args()

    # Try to open the BBV info file; nothing else works without that.
    try:
        with gzip.open(args.bbvi) as fin:
            bbvinfo = json.load(fin)
    except:
        print("Can't open the BBV info file")
        sys.exit(1)

    # The simpoints file is needed for (1) simpoint index to region
    # number mapping and (2) projecting overall top blocks from a
    # weighted sum of the top simpoint regions. The weights file is
    # only required for the latter computation.
    (regions, weights) = load_simpoint_info(args.simpoints, args.weights)

    do_global = args.region == -1 and args.simpoint == -1
    do_weights = len(weights) > 0 and do_global
    do_disasm = args.bin is not None
    do_cache = args.cache

    if args.simpoint != -1:
        if len(regions) == 0:
            print("Selecting a simpoint requires providing the simpoint/weights files")
            sys.exit(1)
        elif len(regions) <= args.simpoint:
            print("Illegal simpoint index was provided")
            sys.exit(1)
        args.region = regions[args.simpoint]
        print(f"Simpoint {args.simpoint} corresponds to interval/region {args.region}")
    elif args.region != -1:
        if len(bbvinfo["intervals"]) <= args.region:
            print("Illegal region number was provided")
            sys.exit(1)

    if do_cache:
        if not "cache-stats" in bbvinfo:
            print("BBVI has no cache stats!")
            sys.exit(1)
        if do_global:
            print("\nCache statistics for entire run:")
            print_cache_stats(bbvinfo["cache-stats"])
        else:
            print(f"\nCache statistics for region {args.region}:")
            print_cache_stats(bbvinfo["intervals"][args.region]["cache-stats"])

    if do_weights:
        print("\nComputing top blocks according to simpoint weights")
        weighted_blocks = process_weights(weights, bbvinfo)
        print("\nTop blocks by simpoint weights, and directly from full execution:")
        for index, block in enumerate(bbvinfo["blocks"]):
            if index == len(weighted_blocks):
                break
            print(index, hex(block["pc"]), hex(weighted_blocks[index][0]))

    if do_disasm:
        if not args.bin.exists():
            print(f"Bad path to binary: {args.bin}")
            sys.exit(1)

        if do_global:
            print(f"\nDisassembling the top blocks in {args.bin.name}:")
            blocks = bbvinfo["blocks"]
        else:
            print(
                f"\nDisassembling the top blocks for region {args.region} in {args.bin.name}:"
            )
            blocks = bbvinfo["intervals"][args.region]["blocks"]

        coverage = sum(float(b["pct"]) for b in blocks)
        print(
            "\nTop blocks account for %2.f%% of the %s"
            % (coverage, "run" if do_global else "region")
        )

        # For the overall top blocks, get the disassembly
        for ind, block in enumerate(blocks):
            try:
                lines = disass_range(
                    args.bin, block["pc"], block["len"], args.arch
                )
                lines.extend(
                    dump_sources(args.arch, lines[1:], args.bin, args.srcs_path)
                )
            except Exception as e:
                print(e)
                sys.exit(1)
            # emit the unweighted instruction count too
            invoke = int(block["icount"] / block["len"])
            print(
                f'\nBlock {ind} @ {hex(block["pc"])}, {block["len"]} insns, {invoke} times, {block["pct"]}%:\n'
            )
            print("\n".join(lines))


if __name__ == "__main__":
    main()

"""Test multiple threads hitting breakpoints.

SPDX-License-Identifier: GPL-2.0-or-later
"""
from test_gdbstub import main, report


N_BREAK_THREADS = 2
N_BREAKS = 100


def run_test():
    """Run through the tests one by one"""
    if gdb.selected_inferior().architecture().name() == "MicroBlaze":
        print("SKIP: Atomics are broken on MicroBlaze")
        exit(0)
    gdb.execute("break break_here")
    gdb.execute("continue")
    for _ in range(N_BREAK_THREADS * N_BREAKS):
        counter1 = int(gdb.parse_and_eval("s->counter"))
        counter2 = int(gdb.parse_and_eval("s->counter"))
        report(counter1 == counter2, "{} == {}".format(counter1, counter2))
        gdb.execute("continue")
    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)

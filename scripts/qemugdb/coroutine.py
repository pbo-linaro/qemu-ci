#
# GDB debugging support
#
# Copyright 2012 Red Hat, Inc. and/or its affiliates
#
# Authors:
#  Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or later.  See the COPYING file in the top-level directory.

import gdb

VOID_PTR = gdb.lookup_type('void').pointer()

def pthread_self():
    '''Fetch the base address of TLS.'''
    return gdb.parse_and_eval("$fs_base")

def get_glibc_pointer_guard():
    '''Fetch glibc pointer guard value'''
    fs_base = pthread_self()
    return gdb.parse_and_eval('*(uint64_t*)((uint64_t)%s + 0x30)' % fs_base)

def glibc_ptr_demangle(val, pointer_guard):
    '''Undo effect of glibc's PTR_MANGLE()'''
    return gdb.parse_and_eval('(((uint64_t)%s >> 0x11) | ((uint64_t)%s << (64 - 0x11))) ^ (uint64_t)%s' % (val, val, pointer_guard))

def get_jmpbuf_regs(jmpbuf):
    JB_RBX  = 0
    JB_RBP  = 1
    JB_R12  = 2
    JB_R13  = 3
    JB_R14  = 4
    JB_R15  = 5
    JB_RSP  = 6
    JB_PC   = 7

    pointer_guard = get_glibc_pointer_guard()
    return {'rbx': jmpbuf[JB_RBX],
        'rbp': glibc_ptr_demangle(jmpbuf[JB_RBP], pointer_guard),
        'rsp': glibc_ptr_demangle(jmpbuf[JB_RSP], pointer_guard),
        'r12': jmpbuf[JB_R12],
        'r13': jmpbuf[JB_R13],
        'r14': jmpbuf[JB_R14],
        'r15': jmpbuf[JB_R15],
        'rip': glibc_ptr_demangle(jmpbuf[JB_PC], pointer_guard) }

def symbol_lookup(addr):
    # Example: "__clone3 + 44 in section .text of /lib64/libc.so.6"
    result = gdb.execute(f"info symbol {hex(addr)}", to_string=True).strip()
    return result.split(" in ")[0]

def dump_backtrace(regs):
    '''
    Backtrace dump with raw registers, mimic GDB command 'bt'.
    '''
    # Here only rbp and rip that matter..
    rbp = regs['rbp']
    rip = regs['rip']
    i = 0

    while rbp:
        print(f"#{i}\t{symbol_lookup(rip)}")
        rip = gdb.parse_and_eval(f"*(uint64_t *)(uint64_t)({hex(rbp)} + 8)")
        rbp = gdb.parse_and_eval(f"*(uint64_t *)(uint64_t)({hex(rbp)})")
        i += 1

def dump_backtrace_live(regs):
    '''
    Backtrace dump with gdb's 'bt' command, only usable in a live session.
    '''
    old = dict()

    # remember current stack frame and select the topmost
    # so that register modifications don't wreck it
    selected_frame = gdb.selected_frame()
    gdb.newest_frame().select()

    for i in regs:
        old[i] = gdb.parse_and_eval('(uint64_t)$%s' % i)

    for i in regs:
        gdb.execute('set $%s = %s' % (i, regs[i]))

    gdb.execute('bt')

    for i in regs:
        gdb.execute('set $%s = %s' % (i, old[i]))

    selected_frame.select()

def bt_jmpbuf(jmpbuf):
    '''Backtrace a jmpbuf'''
    regs = get_jmpbuf_regs(jmpbuf)
    try:
        # This reuses gdb's "bt" command, which can be slightly prettier
        # but only works with live sessions.
        dump_backtrace_live(regs)
    except:
        # If above doesn't work, fallback to poor man's unwind
        dump_backtrace(regs)

def co_cast(co):
    return co.cast(gdb.lookup_type('CoroutineUContext').pointer())

def coroutine_to_jmpbuf(co):
    coroutine_pointer = co_cast(co)
    return coroutine_pointer['env']['__jmpbuf']


class CoroutineCommand(gdb.Command):
    '''Display coroutine backtrace'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu coroutine', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        if len(argv) != 1:
            gdb.write('usage: qemu coroutine <coroutine-pointer>\n')
            return

        bt_jmpbuf(coroutine_to_jmpbuf(gdb.parse_and_eval(argv[0])))

class CoroutineBt(gdb.Command):
    '''Display backtrace including coroutine switches'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu bt', gdb.COMMAND_STACK,
                             gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):

        gdb.execute("bt")

        try:
            # This only works with a live session
            co_ptr = gdb.parse_and_eval("qemu_coroutine_self()")
            if co_ptr == False:
                return
        except:
            # Fallback to use hard-coded ucontext vars if it's coredump
            co_ptr = gdb.parse_and_eval("co_tls_current")

        while True:
            co = co_cast(co_ptr)
            co_ptr = co["base"]["caller"]
            if co_ptr == 0:
                break
            gdb.write("Coroutine at " + str(co_ptr) + ":\n")
            bt_jmpbuf(coroutine_to_jmpbuf(co_ptr))

class CoroutineSPFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_sp')

    def invoke(self, addr):
        return get_jmpbuf_regs(coroutine_to_jmpbuf(addr))['rsp'].cast(VOID_PTR)

class CoroutinePCFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_pc')

    def invoke(self, addr):
        return get_jmpbuf_regs(coroutine_to_jmpbuf(addr))['rip'].cast(VOID_PTR)

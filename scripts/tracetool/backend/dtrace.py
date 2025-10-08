# SPDX-License-Identifier: GPL-2.0-or-later

"""
DTrace/SystemTAP backend.
"""

from __future__ import annotations

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import Event, out

PUBLIC = True


PROBEPREFIX: str|None = None

def probeprefix() -> str:
    if PROBEPREFIX is None:
        raise ValueError("you must set PROBEPREFIX")
    return PROBEPREFIX


BINARY: str|None = None

def binary() -> str:
    if BINARY is None:
        raise ValueError("you must set BINARY")
    return BINARY


def generate_h_begin(events: list[Event], group: str) -> None:
    if group == "root":
        header = "trace-dtrace-root.h"
    else:
        header = "trace-dtrace-%s.h" % group

    # Workaround for ust backend, which also includes <sys/sdt.h> and may
    # require SDT_USE_VARIADIC to be defined. If dtrace includes <sys/sdt.h>
    # first without defining SDT_USE_VARIADIC then ust breaks because the
    # STAP_PROBEV() macro is not defined.
    out('#ifndef SDT_USE_VARIADIC')
    out('#define SDT_USE_VARIADIC 1')
    out('#endif')

    out('#include "%s"' % header,
        '')

    out('#undef SDT_USE_VARIADIC')

    # SystemTap defines <provider>_<name>_ENABLED() but other DTrace
    # implementations might not.
    for e in events:
        out('#ifndef QEMU_%(uppername)s_ENABLED',
            '#define QEMU_%(uppername)s_ENABLED() true',
            '#endif',
            uppername=e.name.upper())

def generate_h(event: Event, group: str) -> None:
    out('    QEMU_%(uppername)s(%(argnames)s);',
        uppername=event.name.upper(),
        argnames=", ".join(event.args.names()))


def generate_h_backend_dstate(event: Event, group: str) -> None:
    out('    QEMU_%(uppername)s_ENABLED() || \\',
        uppername=event.name.upper())

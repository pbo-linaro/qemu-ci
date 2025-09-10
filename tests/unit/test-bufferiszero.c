/*
 * QEMU buffer_is_zero test
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

static char buffer[8 * 1024 * 1024];

static void test_1(void)
{
    size_t s, a, o;

    /* Basic positive test.  */
    g_assert(buffer_is_zero(buffer, sizeof(buffer)));

    /* Basic negative test.  */
    buffer[sizeof(buffer) - 1] = 1;
    g_assert(!buffer_is_zero(buffer, sizeof(buffer)));
    buffer[sizeof(buffer) - 1] = 0;

    /* Positive tests for size and alignment.  */
    for (a = 1; a <= 64; a++) {
        for (s = 1; s < 1024; s++) {
            buffer[a - 1] = 1;
            buffer[a + s] = 1;
            g_assert(buffer_is_zero(buffer + a, s));
            buffer[a - 1] = 0;
            buffer[a + s] = 0;
        }
    }

    /* Negative tests for size, alignment, and the offset of the marker.  */
    for (a = 1; a <= 64; a++) {
        for (s = 1; s < 1024; s++) {
            for (o = 0; o < s; ++o) {
                buffer[a + o] = 1;
                g_assert(!buffer_is_zero(buffer + a, s));
                buffer[a + o] = 0;
            }
        }
    }
}

static void test_2(void)
{
    if (g_test_perf()) {
        test_1();
    } else {
        do {
            test_1();
        } while (test_buffer_is_zero_next_accel());
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
#ifdef __aarch64__
/*
TODO: fails with sanitizer on aarch64 hosts, solved with latest gcc.

/usr/lib/gcc/aarch64-linux-gnu/12/include/arm_neon.h:15555:10:
runtime error: load of misaligned address 0xaaaab8471641 for type
'__Uint32x4_t', which requires 4 byte alignment
0xaaaab8471641: note: pointer points here
 00 00 00  01 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
              ^
#0 0xaaaab818cc18 in vld1q_u32 /usr/lib/gcc/aarch64-linux-gnu/12/include/arm_neon.h:15555
#1 0xaaaab818cc18 in buffer_is_zero_simd host/include/aarch64/host/bufferiszero.c.inc:25
#2 0xaaaab81085a8 in buffer_is_zero include/qemu/cutils.h:217
#3 0xaaaab81085a8 in test_1 ../tests/unit/test-bufferiszero.c:43
#4 0xaaaab8108a98 in test_2 ../tests/unit/test-bufferiszero.c:67
#5 0xaaaab8108a98 in test_2 ../tests/unit/test-bufferiszero.c:61
#6 0xffffb2371194  (/lib/aarch64-linux-gnu/libglib-2.0.so.0+0x81194)
#7 0xffffb2370fe4  (/lib/aarch64-linux-gnu/libglib-2.0.so.0+0x80fe4)
#8 0xffffb2371628 in g_test_run_suite (/lib/aarch64-linux-gnu/libglib-2.0.so.0+0x81628)
#9 0xffffb2371698 in g_test_run (/lib/aarch64-linux-gnu/libglib-2.0.so.0+0x81698)
#10 0xaaaab80ee4e8 in main ../tests/unit/test-bufferiszero.c:77
#11 0xffffb1ad773c  (/lib/aarch64-linux-gnu/libc.so.6+0x2773c)
#12 0xffffb1ad7814 in __libc_start_main (/lib/aarch64-linux-gnu/libc.so.6+0x27814)
#13 0xaaaab80ef6ac in _start (build/tests/unit/test-bufferiszero+0x1af6ac)

SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior
/usr/lib/gcc/aarch64-linux-gnu/12/include/arm_neon.h:15555:10 in
*/
    (void) test_1;
    (void) test_2;
#else
    g_test_add_func("/cutils/bufferiszero", test_2);
#endif

    return g_test_run();
}

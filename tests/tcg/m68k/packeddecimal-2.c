/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test packed decimal real conversion from long double, dynamic k-factor */

#include <stdio.h>
#include <float.h>

struct T {
    unsigned int d[3];
    long double lf;
    int kfactor;
};

#define PDR(SMEY, EXP, I, M1, M2) \
    { (0b##SMEY << 28) | ((0x##EXP & 0xfff) << 16) | (0x##EXP & 0xf000) | I, \
      0x##M1, 0x##M2 }

static const struct T tests[] = {
    { PDR(0000, 0000, 1, 00000000, 00000000), 1.0e0l, 0 },
    { PDR(0000, 0010, 1, 00000000, 00000000), 1.0e10l, 0 },
    { PDR(0100, 0001, 1, 00000000, 00000000), 1.0e-1l, 0 },
    { PDR(1000, 0005, 5, 55550000, 00000000), -5.5555e5l, 5 },
    { PDR(0100, 0005, 5, 55550000, 00000000), 5.5555e-5l, 5 },
    { PDR(0000, 0005, 2, 22222222, 22222222), 2.2222222222222222e5l, 17 },
    { PDR(0000, 0005, 2, 22220000, 00000000), 2.2222222222222222e5l, 5 },
    { PDR(0000, 0005, 2, 20000000, 00000000), 2.2222222222222222e5l, 2 },
    { PDR(0000, 0005, 6, 66670000, 00000000), 6.6666666666666666e5l, 5 },
    { PDR(0000, 4932, 1, 18973149, 53572318), LDBL_MAX, 17 },
    { PDR(0100, 4932, 1, 68105157, 15560468), LDBL_MIN, 17 },
    { PDR(0100, 4951, 1, 82259976, 59412373), LDBL_TRUE_MIN, 17 },
    { PDR(0000, 0000, 0, 00000000, 00000000), 0.0l },
    { PDR(1000, 0000, 0, 00000000, 00000000), -0.0l },
    { PDR(0111, 0FFF, 0, 00000000, 00000000), __builtin_infl() },
    { PDR(1111, 0FFF, 0, 00000000, 00000000), -__builtin_infl() },
};

int main()
{
    int ret = 0;

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const struct T *t = &tests[i];
        unsigned int out[3];

        asm("fmove.p %1,(%0),%2"
            : : "a"(out), "f"(t->lf), "d"(t->kfactor) : "memory");

        if (out[0] != t->d[0] || out[1] != t->d[1] || out[2] != t->d[2]) {
            fprintf(stderr, "Mismatch at %d: %08x%08x%08x != %08x%08x%08x\n",
                    i, out[0], out[1], out[2],
                    t->d[0], t->d[1], t->d[2]);
            ret = 1;
        }
    }
    return ret;
}

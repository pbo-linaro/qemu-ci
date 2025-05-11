/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test packed decimal real conversion to long double. */

#include <stdio.h>

struct T {
    unsigned int d[3];
    long double f;
};

#define PDR(SMEY, EXP, INT, M1, M2) \
    { (0b##SMEY << 28) | (0x##EXP << 16) | INT, 0x##M1, 0x##M2 }

static const struct T tests[] = {
    { PDR(0000, 000, 1, 00000000, 00000000), 1.0e0l },
    { PDR(0000, 001, 1, 00000000, 00000000), 1.0e1l },
    { PDR(0000, 010, 1, 00000000, 00000000), 1.0e10l },
    { PDR(0000, 000, 0, 10000000, 00000000), 0.1e0l },
    { PDR(0100, 001, 1, 00000000, 00000000), 1.0e-1l },
    { PDR(1000, 005, 5, 55550000, 00000000), -5.5555e5l },
    { PDR(0000, 999, 9, 99999999, 99999999), 9.9999999999999999e999l },
    { PDR(0000, 123, 1, 23456789, 12345678), 1.2345678912345678e123l },
    { PDR(0000, 000, 0, 00000000, 00000000), 0.0l },
    { PDR(1000, 000, 0, 00000000, 00000000), -0.0l },
    { PDR(0000, 999, 0, 00000000, 00000000), 0.0e999l },
    { PDR(0111, FFF, 0, 00000000, 00000000), __builtin_infl() },
    { PDR(1111, FFF, 0, 00000000, 00000000), -__builtin_infl() },
};

int main()
{
    int ret = 0;

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const struct T *t = &tests[i];
        long double f;

        asm("fmove.p (%1),%0" : "=f"(f) : "a"(t->d));

        if (f != t->f) {
            fprintf(stderr, "Mismatch at %d: %.17Le != %.17Le\n", i, f, t->f);
            ret = 1;
        }
    }
    return ret;
}

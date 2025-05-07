#include <assert.h>
#include <stdint.h>
#include <fenv.h>

int main()
{
    uint64_t x, y, z;

    x = 0x4ff0000000000000UL;
    y = 0x4ff0000000000000UL;
    z = 0x2ff0000000000000UL;

    fesetround(FE_DOWNWARD);
    asm("vreplgr2vr.d $vr0, %[x]\n\t"
        "vreplgr2vr.d $vr1, %[y]\n\t"
        "vreplgr2vr.d $vr2, %[z]\n\t"
        "vfnmsub.d $vr0, $vr0, $vr1, $vr2\n\t"
        "vpickve2gr.d %[x], $vr0, 0\n\t"
        "vpickve2gr.d %[y], $vr0, 1\n\t"
        :[x]"+&r"(x), [y]"+&r"(y)
        :[z]"r"(z)
        :"$f0", "$f1", "$f2");

    assert(x == 0xdfefffffffffffffUL);
    assert(y == 0xdfefffffffffffffUL);
    return 0;
}

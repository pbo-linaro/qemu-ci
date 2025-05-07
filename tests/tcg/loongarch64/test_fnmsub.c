#include <assert.h>
#include <stdint.h>
#include <fenv.h>

int main()
{
    double x, y, z;
    union {
        uint64_t i;
        double d;
    } u;

    x = 0x1.0p256;
    y = 0x1.0p256;
    z = 0x1.0p-256;

    fesetround(FE_DOWNWARD);
    asm("fnmsub.d %[x], %[x], %[y], %[z]\n\t"
        :[x]"+f"(x)
        :[y]"f"(y), [z]"f"(z));

    u.d = x;
    assert(u.i == 0xdfefffffffffffffUL);
    return 0;
}

#include <assert.h>
#include <stdint.h>
#include <fenv.h>

int main()
{
    double x, y, z;

    *(uint64_t *)&x = 0x4ff0000000000000UL;
    *(uint64_t *)&y = 0x4ff0000000000000UL;
    *(uint64_t *)&z = 0x2ff0000000000000UL;

    fesetround(FE_DOWNWARD);
    asm("fnmsub.d %[x], %[x], %[y], %[z]\n\t"
        :[x]"+f"(x)
        :[y]"f"(y), [z]"f"(z));

    assert(*(uint64_t *)&x == 0xdfefffffffffffffUL);
    return 0;
}

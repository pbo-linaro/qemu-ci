#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define hypercall(num, arg0, arg1)                                \
    unsigned int _a __attribute__((unused)) = 0;                  \
    unsigned int _b __attribute__((unused)) = 0;                  \
    unsigned int _c __attribute__((unused)) = 0;                  \
    unsigned int _d __attribute__((unused)) = 0;                  \
    __asm__ __volatile__("cpuid\n\t"                              \
                         : "=a"(_a), "=b"(_b), "=c"(_c), "=d"(_d) \
                         : "a"(num), "D"(arg0), "S"(arg1));

int main(void)
{
    uint16_t value;

    for (size_t i = 0; i < 1000000; i++) {
        hypercall(0x13371337, &value, sizeof(value));
        if (value == 0x1337) {
            printf("Victory!\n");
            return 0;
        }
    }
    return 1;
}


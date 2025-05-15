#include "qemu/sanitizer.h"

const char *__asan_default_options(void)
{
    return "detect_leaks=0";
}

/*
 * Semihosting for user emulation
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "semihosting/semihost.h"

bool semihosting_enabled(bool is_user)
{
    return true;
}

const char *semihosting_get_arg(int i)
{
    return NULL;
}

int semihosting_get_argc(void)
{
    return 0;
}

const char *semihosting_get_cmdline(void)
{
    return NULL;
}

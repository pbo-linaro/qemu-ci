/*
 * QEMU IGVM configuration backend for Confidential Guests
 *
 * Copyright (C) 2023-2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@suse.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BACKENDS_IGVM_H
#define BACKENDS_IGVM_H

#include "system/confidential-guest-support.h"
#include "system/igvm-cfg.h"
#include "qapi/error.h"

int qigvm_process_file(IgvmCfg *igvm, ConfidentialGuestSupport *cgs,
                      Error **errp);

#endif

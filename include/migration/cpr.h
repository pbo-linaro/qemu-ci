/*
 * Copyright (c) 2021, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

#include "qapi/qapi-types-migration.h"

#define MIG_MODE_NONE           -1

#define QEMU_CPR_FILE_MAGIC     0x51435052
#define QEMU_CPR_FILE_VERSION   0x00000001

MigMode cpr_get_incoming_mode(void);
void cpr_set_incoming_mode(MigMode mode);

typedef int (*cpr_walk_fd_cb)(int fd);
void cpr_save_fd(const char *name, int id, int fd);
void cpr_delete_fd(const char *name, int id);
int cpr_find_fd(const char *name, int id);
int cpr_walk_fd(cpr_walk_fd_cb cb);
void cpr_resave_fd(const char *name, int id, int fd);

void cpr_set_cpr_uri(const char *uri);
int cpr_state_save(Error **errp);
int cpr_state_load(Error **errp);
void cpr_state_close(void);
struct QIOChannel *cpr_state_ioc(void);
bool cpr_needed_for_reuse(void *opaque);

QEMUFile *cpr_transfer_output(const char *uri, Error **errp);
QEMUFile *cpr_transfer_input(const char *uri, Error **errp);

#endif

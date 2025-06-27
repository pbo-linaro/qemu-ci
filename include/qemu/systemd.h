/*
 * systemd socket activation support
 *
 * Copyright 2017 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Richard W.M. Jones <rjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_SYSTEMD_H
#define QEMU_SYSTEMD_H

#define FIRST_SOCKET_ACTIVATION_FD 3 /* defined by systemd ABI */

/*
 * Check if socket activation was requested via use of the
 * LISTEN_FDS and LISTEN_PID environment variables.
 *
 * Returns 0 if no socket activation, or the number of FDs.
 */
unsigned int check_socket_activation(void);


/*
 * Check if socket activation indicates named file descriptor based on
 * the colon-delimited LISTEN_FDNAMES.  The "label" must not be NULL,
 * and should be a simple text string that does not contain a colon,
 * matching the FileDescriptorName= directive in systemd.socket(5)
 *
 * It is acceptable to ask for the empty string as a label.
 *
 * Returns -1 if no socket activation is in use, or if the label does
 * not match any file descriptor.  Otherwise, returns the lowest
 * numeric value for a file descriptor matching the label exactly.
 */
unsigned int socket_activated_fd_by_label(const char *label);

#endif

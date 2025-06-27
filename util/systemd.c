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

#include "qemu/osdep.h"
#include "qemu/systemd.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"

#ifndef _WIN32
static char fdnames[256];

unsigned int check_socket_activation(void)
{
    static unsigned int nr_fds = -1;
    const char *s;
    unsigned long pid;
    unsigned long nr_fdsl;
    unsigned int i;
    int fd;
    int f;
    int err;

    if (nr_fds != -1) {
        return nr_fds;
    }
    s = getenv("LISTEN_PID");
    if (s == NULL) {
        nr_fds = 0;
        return 0;
    }
    err = qemu_strtoul(s, NULL, 10, &pid);
    if (err) {
        nr_fds = 0;
        return 0;
    }
    if (pid != getpid()) {
        nr_fds = 0;
        return 0;
    }

    s = getenv("LISTEN_FDS");
    if (s == NULL) {
        nr_fds = 0;
        return 0;
    }
    err = qemu_strtoul(s, NULL, 10, &nr_fdsl);
    if (err) {
        nr_fds = 0;
        return 0;
    }
    assert(nr_fdsl <= UINT_MAX);
    nr_fds = (unsigned int) nr_fdsl;
    s = getenv("LISTEN_FDNAMES");
    if (s != NULL) {
        size_t fdnames_len = strlen(s);
        if (fdnames_len + 1 > sizeof(fdnames)) {
            error_report("LISTEN_FDNAMES is larger than %ldu bytes, "
                         "ignoring socket activation.",
                         sizeof(fdnames));
            nr_fds = 0;
            return 0;
        } else {
            memcpy(fdnames, s, fdnames_len + 1);
        }
    }

    /* So these are not passed to any child processes we might start. */
    unsetenv("LISTEN_FDS");
    unsetenv("LISTEN_PID");
    unsetenv("LISTEN_FDNAMES");

    /* So the file descriptors don't leak into child processes. */
    for (i = 0; i < nr_fds; ++i) {
        fd = FIRST_SOCKET_ACTIVATION_FD + i;
        f = fcntl(fd, F_GETFD);
        if (f == -1 || fcntl(fd, F_SETFD, f | FD_CLOEXEC) == -1) {
            /* If we cannot set FD_CLOEXEC then it probably means the file
             * descriptor is invalid, so socket activation has gone wrong
             * and we should exit.
             */
            error_report("Socket activation failed: "
                         "invalid file descriptor fd = %d: %s",
                         fd, g_strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    return nr_fds;
}

unsigned int socket_activated_fd_by_label(const char *label)
{
    int nr_fds = check_socket_activation();
    if (!nr_fds) {
        return -1;
    }
    int curfd;
    const char *nameend;
    const char *nameptr;
    size_t labellen, namelen;

    labellen = sizeof(label);
    curfd = 0;
    nameptr = fdnames;
    do {
        nameend = strchr(nameptr, ':');
        if (nameend) {
            namelen = nameend - nameptr;
            nameend++;
        } else {
            namelen = strlen(nameptr);
        }
        if (labellen == namelen && memcmp(nameptr, label, namelen) == 0) {
            return curfd + FIRST_SOCKET_ACTIVATION_FD;
        }
        curfd++;
        nameptr = nameend;
    } while (nameptr && curfd < nr_fds);
    return -1;
}

#else /* !_WIN32 */
unsigned int check_socket_activation(void)
{
    return 0;
}
unsigned int socket_activated_fd_by_label(const char *label)
{
    return 0;
}
#endif

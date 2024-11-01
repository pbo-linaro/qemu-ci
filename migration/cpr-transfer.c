/*
 * Copyright (c) 2022, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/savevm.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"

QEMUFile *cpr_transfer_output(const char *uri, Error **errp)
{
    g_autoptr(MigrationChannel) channel = NULL;
    QIOChannel *ioc;

    if (!migrate_uri_parse(uri, &channel, errp)) {
        return NULL;
    }

    if (channel->addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET &&
        channel->addr->u.socket.type == SOCKET_ADDRESS_TYPE_UNIX) {

        QIOChannelSocket *sioc = qio_channel_socket_new();
        SocketAddress *saddr = &channel->addr->u.socket;

        if (qio_channel_socket_connect_sync(sioc, saddr, errp)) {
            object_unref(OBJECT(sioc));
            return NULL;
        }
        ioc = QIO_CHANNEL(sioc);

    } else {
        error_setg(errp, "bad cpr-uri %s; must be unix:", uri);
        return NULL;
    }

    qio_channel_set_name(ioc, "cpr-out");
    return qemu_file_new_output(ioc);
}

QEMUFile *cpr_transfer_input(const char *uri, Error **errp)
{
    g_autoptr(MigrationChannel) channel = NULL;
    QIOChannel *ioc;

    if (!migrate_uri_parse(uri, &channel, errp)) {
        return NULL;
    }

    if (channel->addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET &&
        channel->addr->u.socket.type == SOCKET_ADDRESS_TYPE_UNIX) {

        QIOChannelSocket *sioc;
        SocketAddress *saddr = &channel->addr->u.socket;
        QIONetListener *listener = qio_net_listener_new();

        qio_net_listener_set_name(listener, "cpr-socket-listener");
        if (qio_net_listener_open_sync(listener, saddr, 1, errp) < 0) {
            object_unref(OBJECT(listener));
            return NULL;
        }

        sioc = qio_net_listener_wait_client(listener);
        ioc = QIO_CHANNEL(sioc);

    } else {
        error_setg(errp, "bad cpr-uri %s; must be unix:", uri);
        return NULL;
    }

    qio_channel_set_name(ioc, "cpr-in");
    return qemu_file_new_input(ioc);
}

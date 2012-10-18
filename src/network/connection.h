/*
 * uhub - A tiny ADC p2p connection hub
 * Copyright (C) 2007-2012, Jan Vidar Krey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HAVE_UHUB_NETWORK_CONNECTION_H
#define HAVE_UHUB_NETWORK_CONNECTION_H

#include "uhub.h"
#include "network/common.h"
#include "network/backend.h"
#include "network/tls.h"

#define NET_EVENT_TIMEOUT         0x0001
#define NET_EVENT_READ            0x0002
#define NET_EVENT_WRITE           0x0004

struct net_connection
{
	NET_CON_STRUCT_COMMON
};

extern int   net_con_get_sd(struct net_connection* con);
extern void* net_con_get_ptr(struct net_connection* con);

extern struct net_connection* net_con_create();
extern void net_con_destroy(struct net_connection*);
extern void net_con_initialize(struct net_connection* con, int sd, net_connection_cb callback, const void* ptr, int events);
extern void net_con_reinitialize(struct net_connection* con, net_connection_cb callback, const void* ptr, int events);
extern void net_con_update(struct net_connection* con, int events);
extern void net_con_callback(struct net_connection* con, int events);

/**
 * Close the connection.
 * This will ensure a connection is closed properly and will generate a NET_EVENT_DESTROYED event which indicates
 * that the con can safely be deleted (or set to NULL).
 */
extern void net_con_close(struct net_connection* con);

/**
 * Send data
 *
 * @return returns the number of bytes sent.
 *         0 if no data is sent, and this function should be called again (EWOULDBLOCK/EINTR)
 *        <0 if an error occured, the negative number contains the error code.
 */
extern ssize_t net_con_send(struct net_connection* con, const void* buf, size_t len);

/**
 * Receive data
 *
 * @return returns the number of bytes sent.
 *         0 if no data is sent, and this function should be called again (EWOULDBLOCK/EINTR)
 *        <0 if an error occured, the negative number contains the error code.
 */
extern ssize_t net_con_recv(struct net_connection* con, void* buf, size_t len);

/**
 * Receive data without removing them from the recv() buffer.
 * NOTE: This does not currently work for SSL connections after the SSL handshake has been
 * performed.
 */
extern ssize_t net_con_peek(struct net_connection* con, void* buf, size_t len);

/**
 * Set timeout for connetion.
 *
 * @param seconds the number of seconds into the future.
 */
extern void net_con_set_timeout(struct net_connection* con, int seconds);
extern void net_con_clear_timeout(struct net_connection* con);

#endif /* HAVE_UHUB_NETWORK_CONNECTION_H */


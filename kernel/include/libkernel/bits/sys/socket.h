/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#ifndef _KERNEL_LIBKERNEL_BITS_SYS_SOCKET_H
#define _KERNEL_LIBKERNEL_BITS_SYS_SOCKET_H

enum SOCK_DOMAINS {
    PF_LOCAL,
    PF_INET,
    PF_INET6,
    PF_IPX,
    PF_NETLINK,
    PF_X25,
    PF_AX25,
    PF_ATMPVC,
    PF_APPLETALK,
    PF_PACKET,
};

enum SOCK_TYPES {
    SOCK_STREAM,
    SOCK_DGRAM,
    SOCK_SEQPACKET,
    SOCK_RAW,
    SOCK_RDM,
    SOCK_PACKET,
};

#endif
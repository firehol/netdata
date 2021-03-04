// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_EBPF_SYNC_H
#define NETDATA_EBPF_SYNC_H 1

enum netdata_sync {
    NETDATA_SYNC_GLOBLAL_TABLE,

    NETDATA_SYNC_END
};

extern void *ebpf_sync_thread(void *ptr);

#endif
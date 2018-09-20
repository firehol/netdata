// SPDX-License-Identifier: GPL-3.0+

#include "common.h"

#ifndef NETDATA_RRD2JSON_API_OLD_H
#define NETDATA_RRD2JSON_API_OLD_H 1

extern unsigned long rrdset_info2json_api_old(RRDSET *st, char *options, BUFFER *wb);

extern void rrd_graph2json_api_old(RRDSET *st, char *options, BUFFER *wb);

extern void rrd_all2json_api_old(RRDHOST *host, BUFFER *wb);

extern time_t rrdset2json_api_old(
        int type
        , RRDSET *st
        , BUFFER *wb
        , long points
        , long group
        , int group_method
        , time_t after
        , time_t before
        , int only_non_zero
);

#endif //NETDATA_RRD2JSON_API_OLD_H

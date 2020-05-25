// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_EXPORTING_OPENTSDB_H
#define NETDATA_EXPORTING_OPENTSDB_H

#include "exporting/exporting_engine.h"

int init_opentsdb_telnet_instance(struct instance *instance);
int init_opentsdb_http_instance(struct instance *instance);

void sanitize_opentsdb_label_value(char *dst, char *src, size_t len);
int format_host_labels_opentsdb_telnet(struct instance *instance, RRDHOST *host);
int format_host_labels_opentsdb_http(struct instance *instance, RRDHOST *host);

int format_dimension_collected_opentsdb_telnet(struct instance *instance, RRDDIM *rd);
int format_dimension_stored_opentsdb_telnet(struct instance *instance, RRDDIM *rd);

int format_dimension_collected_opentsdb_http(struct instance *instance, RRDDIM *rd);
int format_dimension_stored_opentsdb_http(struct instance *instance, RRDDIM *rd);

struct opentsdb_specific_data {
#ifdef ENABLE_HTTPS
    SSL *conn; //SSL connection
#else
    void *conn;
#endif
    int flags; //The flags for SSL connection
};

#endif //NETDATA_EXPORTING_OPENTSDB_H

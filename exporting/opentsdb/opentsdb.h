// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_EXPORTING_OPENTSDB_H
#define NETDATA_EXPORTING_OPENTSDB_H

#include "exporting/exporting_engine.h"

int init_opentsdb_connector(struct connector *connector);
int init_opentsdb_telnet_instance(struct instance *instance);
int init_opentsdb_http_instance(struct instance *instance);

int format_host_labels_opentsdb_telnet(struct instance *instance, RRDHOST *host);

int format_dimension_collected_opentsdb_telnet(struct instance *instance, RRDDIM *rd);
int format_dimension_stored_opentsdb_telnet(struct instance *instance, RRDDIM *rd);

int format_dimension_collected_opentsdb_http(struct instance *instance, RRDDIM *rd);
int format_dimension_stored_opentsdb_http(struct instance *instance, RRDDIM *rd);

#endif //NETDATA_EXPORTING_OPENTSDB_H

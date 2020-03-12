// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_EXPORTING_MONGODB_H
#define NETDATA_EXPORTING_MONGODB_H

#include "exporting/exporting_engine.h"

extern int mongodb_init(const char *uri_string, const char *database_string, const char *collection_string, const int32_t socket_timeout);

extern int mongodb_insert(char *data, size_t n_metrics);

extern void mongodb_cleanup();

#endif //NETDATA_EXPORTING_MONGODB_H

// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_API_QUERY_AVERAGE_H
#define NETDATA_API_QUERY_AVERAGE_H

#include "../query.h"

extern void *grouping_init_average(RRDR *r);
extern void grouping_reset_average(RRDR *r);
extern void grouping_free_average(RRDR *r);
extern void grouping_add_average(RRDR *r, calculated_number value);
extern void grouping_flush_average(RRDR *r, calculated_number *rrdr_value_ptr, uint8_t *rrdr_value_options_ptr);

#endif //NETDATA_API_QUERY_AVERAGE_H

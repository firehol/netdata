// SPDX-License-Identifier: GPL-3.0-or-later

#include "stddev.h"


// ----------------------------------------------------------------------------
// stddev

struct grouping_stddev {
    size_t series_size;
    size_t next_pos;

    LONG_DOUBLE series[];
};

void *grouping_init_stddev(RRDR *r) {
    long entries = (r->group > r->group_points) ? r->group : r->group_points;
    return callocz(1, sizeof(struct grouping_stddev) + entries * sizeof(LONG_DOUBLE));
}

void grouping_reset_stddev(RRDR *r) {
    struct grouping_stddev *g = (struct grouping_stddev *)r->grouping_data;
    g->next_pos = 0;
}

void grouping_free_stddev(RRDR *r) {
    freez(r->grouping_data);
}

void grouping_add_stddev(RRDR *r, calculated_number value) {
    struct grouping_stddev *g = (struct grouping_stddev *)r->grouping_data;

    if(unlikely(g->next_pos >= g->series_size)) {
        error("INTERNAL ERROR: stddev buffer overflow on chart '%s' - next_pos = %zu, series_size = %zu, r->group = %ld, r->group_points = %ld.", r->st->name, g->next_pos, g->series_size, r->group, r->group_points);
    }
    else {
        g->series[g->next_pos++] = (LONG_DOUBLE)value;
    }
}

void grouping_flush_stddev(RRDR *r, calculated_number *rrdr_value_ptr, uint8_t *rrdr_value_options_ptr) {
    struct grouping_stddev *g = (struct grouping_stddev *)r->grouping_data;

    if(unlikely(!g->next_pos)) {
        *rrdr_value_ptr = 0.0;
        *rrdr_value_options_ptr |= RRDR_EMPTY;
    }
    else {
        calculated_number value = standard_deviation(g->series, g->next_pos);

        if(isnan(value)) {
            *rrdr_value_ptr = 0.0;
            *rrdr_value_options_ptr |= RRDR_EMPTY;
        }
        else {
            *rrdr_value_ptr = value;
        }
    }

    g->next_pos = 0;
}


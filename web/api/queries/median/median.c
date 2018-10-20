// SPDX-License-Identifier: GPL-3.0-or-later

#include "median.h"


// ----------------------------------------------------------------------------
// median

struct grouping_median {
    size_t series_size;
    size_t next_pos;

    LONG_DOUBLE series[];
};

void *grouping_init_median(RRDR *r) {
    long entries = (r->group > r->group_points) ? r->group : r->group_points;
    return callocz(1, sizeof(struct grouping_median) + entries * sizeof(LONG_DOUBLE));
}

void grouping_reset_median(RRDR *r) {
    struct grouping_median *g = (struct grouping_median *)r->grouping_data;
    g->next_pos = 0;
}

void grouping_free_median(RRDR *r) {
    freez(r->grouping_data);
}

void grouping_add_median(RRDR *r, calculated_number value) {
    struct grouping_median *g = (struct grouping_median *)r->grouping_data;

    if(unlikely(g->next_pos >= g->series_size)) {
        error("INTERNAL ERROR: median buffer overflow on chart '%s' - next_pos = %zu, series_size = %zu, r->group = %ld, r->group_points = %ld.", r->st->name, g->next_pos, g->series_size, r->group, r->group_points);
    }
    else {
        g->series[g->next_pos++] = (LONG_DOUBLE)value;
    }
}

void grouping_flush_median(RRDR *r, calculated_number *rrdr_value_ptr, uint8_t *rrdr_value_options_ptr) {
    struct grouping_median *g = (struct grouping_median *)r->grouping_data;

    if(unlikely(!g->next_pos)) {
        *rrdr_value_ptr = 0.0;
        *rrdr_value_options_ptr |= RRDR_EMPTY;
    }
    else {
        calculated_number value;

        if(g->next_pos > 1) {
            sort_series(g->series, g->next_pos);
            value = (calculated_number)median_on_sorted_series(g->series, g->next_pos);
        }
        else {
            value = (calculated_number)g->series[0];
        }

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


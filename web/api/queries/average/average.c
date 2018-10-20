// SPDX-License-Identifier: GPL-3.0-or-later

#include "average.h"

// ----------------------------------------------------------------------------
// average

struct grouping_average {
    calculated_number sum;
    size_t count;
};

void *grouping_init_average(RRDR *r) {
    (void)r;
    return callocz(1, sizeof(struct grouping_average));
}

void grouping_reset_average(RRDR *r) {
    struct grouping_average *g = (struct grouping_average *)r->grouping_data;
    g->sum = 0;
    g->count = 0;
}

void grouping_free_average(RRDR *r) {
    freez(r->grouping_data);
}

void grouping_add_average(RRDR *r, calculated_number value) {
    if(!isnan(value)) {
        struct grouping_average *g = (struct grouping_average *)r->grouping_data;
        g->sum += value;
        g->count++;
    }
}

void grouping_flush_average(RRDR *r, calculated_number *rrdr_value_ptr, RRDR_VALUE_FLAGS *rrdr_value_options_ptr) {
    struct grouping_average *g = (struct grouping_average *)r->grouping_data;

    if(unlikely(!g->count)) {
        *rrdr_value_ptr = 0.0;
        *rrdr_value_options_ptr |= RRDR_VALUE_EMPTY;
    }
    else {
        if(unlikely(r->group_points != 1))
            *rrdr_value_ptr = g->sum / r->group_sum_divisor;
        else
            *rrdr_value_ptr = g->sum / g->count;
    }

    g->sum = 0.0;
    g->count = 0;
}


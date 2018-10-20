// SPDX-License-Identifier: GPL-3.0-or-later

#include "incremental_sum.h"

// ----------------------------------------------------------------------------
// incremental sum

struct grouping_incremental_sum {
    calculated_number first;
    calculated_number last;
    size_t count;
};

void *grouping_init_incremental_sum(RRDR *r) {
    (void)r;
    return callocz(1, sizeof(struct grouping_incremental_sum));
}

void grouping_reset_incremental_sum(RRDR *r) {
    struct grouping_incremental_sum *g = (struct grouping_incremental_sum *)r->grouping_data;
    g->first = 0;
    g->last = 0;
    g->count = 0;
}

void grouping_free_incremental_sum(RRDR *r) {
    freez(r->grouping_data);
}

void grouping_add_incremental_sum(RRDR *r, calculated_number value) {
    if(!isnan(value)) {
        struct grouping_incremental_sum *g = (struct grouping_incremental_sum *)r->grouping_data;

        if(unlikely(!g->count)) {
            g->first = value;
            g->count++;
        }
        else {
            g->last = value;
            g->count++;
        }
    }
}

void grouping_flush_incremental_sum(RRDR *r, calculated_number *rrdr_value_ptr, RRDR_VALUE_FLAGS *rrdr_value_options_ptr) {
    struct grouping_incremental_sum *g = (struct grouping_incremental_sum *)r->grouping_data;

    if(unlikely(!g->count)) {
        *rrdr_value_ptr = 0.0;
        *rrdr_value_options_ptr |= RRDR_VALUE_EMPTY;
    }
    else if(unlikely(g->count == 1)) {
        *rrdr_value_ptr = 0.0;
    }
    else {
        *rrdr_value_ptr = g->last - g->first;
    }

    g->first = 0.0;
    g->last = 0.0;
    g->count = 0;
}

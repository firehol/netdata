// SPDX-License-Identifier: GPL-3.0-or-later

#include "charts2json.h"

// generate JSON for the /api/v1/charts API call

void charts2json(RRDHOST *host, BUFFER *wb) {
    static char *custom_dashboard_info_js_filename = NULL;
    size_t c, dimensions = 0, memory = 0, alarms = 0;
    RRDSET *st;

    usec_t now_usec = now_realtime_usec();

    if(unlikely(!custom_dashboard_info_js_filename))
        custom_dashboard_info_js_filename = config_get(CONFIG_SECTION_WEB, "custom dashboard_info.js", "");

    buffer_sprintf(wb, "{\n"
                       "\t\"hostname\": \"%s\""
                       ",\n\t\"version\": \"%s\""
                       ",\n\t\"os\": \"%s\""
                       ",\n\t\"timezone\": \"%s\""
                       ",\n\t\"update_every\": %llu"
                       ",\n\t\"history\": %zu"
                       ",\n\t\"custom_info\": \"%s\""
                       ",\n\t\"charts\": {"
                   , host->hostname
                   , host->program_version
                   , host->os
                   , host->timezone
                   , host->rrd_update_every_usec
                   , host->rrd_history_entries
                   , custom_dashboard_info_js_filename
    );

    c = 0;
    rrdhost_rdlock(host);
    rrdset_foreach_read(st, host) {
        if(rrdset_is_available_for_viewers(st)) {
            if(c) buffer_strcat(wb, ",");
            buffer_strcat(wb, "\n\t\t\"");
            buffer_strcat(wb, st->id);
            buffer_strcat(wb, "\": ");
            rrdset2json(st, wb, &dimensions, &memory);

            c++;
            st->last_accessed_time_usec = now_usec;
        }
    }

    RRDCALC *rc;
    for(rc = host->alarms; rc ; rc = rc->next) {
        if(rc->rrdset)
            alarms++;
    }
    rrdhost_unlock(host);

    buffer_sprintf(wb
                   , "\n\t}"
                     ",\n\t\"charts_count\": %zu"
                     ",\n\t\"dimensions_count\": %zu"
                     ",\n\t\"alarms_count\": %zu"
                     ",\n\t\"rrd_memory_bytes\": %zu"
                     ",\n\t\"hosts_count\": %zu"
                     ",\n\t\"hosts\": ["
                   , c
                   , dimensions
                   , alarms
                   , memory
                   , rrd_hosts_available
    );

    if(unlikely(rrd_hosts_available > 1)) {
        rrd_rdlock();

        size_t found = 0;
        RRDHOST *h;
        rrdhost_foreach_read(h) {
            if(!rrdhost_should_be_removed(h, host, now_usec)) {
                buffer_sprintf(wb
                               , "%s\n\t\t{"
                                 "\n\t\t\t\"hostname\": \"%s\""
                                 "\n\t\t}"
                               , (found > 0) ? "," : ""
                               , h->hostname
                );

                found++;
            }
        }

        rrd_unlock();
    }
    else {
        buffer_sprintf(wb
                       , "\n\t\t{"
                         "\n\t\t\t\"hostname\": \"%s\""
                         "\n\t\t}"
                       , host->hostname
        );
    }

    buffer_sprintf(wb, "\n\t]\n}\n");
}

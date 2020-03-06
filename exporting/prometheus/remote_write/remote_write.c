// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_write.h"

static int as_collected;
static int homogeneous;
char context[PROMETHEUS_ELEMENT_MAX + 1];
char chart[PROMETHEUS_ELEMENT_MAX + 1];
char family[PROMETHEUS_ELEMENT_MAX + 1];
char units[PROMETHEUS_ELEMENT_MAX + 1] = "";

static inline void remote_write_split_words(char *str, char **words, int max_words) {
    char *s = str;
    int i = 0;

    while(*s && i < max_words - 1) {
        while(*s && isspace(*s)) s++; // skip spaces to the begining of a tag name

        if(*s)
            words[i] = s;

        while(*s && !isspace(*s) && *s != '=') s++; // find the end of the tag name

        if(*s != '=') {
            words[i] = NULL;
            break;
        }
        *s = '\0';
        s++;
        i++;

        while(*s && isspace(*s)) s++; // skip spaces to the begining of a tag value

        if(*s && *s == '"') s++; // strip an opening quote
        if(*s)
            words[i] = s;

        while(*s && !isspace(*s) && *s != ',') s++; // find the end of the tag value

        if(*s && *s != ',') {
            words[i] = NULL;
            break;
        }
        if(s != words[i] && *(s - 1) == '"') *(s - 1) = '\0'; // strip a closing quote
        if(*s != '\0') {
            *s = '\0';
            s++;
            i++;
        }
    }
}

/**
 * Send header to a server
 *
 * @param sock a communication socket.
 * @param instance an instance data structure.
 */
void prometheus_remote_write_send_header(int *sock, struct instance *instance)
{
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags += MSG_NOSIGNAL;
#endif

    struct prometheus_remote_write_specific_config *connector_specific_config = instance->config.connector_specific_config;

    static BUFFER *header;
    if(!header)
        header = buffer_create(0);

    buffer_sprintf(
        header,
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Accept: */*\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n\r\n",
        connector_specific_config->remote_write_path,
        instance->engine->config.hostname,
        buffer_strlen((BUFFER *)instance->buffer));

    send(*sock, buffer_tostring(header), buffer_strlen(header), flags);
    buffer_flush(header);
}

/**
 * Process a responce received after Prometheus remote write connector had sent data
 *
 * @param buffer a response from a remote service.
 * @param instance an instance data structure.
 * @return Returns 0 on success, 1 on failure.
 */
int process_prometheus_remote_write_response(BUFFER *buffer, struct instance *instance) {
    if(unlikely(!buffer)) return 1;

    const char *s = buffer_tostring(buffer);
    int len = buffer_strlen(buffer);

    // do nothing with HTTP responses 200 or 204

    while(!isspace(*s) && len) {
        s++;
        len--;
    }
    s++;
    len--;

    if(likely(len > 4 && (!strncmp(s, "200 ", 4) || !strncmp(s, "204 ", 4))))
        return 0;
    else
        return exporting_discard_response(buffer, instance);
}

/**
 * Initialize Prometheus Remote Write connector instance
 *
 * @param instance an instance data structure.
 * @return Returns 0 on success, 1 on failure.
 */
int init_prometheus_remote_write_instance(struct instance *instance)
{
    instance->worker = simple_connector_worker;

    instance->start_batch_formatting = NULL;
    instance->start_host_formatting = format_host_prometheus_remote_write;
    instance->start_chart_formatting = format_chart_prometheus_remote_write;
    instance->metric_formatting = format_dimension_prometheus_remote_write;
    instance->end_chart_formatting = NULL;
    instance->end_host_formatting = NULL;
    instance->end_batch_formatting = format_batch_prometheus_remote_write;

    instance->send_header = prometheus_remote_write_send_header;
    instance->check_response = process_prometheus_remote_write_response;

    instance->buffer = (void *)buffer_create(0);
    if (!instance->buffer) {
        error("EXPORTING: cannot create buffer for AWS Kinesis exporting connector instance %s", instance->config.name);
        return 1;
    }
    uv_mutex_init(&instance->mutex);
    uv_cond_init(&instance->cond_var);

    // struct prometheus_remote_write_specific_config *connector_specific_config =
    //     instance->config.connector_specific_config;
    struct prometheus_remote_write_specific_data *connector_specific_data =
        callocz(1, sizeof(struct prometheus_remote_write_specific_data));
    instance->connector_specific_data = (void *)connector_specific_data;

    init_write_request();

    return 0;
}

/**
 * Format host data for Prometheus Remote Write connector
 *
 * @param instance an instance data structure.
 * @param host a data collecting host.
 * @return Always returns 0.
 */
int format_host_prometheus_remote_write(struct instance *instance, RRDHOST *host)
{
    char hostname[PROMETHEUS_ELEMENT_MAX + 1];
    prometheus_label_copy(hostname, instance->engine->config.hostname, PROMETHEUS_ELEMENT_MAX);

    add_host_info("netdata_info", hostname, host->program_name, host->program_version, now_realtime_usec() / USEC_PER_MS);

    // TODO: use labels instead of tags
    if(host->tags && *(host->tags)) {
        char tags[PROMETHEUS_LABELS_MAX + 1];
        strncpy(tags, host->tags, PROMETHEUS_LABELS_MAX);
        char *words[PROMETHEUS_LABELS_MAX_NUMBER] = {NULL};
        int i;

        remote_write_split_words(tags, words, PROMETHEUS_LABELS_MAX_NUMBER);

        add_host_info("netdata_host_tags_info", hostname, NULL, NULL, now_realtime_usec() / USEC_PER_MS);

        for(i = 0; words[i] != NULL && words[i + 1] != NULL && (i + 1) < PROMETHEUS_LABELS_MAX_NUMBER; i += 2) {
            add_tag(words[i], words[i + 1]);
        }
    }

    return 0;
}

/**
 * Format chart data for Prometheus Remote Write connector
 *
 * @param instance an instance data structure.
 * @param st a chart.
 * @return Always returns 0.
 */
int format_chart_prometheus_remote_write(struct instance *instance, RRDSET *st)
{
    prometheus_label_copy(chart, (instance->config.options & EXPORTING_OPTION_SEND_NAMES && st->name)?st->name:st->id, PROMETHEUS_ELEMENT_MAX);
    prometheus_label_copy(family, st->family, PROMETHEUS_ELEMENT_MAX);
    prometheus_name_copy(context, st->context, PROMETHEUS_ELEMENT_MAX);

    if(likely(can_send_rrdset(instance, st))) {
        as_collected = (EXPORTING_OPTIONS_DATA_SOURCE(instance->config.options) == EXPORTING_SOURCE_DATA_AS_COLLECTED);
        homogeneous = 1;
        if(as_collected) {
            if(rrdset_flag_check(st, RRDSET_FLAG_HOMOGENEOUS_CHECK))
                rrdset_update_heterogeneous_flag(st);

            if(rrdset_flag_check(st, RRDSET_FLAG_HETEROGENEOUS))
                homogeneous = 0;
        }
        else {
            if(EXPORTING_OPTIONS_DATA_SOURCE(instance->config.options) == EXPORTING_SOURCE_DATA_AVERAGE)
                prometheus_units_copy(units, st->units, PROMETHEUS_ELEMENT_MAX, 0);
        }
    }

    return 0;
}

/**
 * Format dimension data for Prometheus Remote Write connector
 *
 * @param instance an instance data structure.
 * @param rd a dimension.
 * @return Always returns 0.
 */
int format_dimension_prometheus_remote_write(struct instance *instance, RRDDIM *rd)
{
    (void)instance;
    (void)rd;

    if(rd->collections_counter && !rrddim_flag_check(rd, RRDDIM_FLAG_OBSOLETE)) {
        char name[PROMETHEUS_LABELS_MAX + 1];
        char dimension[PROMETHEUS_ELEMENT_MAX + 1];
        char *suffix = "";

        if (as_collected) {
            // we need as-collected / raw data

            if(unlikely(rd->last_collected_time.tv_sec < instance->after)) {
                debug(D_BACKEND, "EXPORTING: not sending dimension '%s' of chart '%s' from host '%s', its last data collection (%lu) is not within our timeframe (%lu to %lu)", rd->id, rd->rrdset->id, instance->engine->config.hostname, (unsigned long)rd->last_collected_time.tv_sec, (unsigned long)instance->after, (unsigned long)instance->before);
                return 1;
            }

            if(homogeneous) {
                // all the dimensions of the chart, has the same algorithm, multiplier and divisor
                // we add all dimensions as labels

                prometheus_label_copy(dimension, (instance->config.options & EXPORTING_OPTION_SEND_NAMES && rd->name) ? rd->name : rd->id, PROMETHEUS_ELEMENT_MAX);
                snprintf(name, PROMETHEUS_LABELS_MAX, "%s_%s%s", instance->engine->config.prefix, context, suffix);

                add_metric(name, chart, family, dimension, instance->engine->config.hostname, rd->last_collected_value, timeval_msec(&rd->last_collected_time));
            }
            else {
                // the dimensions of the chart, do not have the same algorithm, multiplier or divisor
                // we create a metric per dimension

                prometheus_name_copy(dimension, (instance->config.options & EXPORTING_OPTION_SEND_NAMES && rd->name) ? rd->name : rd->id, PROMETHEUS_ELEMENT_MAX);
                snprintf(name, PROMETHEUS_LABELS_MAX, "%s_%s_%s%s", instance->engine->config.prefix, context, dimension, suffix);

                add_metric(name, chart, family, NULL, instance->engine->config.hostname, rd->last_collected_value, timeval_msec(&rd->last_collected_time));
            }
        }
        else {
            // we need average or sum of the data

            time_t first_t = instance->after, last_t = instance->before;
            // TODO: remove first_t
            (void)first_t;
            calculated_number value = exporting_calculate_value_from_stored_data(instance, rd, &last_t);

            if(!isnan(value) && !isinf(value)) {

                if(EXPORTING_OPTIONS_DATA_SOURCE(instance->config.options) == EXPORTING_SOURCE_DATA_AVERAGE)
                    suffix = "_average";
                else if(EXPORTING_OPTIONS_DATA_SOURCE(instance->config.options) == EXPORTING_SOURCE_DATA_SUM)
                    suffix = "_sum";

                prometheus_label_copy(dimension, (instance->config.options & EXPORTING_OPTION_SEND_NAMES && rd->name) ? rd->name : rd->id, PROMETHEUS_ELEMENT_MAX);
                snprintf(name, PROMETHEUS_LABELS_MAX, "%s_%s%s%s", instance->engine->config.prefix, context, units, suffix);

                add_metric(name, chart, family, dimension, instance->engine->config.hostname, value, last_t * MSEC_PER_SEC);
            }
        }
    }

    return 0;
}

/**
 * Format a batch for Prometheus Remote Write connector
 *
 * @param instance an instance data structure.
 * @return Returns 0 on success, 1 on failure.
 */
int format_batch_prometheus_remote_write(struct instance *instance)
{
    size_t data_size = get_write_request_size();

    if (unlikely(!data_size)) {
        error("EXPORTING: write request size is out of range");
        return 1;
    }

    BUFFER *buffer = instance->buffer;

    buffer_need_bytes(buffer, data_size);
    if (unlikely(pack_and_clear_write_request(buffer->buffer, &data_size))) {
        error("EXPORTING: cannot pack write request");
        return 1;
    }
    buffer->len = data_size;
    instance->stats.chart_buffered_bytes = (collected_number)buffer_strlen(buffer);

    return 0;
}

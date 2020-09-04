// SPDX-License-Identifier: GPL-3.0-or-later

#include "opentsdb.h"

/**
 * Initialize OpenTSDB telnet connector instance
 *
 * @param instance an instance data structure.
 * @return Returns 0 on success, 1 on failure.
 */
int init_opentsdb_telnet_instance(struct instance *instance)
{
    instance->worker = simple_connector_worker;

    struct simple_connector_config *connector_specific_config = callocz(1, sizeof(struct simple_connector_config));
    instance->config.connector_specific_config = (void *)connector_specific_config;
    connector_specific_config->default_port = 4242;

    instance->start_batch_formatting = NULL;
    instance->start_host_formatting = format_host_labels_opentsdb_telnet;
    instance->start_chart_formatting = NULL;

    if (EXPORTING_OPTIONS_DATA_SOURCE(instance->config.options) == EXPORTING_SOURCE_DATA_AS_COLLECTED)
        instance->metric_formatting = format_dimension_collected_opentsdb_telnet;
    else
        instance->metric_formatting = format_dimension_stored_opentsdb_telnet;

    instance->end_chart_formatting = NULL;
    instance->end_host_formatting = flush_host_labels;
    instance->end_batch_formatting = simple_connector_update_buffered_bytes;

    instance->prepare_header = NULL;
    instance->check_response = exporting_discard_response;

    instance->buffer = (void *)buffer_create(0);
    if (!instance->buffer) {
        error("EXPORTING: cannot create buffer for opentsdb telnet exporting connector instance %s", instance->config.name);
        return 1;
    }

    simple_connector_init(instance);

    if (uv_mutex_init(&instance->mutex))
        return 1;
    if (uv_cond_init(&instance->cond_var))
        return 1;

    return 0;
}

/**
 * Initialize OpenTSDB HTTP connector instance
 *
 * @param instance an instance data structure.
 * @return Returns 0 on success, 1 on failure.
 */
int init_opentsdb_http_instance(struct instance *instance)
{
    instance->worker = simple_connector_worker;

    struct simple_connector_config *connector_specific_config = callocz(1, sizeof(struct simple_connector_config));
    instance->config.connector_specific_config = (void *)connector_specific_config;
    connector_specific_config->default_port = 4242;

    struct simple_connector_data *connector_specific_data = callocz(1, sizeof(struct simple_connector_data));
#ifdef ENABLE_HTTPS
    connector_specific_data->flags = NETDATA_SSL_START;
    connector_specific_data->conn = NULL;
    if (instance->config.options & EXPORTING_OPTION_USE_TLS) {
        security_start_ssl(NETDATA_SSL_CONTEXT_OPENTSDB);
    }
#endif
    instance->connector_specific_data = connector_specific_data;

    instance->start_batch_formatting = open_batch_opentsdb_http;
    instance->start_host_formatting = format_host_labels_opentsdb_http;
    instance->start_chart_formatting = NULL;

    if (EXPORTING_OPTIONS_DATA_SOURCE(instance->config.options) == EXPORTING_SOURCE_DATA_AS_COLLECTED)
        instance->metric_formatting = format_dimension_collected_opentsdb_http;
    else
        instance->metric_formatting = format_dimension_stored_opentsdb_http;

    instance->end_chart_formatting = NULL;
    instance->end_host_formatting = flush_host_labels;
    instance->end_batch_formatting = close_batch_opentsdb_http;

    instance->prepare_header = opentsdb_http_send_header;
    instance->check_response = exporting_discard_response;

    instance->buffer = (void *)buffer_create(0);
    if (!instance->buffer) {
        error("EXPORTING: cannot create buffer for opentsdb HTTP exporting connector instance %s", instance->config.name);
        return 1;
    }

    simple_connector_init(instance);

    if (uv_mutex_init(&instance->mutex))
        return 1;
    if (uv_cond_init(&instance->cond_var))
        return 1;

    return 0;
}

/**
 * Copy a label value and substitute underscores in place of charachters which can't be used in OpenTSDB output
 *
 * @param dst a destination string.
 * @param src a source string.
 * @param len the maximum number of characters copied.
 */

void sanitize_opentsdb_label_value(char *dst, char *src, size_t len)
{
    while (*src != '\0' && len) {
        if (isalpha(*src) || isdigit(*src) || *src == '-' || *src == '_' || *src == '.' || *src == '/' || IS_UTF8_BYTE(*src))
            *dst++ = *src;
        else
            *dst++ = '_';
        src++;
        len--;
    }
    *dst = '\0';
}

/**
 * Format host labels for JSON connector
 *
 * @param instance an instance data structure.
 * @param host a data collecting host.
 * @return Always returns 0.
 */
int format_host_labels_opentsdb_telnet(struct instance *instance, RRDHOST *host)
{
    if (!instance->labels)
        instance->labels = buffer_create(1024);

    if (unlikely(!sending_labels_configured(instance)))
        return 0;

    rrdhost_check_rdlock(localhost);
    netdata_rwlock_rdlock(&host->labels_rwlock);
    for (struct label *label = host->labels; label; label = label->next) {
        if (!should_send_label(instance, label))
            continue;

        char value[CONFIG_MAX_VALUE + 1];
        sanitize_opentsdb_label_value(value, label->value, CONFIG_MAX_VALUE);

        if (*value)
            buffer_sprintf(instance->labels, " %s=%s", label->key, value);
    }
    netdata_rwlock_unlock(&host->labels_rwlock);

    return 0;
}

/**
 * Format dimension using collected data for OpenTSDB telnet connector
 *
 * @param instance an instance data structure.
 * @param rd a dimension.
 * @return Always returns 0.
 */
int format_dimension_collected_opentsdb_telnet(struct instance *instance, RRDDIM *rd)
{
    struct engine *engine = instance->engine;
    RRDSET *st = rd->rrdset;
    RRDHOST *host = st->rrdhost;

    char chart_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        chart_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && st->name) ? st->name : st->id,
        RRD_ID_LENGTH_MAX);

    char dimension_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        dimension_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && rd->name) ? rd->name : rd->id,
        RRD_ID_LENGTH_MAX);

    buffer_sprintf(
        instance->buffer,
        "put %s.%s.%s %llu " COLLECTED_NUMBER_FORMAT " host=%s%s%s%s\n",
        instance->config.prefix,
        chart_name,
        dimension_name,
        (unsigned long long)rd->last_collected_time.tv_sec,
        rd->last_collected_value,
        (host == localhost) ? engine->config.hostname : host->hostname,
        (host->tags) ? " " : "",
        (host->tags) ? host->tags : "",
        (instance->labels) ? buffer_tostring(instance->labels) : "");

    return 0;
}

/**
 * Format dimension using a calculated value from stored data for OpenTSDB telnet connector
 *
 * @param instance an instance data structure.
 * @param rd a dimension.
 * @return Always returns 0.
 */
int format_dimension_stored_opentsdb_telnet(struct instance *instance, RRDDIM *rd)
{
    struct engine *engine = instance->engine;
    RRDSET *st = rd->rrdset;
    RRDHOST *host = st->rrdhost;

    char chart_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        chart_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && st->name) ? st->name : st->id,
        RRD_ID_LENGTH_MAX);

    char dimension_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        dimension_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && rd->name) ? rd->name : rd->id,
        RRD_ID_LENGTH_MAX);

    time_t last_t;
    calculated_number value = exporting_calculate_value_from_stored_data(instance, rd, &last_t);

    if(isnan(value))
        return 0;

    buffer_sprintf(
        instance->buffer,
        "put %s.%s.%s %llu " CALCULATED_NUMBER_FORMAT " host=%s%s%s%s\n",
        instance->config.prefix,
        chart_name,
        dimension_name,
        (unsigned long long)last_t,
        value,
        (host == localhost) ? engine->config.hostname : host->hostname,
        (host->tags) ? " " : "",
        (host->tags) ? host->tags : "",
        (instance->labels) ? buffer_tostring(instance->labels) : "");

    return 0;
}

/**
 * Send header to a server
 *
 * @param sock a communication socket.
 * @param instance an instance data structure.
 * @return Returns 0 on success, 1 on failure.
 */
int opentsdb_http_send_header(int *sock, struct instance *instance)
{
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags += MSG_NOSIGNAL;
#endif

    struct simple_connector_data *connector_specific_data = instance->connector_specific_data;
    struct simple_connector_buffer *simple_connector_buffer = connector_specific_data->first_buffer;

    BUFFER *header = simple_connector_buffer->header;
    if (!header)
        header = buffer_create(0);

    buffer_sprintf(
        header,
        "POST /api/put HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %lu\r\n"
        "\r\n",
        instance->config.destination,
        buffer_strlen((BUFFER *)instance->buffer));

    size_t header_len = buffer_strlen(header);
    ssize_t written = 0;

#ifdef ENABLE_HTTPS
    if (instance->config.options & EXPORTING_OPTION_USE_TLS &&
        connector_specific_data->conn &&
        connector_specific_data->flags == NETDATA_SSL_HANDSHAKE_COMPLETE) {
        written = (ssize_t)SSL_write(connector_specific_data->conn, buffer_tostring(header), header_len);
    } else {
        written = send(*sock, buffer_tostring(header), header_len, flags);
    }
#else
    written = send(*sock, buffer_tostring(header), header_len, flags);
#endif

    buffer_flush(header);

    if (written != -1 && (size_t)written == header_len)
        return 0;
    else
        return 1;
}

/**
 * Open a JSON list for a bach
 *
 * @param instance an instance data structure.
 * @return Always returns 0.
 */
int open_batch_opentsdb_http(struct instance *instance){
    buffer_strcat(instance->buffer, "[\n");

    return 0;
}

/**
 * Close a JSON list for a bach and update buffered bytes counter
 *
 * @param instance an instance data structure.
 * @return Always returns 0.
 */
int close_batch_opentsdb_http(struct instance *instance){
    buffer_strcat(instance->buffer, "\n]\n");

    simple_connector_update_buffered_bytes(instance);

    return 0;
}

/**
 * Format host labels for OpenTSDB HTTP connector
 *
 * @param instance an instance data structure.
 * @param host a data collecting host.
 * @return Always returns 0.
 */
int format_host_labels_opentsdb_http(struct instance *instance, RRDHOST *host)
{
    if (!instance->labels)
        instance->labels = buffer_create(1024);

    if (unlikely(!sending_labels_configured(instance)))
        return 0;

    rrdhost_check_rdlock(host);
    netdata_rwlock_rdlock(&host->labels_rwlock);
    for (struct label *label = host->labels; label; label = label->next) {
        if (!should_send_label(instance, label))
            continue;

        char escaped_value[CONFIG_MAX_VALUE * 2 + 1];
        sanitize_json_string(escaped_value, label->value, CONFIG_MAX_VALUE);

        char value[CONFIG_MAX_VALUE + 1];
        sanitize_opentsdb_label_value(value, escaped_value, CONFIG_MAX_VALUE);

        if (*value) {
            buffer_strcat(instance->labels, ",");
            buffer_sprintf(instance->labels, "\"%s\":\"%s\"", label->key, value);
        }
    }
    netdata_rwlock_unlock(&host->labels_rwlock);

    return 0;
}

/**
 * Format dimension using collected data for OpenTSDB HTTP connector
 *
 * @param instance an instance data structure.
 * @param rd a dimension.
 * @return Always returns 0.
 */
int format_dimension_collected_opentsdb_http(struct instance *instance, RRDDIM *rd)
{
    struct engine *engine = instance->engine;
    RRDSET *st = rd->rrdset;
    RRDHOST *host = st->rrdhost;

    char chart_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        chart_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && st->name) ? st->name : st->id,
        RRD_ID_LENGTH_MAX);

    char dimension_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        dimension_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && rd->name) ? rd->name : rd->id,
        RRD_ID_LENGTH_MAX);

    if (buffer_strlen((BUFFER *)instance->buffer) > 2)
        buffer_strcat(instance->buffer, ",\n");

    buffer_sprintf(
        instance->buffer,
        "{"
        "\"metric\":\"%s.%s.%s\","
        "\"timestamp\":%llu,"
        "\"value\":"COLLECTED_NUMBER_FORMAT","
        "\"tags\":{"
        "\"host\":\"%s%s%s\"%s"
        "}"
        "}",
        instance->config.prefix,
        chart_name,
        dimension_name,
        (unsigned long long)rd->last_collected_time.tv_sec,
        rd->last_collected_value,
        (host == localhost) ? engine->config.hostname : host->hostname,
        (host->tags) ? " " : "",
        (host->tags) ? host->tags : "",
        instance->labels ? buffer_tostring(instance->labels) : "");

    return 0;
}

/**
 * Format dimension using a calculated value from stored data for OpenTSDB HTTP connector
 *
 * @param instance an instance data structure.
 * @param rd a dimension.
 * @return Always returns 0.
 */
int format_dimension_stored_opentsdb_http(struct instance *instance, RRDDIM *rd)
{
    struct engine *engine = instance->engine;
    RRDSET *st = rd->rrdset;
    RRDHOST *host = st->rrdhost;

    char chart_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        chart_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && st->name) ? st->name : st->id,
        RRD_ID_LENGTH_MAX);

    char dimension_name[RRD_ID_LENGTH_MAX + 1];
    exporting_name_copy(
        dimension_name,
        (instance->config.options & EXPORTING_OPTION_SEND_NAMES && rd->name) ? rd->name : rd->id,
        RRD_ID_LENGTH_MAX);

    time_t last_t;
    calculated_number value = exporting_calculate_value_from_stored_data(instance, rd, &last_t);

    if(isnan(value))
        return 0;

    if (buffer_strlen((BUFFER *)instance->buffer) > 2)
        buffer_strcat(instance->buffer, ",\n");

    buffer_sprintf(
        instance->buffer,
        "{"
        "\"metric\":\"%s.%s.%s\","
        "\"timestamp\":%llu,"
        "\"value\":"CALCULATED_NUMBER_FORMAT","
        "\"tags\":{"
        "\"host\":\"%s%s%s\"%s"
        "}"
        "}",
        instance->config.prefix,
        chart_name,
        dimension_name,
        (unsigned long long)last_t,
        value,
        (host == localhost) ? engine->config.hostname : host->hostname,
        (host->tags) ? " " : "",
        (host->tags) ? host->tags : "",
        instance->labels ? buffer_tostring(instance->labels) : "");

    return 0;
}

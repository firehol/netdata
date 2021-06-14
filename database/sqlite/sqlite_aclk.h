// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_SQLITE_ACLK_H
#define NETDATA_SQLITE_ACLK_H

#include "sqlite3.h"

#include "../../aclk/schema-wrappers/chart_stream.h"

static inline void uuid_unparse_lower_fix(uuid_t *uuid, char *out)
{
    uuid_unparse_lower(*uuid, out);
    out[8] = '_';
    out[13] = '_';
    out[18] = '_';
    out[23] = '_';
}

static inline char *get_str_from_uuid(uuid_t *uuid)
{
    char uuid_str[GUID_LEN + 1];
    if (unlikely(!uuid)) {
        uuid_t zero_uuid;
        uuid_clear(zero_uuid);
        uuid_unparse_lower(zero_uuid, uuid_str);
    }
    else
        uuid_unparse_lower(*uuid, uuid_str);
    return strdupz(uuid_str);
}

#define TABLE_ACLK_CHART "CREATE TABLE IF NOT EXISTS aclk_chart_%s (sequence_id INTEGER PRIMARY KEY, " \
        "date_created, date_updated, date_submitted, status, chart_id, unique_id, " \
        "update_count default 1, unique(chart_id, status));"

#define TABLE_ACLK_CHART_PAYLOAD "CREATE TABLE IF NOT EXISTS aclk_chart_payload_%s (unique_id BLOB PRIMARY KEY, " \
        "chart_id, type, date_created, payload);"

#define TRIGGER_ACLK_CHART_PAYLOAD "CREATE TRIGGER IF NOT EXISTS aclk_tr_chart_payload_%s " \
        "after insert on aclk_chart_payload_%s " \
        "begin insert into aclk_chart_%s (chart_id, unique_id, status, date_created) values " \
        " (new.chart_id, new.unique_id, 'pending', strftime('%%s')) on conflict(chart_id, status) " \
        " do update set unique_id = new.unique_id, update_count = update_count + 1; " \
        "end;"

//#define TABLE_ACLK_DIMENSION "DROP TABLE IF EXISTS aclk_dimension_%s;"

//#define TABLE_ACLK_DIMENSION_PAYLOAD "DROP TABLE IF EXISTS aclk_dimension_payload_%s;"
//
//#define TRIGGER_ACLK_DIMENSION_PAYLOAD "DROP TRIGGER IF EXISTS aclk_tr_dimension_payload_%s;"

#define TABLE_ACLK_ALERT "CREATE TABLE IF NOT EXISTS aclk_alert_%s (sequence_id INTEGER PRIMARY KEY, " \
                 "date_created, date_updated, date_submitted, status, alarm_id, unique_id, " \
                 "update_count DEFAULT 1, UNIQUE(alarm_id, status));"

#define TABLE_ACLK_ALERT_PAYLOAD "CREATE TABLE IF NOT EXISTS aclk_alert_payload_%s (unique_id BLOB PRIMARY KEY, " \
                 "ae_unique_id, alarm_id, type, date_created, payload);"

#define TRIGGER_ACLK_ALERT_PAYLOAD "CREATE TRIGGER IF NOT EXISTS aclk_tr_alert_payload_%s " \
        "after insert on aclk_alert_payload_%s " \
        "begin insert into aclk_alert_%s (alarm_id, unique_id, status, date_created) values " \
        " (new.alarm_id, new.unique_id, 'pending', strftime('%%s')) on conflict(alarm_id, status) " \
        " do update set unique_id = new.unique_id, update_count = update_count + 1; " \
        "end;"


enum aclk_database_opcode {
    /* can be used to return empty status or flush the command queue */
    ACLK_DATABASE_NOOP = 0,
    ACLK_DATABASE_CLEANUP,
    ACLK_DATABASE_TIMER,
    ACLK_DATABASE_ADD_CHART,
    ACLK_DATABASE_PUSH_CHART,
    ACLK_DATABASE_PUSH_CHART_CONFIG,
    ACLK_DATABASE_CHART_ACK,
    ACLK_DATABASE_PUSH_ALERT,
    ACLK_DATABASE_ADD_CHART_CONFIG,
    ACLK_DATABASE_PUSH_DIMENSION,
    ACLK_DATABASE_RESET_CHART,
    ACLK_DATABASE_STATUS_CHART,
    ACLK_DATABASE_RESET_NODE,
    ACLK_DATABASE_UPD_CHART,
    ACLK_DATABASE_UPD_ALERT,
    ACLK_DATABASE_SHUTDOWN,
    ACLK_DATABASE_ADD_ALARM,
    ACLK_DATABASE_DEDUP_CHART,
    ACLK_DATABASE_MAX_OPCODE
};

struct aclk_chart_payload_t {
    long sequence_id;
    long last_sequence_id;
    char *payload;
    struct aclk_chart_payload_t *next;
};


struct aclk_database_cmd {
    enum aclk_database_opcode opcode;
    void *data;
    void *data1;
    void *data_param;
    int count;
    uint64_t param1;
    struct completion *completion;
};

#define ACLK_DATABASE_CMD_Q_MAX_SIZE (2048)

struct aclk_database_cmdqueue {
    unsigned head, tail;
    struct aclk_database_cmd cmd_array[ACLK_DATABASE_CMD_Q_MAX_SIZE];
};

struct aclk_database_worker_config {
    uv_thread_t thread;
    char uuid_str[GUID_LEN + 1];
    char host_guid[GUID_LEN + 1];
    uv_loop_t *loop;
    RRDHOST *host;
    uv_async_t async;
    /* FIFO command queue */
    uv_mutex_t cmd_mutex;
    uv_cond_t cmd_cond;
    volatile unsigned queue_size;
    struct aclk_database_cmdqueue cmd_queue;
    int error;
    int chart_updates;
    int alert_updates;
};

static inline RRDHOST *find_host_by_node_id(char *node_id)
{
    uuid_t node_uuid;
    if (unlikely(!node_id))
        return NULL;

    uuid_parse(node_id, node_uuid);

    RRDHOST *host = localhost;
    while(host) {
        if (host->node_id && !(uuid_compare(*host->node_id, node_uuid)))
            return host;
        host = host->next;
    }
    return NULL;
}


//extern void sqlite_worker(void* arg);
extern void aclk_database_enq_cmd(struct aclk_database_worker_config *wc, struct aclk_database_cmd *cmd);

extern void sql_queue_chart_to_aclk(RRDSET *st, int cmd);
extern void sql_queue_alarm_to_aclk(RRDHOST *host, ALARM_ENTRY *ae);
extern sqlite3 *db_meta;
extern void sql_create_aclk_table(RRDHOST *host, uuid_t *host_uuid);
int aclk_add_chart_event(RRDSET *st, char *payload_type, struct completion *completion);
int aclk_push_chart_config_event(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
int aclk_add_alarm_event(RRDHOST *host, ALARM_ENTRY *ae, char *payload_type, struct completion *completion);
//void aclk_fetch_chart_event(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
void sql_reset_chart_event(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
void aclk_reset_chart_event(char *node_id, uint64_t last_sequence_id);
void aclk_status_chart_event(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
void aclk_reset_node_event(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
void aclk_push_chart_event(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
void sql_drop_host_aclk_table_list(uuid_t *host_uuid);
void aclk_ack_chart_sequence_id(char *node_id, uint64_t last_sequence_id);
void aclk_get_chart_config(char **hash_id_list);
void aclk_start_streaming(char *node_id);
void aclk_start_alert_streaming(char *node_id);
void sql_aclk_drop_all_table_list();
void sql_set_chart_ack(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
void aclk_submit_param_command(char *node_id, enum aclk_database_opcode aclk_command, uint64_t param);
extern void aclk_set_architecture(int mode);
char **build_dimension_payload_list(RRDSET *st, size_t **payload_list_size, size_t  *total);
void sql_chart_deduplicate(struct aclk_database_worker_config *wc, struct aclk_database_cmd cmd);
#endif //NETDATA_SQLITE_ACLK_H

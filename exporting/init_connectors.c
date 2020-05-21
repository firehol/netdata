// SPDX-License-Identifier: GPL-3.0-or-later

#include "exporting_engine.h"
#include "graphite/graphite.h"
#include "json/json.h"
#include "opentsdb/opentsdb.h"

#if ENABLE_PROMETHEUS_REMOTE_WRITE
#include "prometheus/remote_write/remote_write.h"
#endif

#if HAVE_KINESIS
#include "aws_kinesis/aws_kinesis.h"
#endif

#if ENABLE_EXPORTING_PUBSUB
#include "pubsub/pubsub.h"
#endif

#if HAVE_MONGOC
#include "mongodb/mongodb.h"
#endif

/**
 * Initialize connectors
 *
 * @param engine an engine data structure.
 * @return Returns 0 on success, 1 on failure.
 */
int init_connectors(struct engine *engine)
{
    engine->now = now_realtime_sec();

    for (struct instance *instance = engine->instance_root; instance; instance = instance->next) {
        instance->index = engine->instance_num++;
        instance->after = engine->now;

        switch (instance->config.type) {
            case EXPORTING_CONNECTOR_TYPE_GRAPHITE:
                if (init_graphite_instance(instance) != 0)
                    return 1;
                break;
            case EXPORTING_CONNECTOR_TYPE_JSON:
                if (init_json_instance(instance) != 0)
                    return 1;
                break;
            case EXPORTING_CONNECTOR_TYPE_OPENTSDB_USING_TELNET:
                if (init_opentsdb_telnet_instance(instance) != 0)
                    return 1;
                break;
            case EXPORTING_CONNECTOR_TYPE_OPENTSDB_USING_HTTP:
                if (init_opentsdb_http_instance(instance) != 0)
                    return 1;
                break;
            case EXPORTING_CONNECTOR_TYPE_PROMETHEUS_REMOTE_WRITE:
#if ENABLE_PROMETHEUS_REMOTE_WRITE
                if (init_prometheus_remote_write_instance(instance) != 0)
                    return 1;
#endif
                break;
            case EXPORTING_CONNECTOR_TYPE_KINESIS:
#if HAVE_KINESIS
                if (init_aws_kinesis_instance(instance) != 0)
                    return 1;
#endif
                break;
            case EXPORTING_CONNECTOR_TYPE_PUBSUB:
#if ENABLE_EXPORTING_PUBSUB
                if (init_pubsub_instance(instance) != 0)
                    return 1;
#endif
                break;
            case EXPORTING_CONNECTOR_TYPE_MONGODB:
#if HAVE_MONGOC
                if (init_mongodb_instance(instance) != 0)
                    return 1;
#endif
                break;
            default:
                error("EXPORTING: unknown exporting connector type");
                return 1;
        }

        // dispatch the instance worker thread
        int error = uv_thread_create(&instance->thread, instance->worker, instance);
        if (error) {
            error("EXPORTING: cannot create tread worker. uv_thread_create(): %s", uv_strerror(error));
            return 1;
        }
        char threadname[NETDATA_THREAD_NAME_MAX + 1];
        snprintfz(threadname, NETDATA_THREAD_NAME_MAX, "EXPORTING-%zu", instance->index);
        uv_thread_set_name_np(instance->thread, threadname);

        send_statistics("EXPORTING_START", "OK", instance->config.type_name);
    }

    return 0;
}

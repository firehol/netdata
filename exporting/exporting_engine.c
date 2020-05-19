// SPDX-License-Identifier: GPL-3.0-or-later

#include "exporting_engine.h"

static struct engine *engine = NULL;

/**
 * Clean up the main exporting thread and all connector workers on Netdata exit
 *
 * @param ptr thread data.
 */
static void exporting_main_cleanup(void *ptr)
{
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;
    static_thread->enabled = NETDATA_MAIN_THREAD_EXITING;

    info("cleaning up...");

    if (!engine) {
        static_thread->enabled = NETDATA_MAIN_THREAD_EXITED;
        return;
    }

    engine->exit = 1;

    int found = 0;
    usec_t max = 2 * USEC_PER_SEC, step = 50000;

    for (struct instance *instance = engine->instance_root; instance; instance = instance->next) {
        if (!instance->exited) {
            found++;
            info("stopping worker for instance %s", instance->config.name);
            uv_cond_signal(&instance->cond_var);
        } else
            info("found stopped worker for instance %s", instance->config.name);
    }

    while (found && max > 0) {
        max -= step;
        info("Waiting %d exporting connectors to finish...", found);
        sleep_usec(step);
        found = 0;

        for (struct instance *instance = engine->instance_root; instance; instance = instance->next) {
            if (!instance->exited)
                found++;
        }
    }

    for (struct instance *instance = engine->instance_root; instance;) {
        struct instance *current_instance = instance;
        instance = instance->next;
        clean_instance(current_instance);
    }

    if (engine->config.prefix)
        freez((void *)engine->config.prefix);
    if (engine->config.hostname)
        freez((void *)engine->config.hostname);
    freez(engine);

    static_thread->enabled = NETDATA_MAIN_THREAD_EXITED;
}

/**
 * Exporting engine main
 *
 * The main thread used to control the exporting engine.
 *
 * @param ptr a pointer to netdata_static_structure.
 *
 * @return It always returns NULL.
 */
void *exporting_main(void *ptr)
{
    netdata_thread_cleanup_push(exporting_main_cleanup, ptr);

    engine = read_exporting_config();
    if (!engine) {
        info("EXPORTING: no exporting connectors configured");
        goto cleanup;
    }

    if (init_connectors(engine) != 0) {
        error("EXPORTING: cannot initialize exporting connectors");
        goto cleanup;
    }

    RRDSET *st_main_rusage = NULL;
    RRDDIM *rd_main_user = NULL;
    RRDDIM *rd_main_system = NULL;
    create_main_rusage_chart(&st_main_rusage, &rd_main_user, &rd_main_system);

    usec_t step_ut = localhost->rrd_update_every * USEC_PER_SEC;
    heartbeat_t hb;
    heartbeat_init(&hb);

    while (!netdata_exit) {
        heartbeat_next(&hb, step_ut);
        engine->now = now_realtime_sec();

        if (mark_scheduled_instances(engine))
            prepare_buffers(engine);

        send_main_rusage(st_main_rusage, rd_main_user, rd_main_system);

#ifdef UNIT_TESTING
        return NULL;
#endif
    }

cleanup:
    netdata_thread_cleanup_pop(1);
    return NULL;
}

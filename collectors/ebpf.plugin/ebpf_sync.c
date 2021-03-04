// SPDX-License-Identifier: GPL-3.0-or-later

#include "ebpf.h"
#include "ebpf_sync.h"

static ebpf_data_t sync_data;

static struct bpf_link **probe_links = NULL;
static struct bpf_object *objects = NULL;


static char *sync_counter_dimension_name[NETDATA_SYNC_END] = { "sync", "sync_return" };
static netdata_syscall_stat_t sync_counter_aggregated_data[NETDATA_SYNC_END];
static netdata_publish_syscall_t sync_counter_publish_aggregated[NETDATA_SYNC_END];

static int read_thread_closed = 1;

static int *map_fd = NULL;
static netdata_idx_t sync_hash_values[NETDATA_SYNC_END] = {0, 0};

struct netdata_static_thread sync_threads = {"CACHESTAT KERNEL",
                                                  NULL, NULL, 1, NULL,
                                                  NULL,  NULL};

/*****************************************************************
 *
 *  DATA THREAD
 *
 *****************************************************************/

/**
 * Read global counter
 *
 * Read the table with number of calls for all functions
 */
static void read_global_table()
{
    uint32_t idx;
    netdata_idx_t *val = sync_hash_values;
    netdata_idx_t stored;
    int fd = map_fd[NETDATA_SYNC_GLOBLAL_TABLE];

    for (idx = NETDATA_SYNC_CALL; idx < NETDATA_SYNC_END; idx++) {
        if (!bpf_map_lookup_elem(fd, &idx, &stored)) {
            val[idx] = stored;
        }
    }
}

/**
 * Socket read hash
 *
 * This is the thread callback.
 * This thread is necessary, because we cannot freeze the whole plugin to read the data on very busy socket.
 *
 * @param ptr It is a NULL value for this thread.
 *
 * @return It always returns NULL.
 */
void *ebpf_sync_read_hash(void *ptr)
{
    UNUSED(ptr);
    read_thread_closed = 0;

    heartbeat_t hb;
    heartbeat_init(&hb);
    usec_t step = NETDATA_EBPF_SYNC_SLEEP_MS;

    while (!close_ebpf_plugin) {
        usec_t dt = heartbeat_next(&hb, step);
        (void)dt;

        read_global_table();
    }
    read_thread_closed = 1;

    return NULL;
}

/**
 * Send global
 *
 * Send global charts to Netdata
 */
static void sync_send_global()
{
}

/**
* Main loop for this collector.
*/
static void sync_collector(ebpf_module_t *em)
{
    sync_threads.thread = mallocz(sizeof(netdata_thread_t));
    sync_threads.start_routine = ebpf_sync_read_hash;

    map_fd = sync_data.map_fd;

    netdata_thread_create(sync_threads.thread, sync_threads.name, NETDATA_THREAD_OPTION_JOINABLE,
                          ebpf_sync_read_hash, em);

    while (!close_ebpf_plugin) {
        pthread_mutex_lock(&collect_data_mutex);
        pthread_cond_wait(&collect_data_cond_var, &collect_data_mutex);

        pthread_mutex_lock(&lock);

        sync_send_global();

        pthread_mutex_unlock(&lock);
        pthread_mutex_unlock(&collect_data_mutex);
    }
}


/*****************************************************************
 *
 *  CLEANUP THREAD
 *
 *****************************************************************/

/**
 * Clean up the main thread.
 *
 * @param ptr thread data.
 */
static void ebpf_sync_cleanup(void *ptr)
{
    ebpf_module_t *em = (ebpf_module_t *)ptr;
    if (!em->enabled)
        return;

    heartbeat_t hb;
    heartbeat_init(&hb);
    uint32_t tick = 2*USEC_PER_MS;
    while (!read_thread_closed) {
        usec_t dt = heartbeat_next(&hb, tick);
        UNUSED(dt);
    }

    freez(sync_threads.thread);

    struct bpf_program *prog;
    size_t i = 0 ;
    bpf_object__for_each_program(prog, objects) {
        bpf_link__destroy(probe_links[i]);
        i++;
    }
    bpf_object__close(objects);
}

/*****************************************************************
 *
 *  MAIN THREAD
 *
 *****************************************************************/

/**
 * Create global charts
 *
 * Call ebpf_create_chart to create the charts for the collector.
 */
static void ebpf_create_sync_charts(ebpf_module_t *em)
{
    ebpf_create_chart(NETDATA_EBPF_MEMORY_GROUP, NETDATA_EBPF_SYNC_CHART,
                      "Monitor calls for <a href=\"https://linux.die.net/man/2/sync\">sync(2)</a> syscall.",
                      EBPF_COMMON_DIMENSION_CALL, NETDATA_EBPF_SYNC_SUBMENU, NULL, 21300,
                      ebpf_create_global_dimension, &sync_counter_publish_aggregated, 1);

    if (em->mode == MODE_RETURN)
        ebpf_create_chart(NETDATA_EBPF_MEMORY_GROUP, NETDATA_EBPF_SYNC_CHART,
                          "Monitor return valor for <a href=\"https://linux.die.net/man/2/sync\">sync(2)</a> syscall.",
                          EBPF_COMMON_DIMENSION_CALL, NETDATA_EBPF_SYNC_SUBMENU, NULL, 21301,
                          ebpf_create_global_dimension, &sync_counter_publish_aggregated[NETDATA_SYNC_ERROR], 1);
}

/**
 * Sync thread
 *
 * Thread used to make sync thread
 *
 * @param ptr a pointer to `struct ebpf_module`
 *
 * @return It always return NULL
 */
void *ebpf_sync_thread(void *ptr)
{
    netdata_thread_cleanup_push(ebpf_sync_cleanup, ptr);

    ebpf_module_t *em = (ebpf_module_t *)ptr;
    fill_ebpf_data(&sync_data);

    if (!em->enabled)
        goto endsync;

    if (ebpf_update_kernel(&sync_data)) {
        pthread_mutex_unlock(&lock);
        goto endsync;
    }

    probe_links = ebpf_load_program(ebpf_plugin_dir, em, kernel_string, &objects, sync_data.map_fd);
    if (!probe_links) {
        pthread_mutex_unlock(&lock);
        goto endsync;
    }

    int algorithms[NETDATA_SYNC_END] =  { NETDATA_EBPF_INCREMENTAL_IDX, NETDATA_EBPF_INCREMENTAL_IDX};
    ebpf_global_labels(sync_counter_aggregated_data, sync_counter_publish_aggregated,
                       sync_counter_dimension_name, sync_counter_dimension_name,
                       algorithms, NETDATA_SYNC_END);

    pthread_mutex_lock(&lock);
    ebpf_create_sync_charts(em);
    pthread_mutex_unlock(&lock);

    sync_collector(em);

endsync:
    netdata_thread_cleanup_pop(1);
    return NULL;
}
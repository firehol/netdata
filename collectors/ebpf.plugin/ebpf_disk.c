// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/resource.h>

#include "ebpf.h"
#include "ebpf_disk.h"

struct config disk_config = { .first_section = NULL,
    .last_section = NULL,
    .mutex = NETDATA_MUTEX_INITIALIZER,
    .index = { .avl_tree = { .root = NULL, .compar = appconfig_section_compare },
        .rwlock = AVL_LOCK_INITIALIZER } };

static ebpf_local_maps_t disk_maps[] = {{.name = "tbl_disk_rcall", .internal_input = NETDATA_DISK_HISTOGRAM_LENGTH,
                                         .user_input = 0, .type = NETDATA_EBPF_MAP_STATIC,
                                         .map_fd = ND_EBPF_MAP_FD_NOT_INITIALIZED},
                                        {.name = "tbl_disk_wcall", .internal_input = NETDATA_DISK_HISTOGRAM_LENGTH,
                                         .user_input = 0, .type = NETDATA_EBPF_MAP_STATIC,
                                         .map_fd = ND_EBPF_MAP_FD_NOT_INITIALIZED},
                                        {.name = NULL, .internal_input = 0, .user_input = 0,
                                         .type = NETDATA_EBPF_MAP_CONTROLLER,
                                         .map_fd = ND_EBPF_MAP_FD_NOT_INITIALIZED}};
static ebpf_data_t disk_data;

static avl_tree_lock disk_tree;
netdata_ebpf_disks_t *disk_list = NULL;

char *tracepoint_block_type = { "block"} ;
char *tracepoint_block_issue = { "block_rq_issue" };
char *tracepoint_block_rq_complete = { "block_rq_complete" };

static struct bpf_link **probe_links = NULL;
static struct bpf_object *objects = NULL;

static int was_block_issue_enabled = 0;
static int was_block_rq_complete_enabled = 0;

static char **dimensions = NULL;
static netdata_syscall_stat_t disk_aggregated_data[NETDATA_EBPF_HIST_MAX_BINS];
static netdata_publish_syscall_t disk_publish_aggregated[NETDATA_EBPF_HIST_MAX_BINS];

static int read_thread_closed = 1;

static netdata_idx_t *disk_hash_values = NULL;
static struct netdata_static_thread disk_threads = {"DISK KERNEL",
                                                    NULL, NULL, 1, NULL,
                                                    NULL, NULL };

ebpf_publish_disk_t *plot_disks = NULL;
pthread_mutex_t plot_mutex;

/*****************************************************************
 *
 *  FUNCTIONS TO MANIPULATE HARD DISKS
 *
 *****************************************************************/

/**
 * Parse start
 *
 * Parse start address of disk
 *
 * @param w    structure where data is stored
 * @param text variable used to store value
 *
 * @return It returns 0 on success and -1 otherwise
 */
static inline int ebpf_disk_parse_start(netdata_ebpf_disks_t *w, char *text)
{
    int fd = open(text, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    ssize_t file_length = read(fd, text, 4095);
    if (file_length > 0) {
        text[file_length] = '\0';
        w->start = str2uint64_t(text);
    }
    close(fd);

    return 0;
}

/**
 * Parse uevent
 *
 * Parse uevent file
 *
 * @param w    structure where data is stored
 * @param text variable used to store value
 *
 * @return It returns 0 on success and -1 otherwise
 */
static inline int ebpf_parse_uevent(netdata_ebpf_disks_t *w, char *text)
{
    int fd = open(text, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    ssize_t file_length = read(fd, text, FILENAME_MAX);
    if (file_length > 0) {
        text[file_length] = '\0';

        char *s = strstr(text, "PARTNAME=EFI");
        if (s) {
            w->main->boot_partition = w;
            w->flags |= NETDATA_DISK_HAS_EFI;
            w->boot_chart = strdupz("disk_bootsector");
        }
    }
    close(fd);

    return 0;
}

/**
 * Parse Size
 *
 * @param w    structure where data is stored
 * @param text variable used to store value
 *
 * @return It returns 0 on success and -1 otherwise
 */
static inline int ebpf_parse_size(netdata_ebpf_disks_t *w, char *text)
{
    int fd = open(text, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    ssize_t file_length = read(fd, text, FILENAME_MAX);
    if (file_length > 0) {
        text[file_length] = '\0';
        w->end = w->start + str2uint64_t(text) -1;
    }
    close(fd);

    return 0;
}

/**
 * Read Disk information
 *
 * Read disk information from /sys/block
 *
 * @param w    structure where data is stored
 * @param name disk name
 */
static void ebpf_read_disk_info(netdata_ebpf_disks_t *w, char *name)
{
    static netdata_ebpf_disks_t *main_disk = NULL;
    static uint32_t key = 0;
    char *path = { "/sys/block" };
    char disk[NETDATA_DISK_NAME_LEN + 1];
    char text[FILENAME_MAX + 1];
    snprintfz(disk, NETDATA_DISK_NAME_LEN, "%s", name);
    size_t length = strlen(disk);
    if (!length) {
        return;
    }

    length--;
    size_t curr = length;
    while (isdigit((int)disk[length])) {
        disk[length--] = '\0';
    }

    // We are looking for partition information, if it is a device we will ignore it.
    if (curr == length) {
        main_disk = w;
        key = MKDEV(w->major, w->minor);
        w->bootsector_key = key;
        return;
    }
    w->bootsector_key = key;
    w->main = main_disk;

    snprintfz(text, FILENAME_MAX, "%s/%s/%s/uevent", path, disk, name);
    if (ebpf_parse_uevent(w, text))
        return;

    snprintfz(text, FILENAME_MAX, "%s/%s/%s/start", path, disk, name);
    if (ebpf_disk_parse_start(w, text))
        return;

    snprintfz(text, FILENAME_MAX, "%s/%s/%s/size", path, disk, name);
    ebpf_parse_size(w, text);
}

// Decode function extracted from: https://elixir.bootlin.com/linux/v5.10.8/source/include/linux/kdev_t.h#L46
/**
 * New encode dev
 *
 * New enconde algorithm
 *
 * @param major  driver major number
 * @param minor  driver minor number
 *
 * @return
 */
static inline uint32_t netdata_new_encode_dev(uint32_t major, uint32_t minor) {
    return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

/**
 * Compare disks
 *
 * Compare major and minor values to add disks to tree.
 *
 * @param a pointer to netdata_ebpf_disks
 * @param b pointer to netdata_ebpf_disks
 *
 * @return It returns 0 case the values are equal, 1 case a is bigger than b and -1 case a is smaller than b.
*/
static int ebpf_compare_disks(void *a, void *b)
{
    netdata_ebpf_disks_t *ptr1 = a;
    netdata_ebpf_disks_t *ptr2 = b;

    if (ptr1->dev > ptr2->dev)
        return 1;
    if (ptr1->dev < ptr2->dev)
        return -1;

    return 0;
}

/**
 * Update listen table
 *
 * Update link list when it is necessary.
 *
 * @param name the disk name
 */
static void update_disk_table(char *name, int major, int minor)
{
    netdata_ebpf_disks_t find;
    netdata_ebpf_disks_t *w;

    uint32_t dev = netdata_new_encode_dev(major, minor);
    find.dev = dev;
    netdata_ebpf_disks_t *ret = (netdata_ebpf_disks_t *) avl_search_lock(&disk_tree, (avl_t *)&find);
    if (ret) { // Disk is already present
        ret->flags |= NETDATA_DISK_IS_HERE;
        return;
    }

    netdata_ebpf_disks_t *update_next = disk_list;
    if (likely(disk_list)) {
        netdata_ebpf_disks_t *move = disk_list;
        while (move) {
            if (dev == move->dev)
                return;

            update_next = move;
            move = move->next;
        }

        w = callocz(1, sizeof(netdata_ebpf_disks_t));
        strcpy(w->family, name);
        w->major = major;
        w->minor = minor;
        w->dev = netdata_new_encode_dev(major, minor);
        update_next->next = w;
    } else {
        disk_list = callocz(1, sizeof(netdata_ebpf_disks_t));
        strcpy(disk_list->family, name);
        disk_list->major = major;
        disk_list->minor = minor;
        disk_list->dev = netdata_new_encode_dev(major, minor);

        w = disk_list;
    }

    ebpf_read_disk_info(w, name);

    netdata_ebpf_disks_t *check;
    check = (netdata_ebpf_disks_t *) avl_insert_lock(&disk_tree, (avl_t *)w);
    if (check != w)
        error("Internal error, cannot insert the AVL tree.");

#ifdef NETDATA_INTERNAL_CHECKS
    info("The Latency is monitoring the hard disk %s (Major = %d, Minor = %d, Device = %u)", name, major, minor,w->dev);
#endif

    w->flags |= NETDATA_DISK_IS_HERE;
}

/**
 *  Read Local Ports
 *
 *  Parse /proc/partitions to get block disks used to measure latency.
 *
 *  @return It returns 0 on success and -1 otherwise
 */
static int read_local_disks()
{
    char filename[FILENAME_MAX + 1];
    snprintfz(filename, FILENAME_MAX, "%s%s", netdata_configured_host_prefix, NETDATA_EBPF_PROC_PARTITIONS);
    procfile *ff = procfile_open(filename, " \t:", PROCFILE_FLAG_DEFAULT);
    if (!ff)
        return -1;

    ff = procfile_readall(ff);
    if (!ff)
        return -1;

    size_t lines = procfile_lines(ff), l;
    for(l = 2; l < lines ;l++) {
        size_t words = procfile_linewords(ff, l);
        // This is header or end of file
        if (unlikely(words < 4))
            continue;

        int major = (int)strtol(procfile_lineword(ff, l, 0), NULL, 10);
        // The main goal of this thread is to measure block devices, so any block device with major number
        // smaller than 7 according /proc/devices is not "important".
        if (major > 7) {
            int minor = (int)strtol(procfile_lineword(ff, l, 1), NULL, 10);
            update_disk_table(procfile_lineword(ff, l, 3), major, minor);
        }
    }

    procfile_close(ff);

    return 0;
}

/**
 * Update disks
 *
 * @param em main thread structure
 */
void ebpf_update_disks(ebpf_module_t *em)
{
    static time_t update_time = 0;
    time_t curr = now_realtime_sec();
    if (curr < update_time)
        return;

    update_time = curr + 5 * em->update_time;

    (void)read_local_disks();
}

/*****************************************************************
 *
 *  FUNCTIONS TO CLOSE THE THREAD
 *
 *****************************************************************/

/**
 * Disk disable tracepoints
 *
 * Disable tracepoints when the plugin was responsible to enable it.
 */
static void ebpf_disk_disable_tracepoints()
{
    char *default_message = { "Cannot disable the tracepoint" };
    if (!was_block_issue_enabled) {
        if (ebpf_disable_tracing_values(tracepoint_block_type, tracepoint_block_issue))
            error("%s %s/%s.", default_message, tracepoint_block_type, tracepoint_block_issue);
    }

    if (!was_block_rq_complete_enabled) {
        if (ebpf_disable_tracing_values(tracepoint_block_type, tracepoint_block_rq_complete))
            error("%s %s/%s.", default_message, tracepoint_block_type, tracepoint_block_rq_complete);
    }
}

/**
 * Cleanup plot disks
 *
 * Clean disk list
 */
static void ebpf_cleanup_plot_disks()
{
    ebpf_publish_disk_t *move = plot_disks, *next;
    while (move) {
        next = move->next;
        free(move);

        move = next;
    }
}

/**
 * Clean up the main thread.
 *
 * @param ptr thread data.
 */
static void ebpf_disk_cleanup(void *ptr)
{
    ebpf_disk_disable_tracepoints();

    ebpf_module_t *em = (ebpf_module_t *)ptr;
    if (!em->enabled)
        return;

    heartbeat_t hb;
    heartbeat_init(&hb);
    uint32_t tick = 2 * USEC_PER_MS;
    while (!read_thread_closed) {
        usec_t dt = heartbeat_next(&hb, tick);
        UNUSED(dt);
    }

    if (dimensions)
        ebpf_histogram_dimension_cleanup(dimensions, NETDATA_EBPF_HIST_MAX_BINS);

    freez(disk_hash_values);
    pthread_mutex_destroy(&plot_mutex);

    ebpf_cleanup_plot_disks();

    if (probe_links) {
        struct bpf_program *prog;
        size_t i = 0 ;
        bpf_object__for_each_program(prog, objects) {
            bpf_link__destroy(probe_links[i]);
            i++;
        }
        bpf_object__close(objects);
    }
}

/*****************************************************************
 *
 *  MAIN LOOP
 *
 *****************************************************************/

/**
 * Fill Plot list
 *
 * @param ptr a pointer for current disk
 */
static void ebpf_fill_plot_disks(netdata_ebpf_disks_t *ptr)
{
    pthread_mutex_lock(&plot_mutex);
    ebpf_publish_disk_t *w;
    if (likely(plot_disks)) {
        ebpf_publish_disk_t *move = plot_disks, *store;
        while (move) {
            if (move->plot == ptr) {
                pthread_mutex_unlock(&plot_mutex);
                return;
            }

            store = move;
            move = move->next;
        }

        w = callocz(1, sizeof(ebpf_publish_disk_t));
        w->plot = ptr;
        store->next = w;
    } else {
        w = callocz(1, sizeof(ebpf_publish_disk_t));
        w->plot = ptr;
    }
    pthread_mutex_unlock(&plot_mutex);

    ptr->flags |= NETDATA_DISK_ADDED_TO_PLOT_LIST;
}

/**
 * Read hard disk table
 *
 * @param table file descriptor for table
 *
 * Read the table with number of calls for all functions
 */
static void read_hard_disk_tables(int table)
{
    netdata_idx_t *values = disk_hash_values;
    block_key_t key = {};
    block_key_t next_key = {};
    int cmp_table = disk_maps[NETDATA_DISK_READ].map_fd;

    netdata_ebpf_disks_t *ret = NULL;

    while (bpf_map_get_next_key(table, &key, &next_key) == 0) {
        int test = bpf_map_lookup_elem(table, &key, values);
        if (test < 0) {
            key = next_key;
            continue;
        }

        netdata_ebpf_disks_t find;
        find.dev = key.dev;

        if (likely(ret)) {
            if (find.dev != ret->dev)
                ret = (netdata_ebpf_disks_t *)avl_search_lock(&disk_tree, (avl_t *)&find);
        } else
            ret = (netdata_ebpf_disks_t *)avl_search_lock(&disk_tree, (avl_t *)&find);

        // Disk was inserted after we parse /proc/partitions
        if (!ret) {
            if (read_local_disks()) {
                key = next_key;
                continue;
            }

            ret = (netdata_ebpf_disks_t *)avl_search_lock(&disk_tree, (avl_t *)&find);
            if (!ret) {
                // We should never reach this point, but we are adding it to keep a safe code
                key = next_key;
                continue;
            }
        }

        uint64_t total = 0;
        int i;
        int end = (running_on_kernel < NETDATA_KERNEL_V4_15) ? 1 : ebpf_nprocs;
        for (i = 0; i < end; i++) {
            total += values[i];
        }

        if (table == cmp_table) {
            ret->hread.histogram[key.bin] = total;
        } else {
            ret->hwrite.histogram[key.bin] = total;
        }

        if (!(ret->flags & NETDATA_DISK_ADDED_TO_PLOT_LIST))
            ebpf_fill_plot_disks(ret);

        key = next_key;
    }
}

/**
 * Disk read hash
 *
 * This is the thread callback.
 * This thread is necessary, because we cannot freeze the whole plugin to read the data on very busy socket.
 *
 * @param ptr It is a NULL value for this thread.
 *
 * @return It always returns NULL.
 */
void *ebpf_disk_read_hash(void *ptr)
{
    heartbeat_t hb;
    heartbeat_init(&hb);

    ebpf_module_t *em = (ebpf_module_t *)ptr;

    usec_t step = NETDATA_LATENCY_DISK_SLEEP_MS * em->update_time;
    while (!close_ebpf_plugin) {
        usec_t dt = heartbeat_next(&hb, step);
        (void)dt;

        read_hard_disk_tables(disk_maps[NETDATA_DISK_READ].map_fd);
        read_hard_disk_tables(disk_maps[NETDATA_DISK_WRITE].map_fd);
    }

    return NULL;
}

/**
 * Create Hard Disk charts
 *
 * @param w the structure with necessary information to create the chart
 *
 * Make Hard disk charts and fill chart name
 */
static void ebpf_create_hd_charts(netdata_ebpf_disks_t *w)
{
    char title[256];
    int order = NETDATA_CHART_PRIO_DISK_LATENCY;
    char *family = w->family;

    snprintf(title, 255, "Disk latency %s for output.", family);
    w->hread.name = strdupz("disk_latency_output");
    w->hread.title = strdupz(title);
    w->hread.order = order;

    ebpf_create_chart(w->hread.name, family, title, EBPF_COMMON_DIMENSION_CALL,
                      family, "disk.latency_output", NETDATA_EBPF_CHART_TYPE_STACKED, order,
                      ebpf_create_global_dimension, disk_publish_aggregated, NETDATA_EBPF_HIST_MAX_BINS);
    order++;

    snprintf(title, 255, "Disk latency %s for input.", family);
    w->hwrite.name = strdupz("disk_latency_input");
    w->hwrite.title = strdupz(title);
    w->hwrite.order = order;

    ebpf_create_chart(w->hwrite.name, family, title, EBPF_COMMON_DIMENSION_CALL,
                      family, "disk.latency_input", NETDATA_EBPF_CHART_TYPE_STACKED, order,
                      ebpf_create_global_dimension, disk_publish_aggregated, NETDATA_EBPF_HIST_MAX_BINS);

    w->flags |= NETDATA_DISK_CHART_CREATED;
}

/**
 * Remove pointer from plot
 *
 * Remove pointer from plot list when the disk is not present.
 */
static void ebpf_remove_pointer_from_plot_disk()
{
    ebpf_publish_disk_t *move = plot_disks, *prev = plot_disks;
    while (move) {
        netdata_ebpf_disks_t *ned = move->plot;
        uint32_t flags = ned->flags;

        if (!(flags & NETDATA_DISK_IS_HERE)) {
            if (move == plot_disks) {
                freez(move);
                plot_disks = NULL;
                break;
            } else {
                prev->next = move->next;
                ebpf_publish_disk_t *clean = move;
                move = move->next;
                freez(clean);
                continue;
            }
        }

        prev = move;
        move = move->next;
    }
}

/**
 * Send Hard disk data
 *
 * Send hard disk information to Netdata.
 */
static void ebpf_latency_send_hd_data()
{
    if (likely(plot_disks)) {
        return;
    }

    ebpf_remove_pointer_from_plot_disk();

    ebpf_publish_disk_t *move = plot_disks;
    while (move) {
        netdata_ebpf_disks_t *ned = move->plot;
        uint32_t flags = ned->flags;
        if (!(flags & NETDATA_DISK_CHART_CREATED)) {
            ebpf_create_hd_charts(ned);
        }

        if ((flags & NETDATA_DISK_IS_HERE)) {
            /*
            write_histogram_chart(ned->hread.name, ned->family,
                                  ned->hread.histogram, dimensions, NETDATA_EBPF_HIST_MAX_BINS);

            write_histogram_chart(ned->hwrite.name, ned->family,
                                  ned->hwrite.histogram, dimensions, NETDATA_EBPF_HIST_MAX_BINS);
                                  */
        }

        ned->flags &= ~NETDATA_DISK_IS_HERE;

        move = move->next;
    }
}

/**
* Main loop for this collector.
*/
static void disk_collector(ebpf_module_t *em)
{
    disk_hash_values = callocz(ebpf_nprocs, sizeof(netdata_idx_t));
    disk_threads.thread = mallocz(sizeof(netdata_thread_t));
    disk_threads.start_routine = ebpf_disk_read_hash;

    netdata_thread_create(disk_threads.thread, disk_threads.name, NETDATA_THREAD_OPTION_JOINABLE,
                          ebpf_disk_read_hash, em);


    read_thread_closed = 0;
    while (!close_ebpf_plugin) {
        pthread_mutex_lock(&collect_data_mutex);
        pthread_cond_wait(&collect_data_cond_var, &collect_data_mutex);

        pthread_mutex_lock(&lock);
        ebpf_remove_pointer_from_plot_disk();
        ebpf_latency_send_hd_data();

        pthread_mutex_unlock(&lock);
        pthread_mutex_unlock(&collect_data_mutex);

        ebpf_update_disks(em);
    }
    read_thread_closed = 1;
}

/*****************************************************************
 *
 *  EBPF DISK THREAD
 *
 *****************************************************************/

/**
 * Enable tracepoints
 *
 * Enable necessary tracepoints for thread.
 *
 * @return  It returns 0 on success and -1 otherwise
 */
static int ebpf_disk_enable_tracepoints()
{
    int test = ebpf_is_tracepoint_enabled(tracepoint_block_type, tracepoint_block_issue);
    if (test == -1)
        return -1;
    else if (!test) {
        if (ebpf_enable_tracing_values(tracepoint_block_type, tracepoint_block_issue))
            return -1;
    }
    was_block_issue_enabled = test;

    test = ebpf_is_tracepoint_enabled(tracepoint_block_type, tracepoint_block_rq_complete);
    if (test == -1)
        return -1;
    else if (!test) {
        if (ebpf_enable_tracing_values(tracepoint_block_type, tracepoint_block_rq_complete))
            return -1;
    }
    was_block_rq_complete_enabled = test;

    return 0;
}

/**
 * Disk thread
 *
 * Thread used to generate disk charts.
 *
 * @param ptr a pointer to `struct ebpf_module`
 *
 * @return It always return NULL
 */
void *ebpf_disk_thread(void *ptr)
{
    netdata_thread_cleanup_push(ebpf_disk_cleanup, ptr);

    ebpf_module_t *em = (ebpf_module_t *)ptr;
    em->maps = disk_maps;

    fill_ebpf_data(&disk_data);

    if (!em->enabled)
        goto enddisk;

    if (ebpf_update_kernel(&disk_data)) {
        goto enddisk;
    }

    if (ebpf_disk_enable_tracepoints()) {
        em->enabled = CONFIG_BOOLEAN_NO;
        goto enddisk;
    }

    avl_init_lock(&disk_tree, ebpf_compare_disks);
    if (read_local_disks()) {
        em->enabled = CONFIG_BOOLEAN_NO;
        goto enddisk;
    }

    if (pthread_mutex_init(&plot_mutex, NULL)) {
        error("Cannot initialize local mutex");
        goto enddisk;
    }

    probe_links = ebpf_load_program(ebpf_plugin_dir, em, kernel_string, &objects, disk_data.map_fd);
    if (!probe_links) {
        goto enddisk;
    }

    int algorithms[NETDATA_EBPF_HIST_MAX_BINS];
    ebpf_fill_algorithms(algorithms, NETDATA_EBPF_HIST_MAX_BINS, NETDATA_EBPF_INCREMENTAL_IDX);
    dimensions = ebpf_fill_histogram_dimension(NETDATA_EBPF_HIST_MAX_BINS);

    ebpf_global_labels(disk_aggregated_data, disk_publish_aggregated, dimensions, dimensions, algorithms,
                       NETDATA_EBPF_HIST_MAX_BINS);

    disk_collector(em);

enddisk:
    netdata_thread_cleanup_pop(1);

    return NULL;
}

// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/resource.h>

#include "ebpf.h"
#include "ebpf_socket.h"

/*****************************************************************
 *
 *  GLOBAL VARIABLES
 *
 *****************************************************************/

static char *socket_dimension_names[NETDATA_MAX_SOCKET_VECTOR] = { "sent", "received", "close", "sent", "received" };
static char *socket_id_names[NETDATA_MAX_SOCKET_VECTOR] = { "tcp_sendmsg", "tcp_cleanup_rbuf", "tcp_close",
                                                            "udp_sendmsg", "udp_recvmsg" };

static netdata_idx_t *socket_hash_values = NULL;
static netdata_syscall_stat_t *socket_aggregated_data = NULL;
static netdata_publish_syscall_t *socket_publish_aggregated = NULL;

static ebpf_data_t socket_data;

static ebpf_socket_publish_apps_t **socket_bandwidth_curr = NULL;
static ebpf_socket_publish_apps_t **socket_bandwidth_prev = NULL;
static ebpf_bandwidth_t *bandwidth_vector = NULL;

static int socket_apps_created = 0;

static int *map_fd = NULL;

/*****************************************************************
 *
 *  PROCESS DATA AND SEND TO NETDATA
 *
 *****************************************************************/

/**
 * Update publish structure before to send data to Netdata.
 *
 * @param publish  the first output structure with independent dimensions
 * @param tcp      structure to store IO from tcp sockets
 * @param udp      structure to store IO from udp sockets
 * @param input    the structure with the input data.
 */
static void ebpf_update_global_publish(
    netdata_publish_syscall_t *publish, netdata_publish_vfs_common_t *tcp, netdata_publish_vfs_common_t *udp,
    netdata_syscall_stat_t *input)
{
    netdata_publish_syscall_t *move = publish;
    while (move) {
        if (input->call != move->pcall) {
            //This condition happens to avoid initial values with dimensions higher than normal values.
            if (move->pcall) {
                move->ncall = (input->call > move->pcall) ? input->call - move->pcall : move->pcall - input->call;
                move->nbyte = (input->bytes > move->pbyte) ? input->bytes - move->pbyte : move->pbyte - input->bytes;
                move->nerr = (input->ecall > move->nerr) ? input->ecall - move->perr : move->perr - input->ecall;
            } else {
                move->ncall = 0;
                move->nbyte = 0;
                move->nerr = 0;
            }

            move->pcall = input->call;
            move->pbyte = input->bytes;
            move->perr = input->ecall;
        } else {
            move->ncall = 0;
            move->nbyte = 0;
            move->nerr = 0;
        }

        input = input->next;
        move = move->next;
    }

    tcp->write = -((long)publish[0].nbyte);
    tcp->read = (long)publish[1].nbyte;

    udp->write = -((long)publish[3].nbyte);
    udp->read = (long)publish[4].nbyte;
}

/**
 * Update the publish strctures to create the dimenssions
 *
 * @param curr   Last values read from memory.
 * @param prev   Previous values read from memory.
 */
static void ebpf_socket_update_apps_publish(ebpf_socket_publish_apps_t *curr, ebpf_socket_publish_apps_t *prev)
{
    curr->publish_recv = curr->received - prev->received;
    curr->publish_sent = curr->sent - prev->sent;
}

/**
 * Send data to Netdata calling auxiliar functions.
 *
 * @param em the structure with thread information
 */
static void ebpf_socket_send_data(ebpf_module_t *em)
{
    netdata_publish_vfs_common_t common_tcp;
    netdata_publish_vfs_common_t common_udp;
    ebpf_update_global_publish(socket_publish_aggregated, &common_tcp, &common_udp, socket_aggregated_data);

    write_count_chart(
      NETDATA_TCP_FUNCTION_COUNT, NETDATA_EBPF_FAMILY, socket_publish_aggregated, 3);
    write_io_chart(
        NETDATA_TCP_FUNCTION_BYTES, NETDATA_EBPF_FAMILY, socket_id_names[0], socket_id_names[1], &common_tcp);
    if (em->mode < MODE_ENTRY) {
        write_err_chart(
          NETDATA_TCP_FUNCTION_ERROR, NETDATA_EBPF_FAMILY, socket_publish_aggregated, 2);
    }

    write_count_chart(
        NETDATA_UDP_FUNCTION_COUNT, NETDATA_EBPF_FAMILY, &socket_publish_aggregated[NETDATA_UDP_START], 2);
    write_io_chart(
        NETDATA_UDP_FUNCTION_BYTES, NETDATA_EBPF_FAMILY, socket_id_names[3], socket_id_names[4], &common_udp);
    if (em->mode < MODE_ENTRY) {
        write_err_chart(
            NETDATA_UDP_FUNCTION_ERROR, NETDATA_EBPF_FAMILY, &socket_publish_aggregated[NETDATA_UDP_START], 2);
    }
}

/**
 * Sum values for pid
 *
 * @param root the structure with all available PIDs
 *
 * @param offset the address that we are reading
 *
 * @return it returns the sum of all PIDs
 */
long long ebpf_socket_sum_values_for_pids(struct pid_on_target *root, size_t offset)
{
    long long ret = 0;
    while (root) {
        int32_t pid = root->pid;
        ebpf_socket_publish_apps_t *w = socket_bandwidth_curr[pid];
        if (w) {
            ret += get_value_from_structure((char *)w, offset);
        }

        root = root->next;
    }

    return ret;
}

/**
 * Send data to Netdata calling auxiliar functions.
 *
 * @param em   the structure with thread information
 * @param root the target list.
 */
void ebpf_socket_send_apps_data(ebpf_module_t *em, struct target *root)
{
    UNUSED(em);
    if (!socket_apps_created)
        return;

    struct target *w;
    collected_number value;

    write_begin_chart(NETDATA_APPS_FAMILY, NETDATA_NET_APPS_BANDWIDTH_SENT);
    for (w = root; w; w = w->next) {
        if (unlikely(w->exposed && w->processes)) {
            value = ebpf_socket_sum_values_for_pids(w->root_pid, offsetof(ebpf_socket_publish_apps_t, publish_sent));
            write_chart_dimension(w->name, value);
        }
    }
    write_end_chart();

    write_begin_chart(NETDATA_APPS_FAMILY, NETDATA_NET_APPS_BANDWIDTH_RECV);
    for (w = root; w; w = w->next) {
        if (unlikely(w->exposed && w->processes)) {
            value = ebpf_socket_sum_values_for_pids(w->root_pid, offsetof(ebpf_socket_publish_apps_t, publish_recv));
            write_chart_dimension(w->name, value);
        }
    }
    write_end_chart();
}

/*****************************************************************
 *
 *  FUNCTIONS TO CREATE CHARTS
 *
 *****************************************************************/

/**
 * Create global charts
 *
 * Call ebpf_create_chart to create the charts for the collector.
 *
 * @param em a pointer to the structure with the default values.
 */
static void ebpf_create_global_charts(ebpf_module_t *em)
{
    ebpf_create_chart(NETDATA_EBPF_FAMILY,
                      NETDATA_TCP_FUNCTION_COUNT,
                      "Calls to internal functions",
                      EBPF_COMMON_DIMENSION_CALL,
                      NETDATA_SOCKET_GROUP,
                      21070,
                      ebpf_create_global_dimension,
                      socket_publish_aggregated,
                      3);

    ebpf_create_chart(NETDATA_EBPF_FAMILY,
                      NETDATA_TCP_FUNCTION_BYTES,
                      "TCP bandwidth",
                      EBPF_COMMON_DIMENSION_BYTESS,
                      NETDATA_SOCKET_GROUP,
                      21071,
                      ebpf_create_global_dimension,
                      socket_publish_aggregated,
                      3);

    if (em->mode < MODE_ENTRY) {
        ebpf_create_chart(NETDATA_EBPF_FAMILY,
                          NETDATA_TCP_FUNCTION_ERROR,
                          "TCP errors",
                          EBPF_COMMON_DIMENSION_CALL,
                          NETDATA_SOCKET_GROUP,
                          21072,
                          ebpf_create_global_dimension,
                          socket_publish_aggregated,
                          2);
    }

    ebpf_create_chart(NETDATA_EBPF_FAMILY,
                      NETDATA_UDP_FUNCTION_COUNT,
                      "UDP calls",
                      EBPF_COMMON_DIMENSION_CALL,
                      NETDATA_SOCKET_GROUP,
                      21073,
                      ebpf_create_global_dimension,
                      &socket_publish_aggregated[NETDATA_UDP_START],
                      2);

    ebpf_create_chart(NETDATA_EBPF_FAMILY,
                      NETDATA_UDP_FUNCTION_BYTES,
                      "UDP bandwidth",
                      EBPF_COMMON_DIMENSION_BYTESS,
                      NETDATA_SOCKET_GROUP,
                      21074,
                      ebpf_create_global_dimension,
                      &socket_publish_aggregated[NETDATA_UDP_START],
                      2);

    if (em->mode < MODE_ENTRY) {
        ebpf_create_chart(NETDATA_EBPF_FAMILY,
                          NETDATA_UDP_FUNCTION_ERROR,
                          "UDP errors",
                          EBPF_COMMON_DIMENSION_CALL,
                          NETDATA_SOCKET_GROUP,
                          21075,
                          ebpf_create_global_dimension,
                          &socket_publish_aggregated[NETDATA_UDP_START],
                          2);
    }
}

/**
 * Create apps charts
 *
 * Call ebpf_create_chart to create the charts on apps submenu.
 *
 * @param em a pointer to the structure with the default values.
 */
void ebpf_socket_create_apps_charts(ebpf_module_t *em, struct target *root)
{
    UNUSED(em);
    ebpf_create_charts_on_apps(NETDATA_NET_APPS_BANDWIDTH_SENT,
                               "Bytes sent",
                               EBPF_COMMON_DIMENSION_BYTESS,
                               NETDATA_APPS_NET_GROUP,
                               20080,
                               root);

    ebpf_create_charts_on_apps(NETDATA_NET_APPS_BANDWIDTH_RECV,
                               "bytes received",
                               EBPF_COMMON_DIMENSION_BYTESS,
                               NETDATA_APPS_NET_GROUP,
                               20081,
                               root);

    socket_apps_created = 1;
}

/*****************************************************************
 *
 *  READ INFORMATION FROM KERNEL RING
 *
 *****************************************************************/

/**
 * Build dimension name
 *
 * Fill dimension name vector with values given
 *
 * @param dimname       the output vector
 * @param hostname      the hostname for the socket.
 * @param service_name  the service used to connect.
 * @param is_inbound    is this an inbound connection.
 *
 * @return  it returns the size of the data copied on success and -1 otherwise.
 */
static inline int build_dimension_name(char *dimname, char *hostname, char *service_name,
                                       int is_inbound, netdata_socket_t *sock)
{
    int size;
    if (!is_inbound) {
        size = snprintf(dimname, CONFIG_MAX_NAME - 1, "%s:%s:%s_",
                        service_name, (sock->protocol == IPPROTO_UDP) ? "UDP" : "TCP",
                        hostname);

    } else {
        size = snprintf(dimname, CONFIG_MAX_NAME -1, "%s:%s_", service_name,
                        (sock->protocol == IPPROTO_UDP)?"UDP":"TCP");

    }

    return size;
}

/**
 * Fill Resolved Name
 *
 * Fill the resolved name structure with the value given.
 * The hostname is the largest value possible, if it is necessary to cut some value, it must be cut.
 *
 * @param ptr       the output vector
 * @param hostname  the hostname resolved or IP.
 * @param length    the length for the hostname.
 */
static inline void fill_resolved_name(netdata_socket_plot_t *ptr, char *hostname, size_t length)
{
    if (length < NETDATA_MAX_NETWORK_COMBINED_LENGTH)
        ptr->resolved_name = strdupz(hostname);
    else {
        length = NETDATA_MAX_NETWORK_COMBINED_LENGTH;
        ptr->resolved_name = mallocz(NETDATA_DIM_LENGTH_WITHOUT_SERVICE_PROTOCOL + 1);
        memcpy(ptr->resolved_name, hostname, NETDATA_DIM_LENGTH_WITHOUT_SERVICE_PROTOCOL);
        ptr->resolved_name[length] = '\0';
    }
}

/**
 * Mount dimension names
 *
 * Fill the vector names after to resolve the addresses
 *
 * @param ptr a pointer to the structure where the values are stored.
 * @param is_inbound is a inbound ptr value?
 */
void fill_names(netdata_socket_plot_t *ptr, int is_inbound)
{
    if (ptr->resolved)
        return;

    netdata_socket_idx_t *idx = &ptr->index;
    netdata_socket_t *sock = &ptr->sock;

    char *errname = { "Not resolved" };
    char hostname[NI_MAXHOST], service_name[NI_MAXSERV];
    // Resolve Name
    if (ptr->family == AF_INET) { //IPV4
        struct sockaddr_in myaddr;
        memset(&myaddr, 0 , sizeof(myaddr));

        myaddr.sin_family = ptr->family;
        myaddr.sin_port = idx->dport;
        myaddr.sin_addr.s_addr = (!is_inbound)?idx->daddr.addr32[0]:idx->saddr.addr32[0];

        if (getnameinfo((struct sockaddr *)&myaddr, sizeof(myaddr), hostname,
                         sizeof(hostname), service_name, sizeof(service_name), NI_NAMEREQD)) {
            //I cannot resolve the name, I will use the IP
            if (!inet_ntop(AF_INET, &myaddr.sin_addr, hostname, NI_MAXHOST))
                memcpy(hostname, errname, 12);
        }
    } else { //IPV6
        struct sockaddr_in6 myaddr6;
        memset(&myaddr6, 0 , sizeof(myaddr6));

        myaddr6.sin6_family = AF_INET6;
        myaddr6.sin6_port =  idx->dport;
        memcpy(myaddr6.sin6_addr.s6_addr, (!is_inbound)?idx->daddr.addr32:idx->saddr.addr32, sizeof(union netdata_ip));
        if (getnameinfo((struct sockaddr *)&myaddr6, sizeof(myaddr6), hostname,
                        sizeof(hostname), service_name, sizeof(service_name), NI_NAMEREQD)) {
            //I cannot resolve the name, I will use the IP
            if (!inet_ntop(AF_INET6, myaddr6.sin6_addr.s6_addr, hostname, NI_MAXHOST))
                memcpy(hostname, errname, 12);
        }
    }

    fill_resolved_name(ptr, hostname,  strlen(hostname) + strlen(service_name)+ NETDATA_DOTS_PROTOCOL_COMBINED_LENGTH);

    char dimname[CONFIG_MAX_NAME];
    int size = build_dimension_name(dimname, hostname, service_name, is_inbound, sock);
    if (size > 0) {
        strcpy(&dimname[size], "sent");
        dimname[size + 1] = '\0';
        ptr->dimension_sent = strdupz(dimname);

        strcpy(&dimname[size], "recv");
        ptr->dimension_recv = strdupz(dimname);
    }

    ptr->resolved++;
#ifdef NETDATA_INTERNAL_CHECKS
    info("The dimensions will be added Sent(%s) Recv(%s)", ptr->dimension_sent, ptr->dimension_recv);
#endif
}

/**
 * Read the hash table and store data to allocated vectors.
 */
static void read_hash_global_tables()
{
    uint64_t idx;
    netdata_idx_t res[NETDATA_SOCKET_COUNTER];

    netdata_idx_t *val = socket_hash_values;
    int fd = map_fd[4];
    for (idx = 0; idx < NETDATA_SOCKET_COUNTER; idx++) {
        if (!bpf_map_lookup_elem(fd, &idx, val)) {
            uint64_t total = 0;
            int i;
            int end = (running_on_kernel < NETDATA_KERNEL_V4_15) ? 1 : ebpf_nprocs;
            for (i = 0; i < end; i++)
                total += val[i];

            res[idx] = total;
        } else {
            res[idx] = 0;
        }
    }

    socket_aggregated_data[0].call = res[NETDATA_KEY_CALLS_TCP_SENDMSG];
    socket_aggregated_data[1].call = res[NETDATA_KEY_CALLS_TCP_CLEANUP_RBUF];
    socket_aggregated_data[2].call = res[NETDATA_KEY_CALLS_TCP_CLOSE];
    socket_aggregated_data[3].call = res[NETDATA_KEY_CALLS_UDP_RECVMSG];
    socket_aggregated_data[4].call = res[NETDATA_KEY_CALLS_UDP_SENDMSG];

    socket_aggregated_data[0].ecall = res[NETDATA_KEY_ERROR_TCP_SENDMSG];
    socket_aggregated_data[1].ecall = res[NETDATA_KEY_ERROR_TCP_CLEANUP_RBUF];
    socket_aggregated_data[3].ecall = res[NETDATA_KEY_ERROR_UDP_RECVMSG];
    socket_aggregated_data[4].ecall = res[NETDATA_KEY_ERROR_UDP_SENDMSG];

    socket_aggregated_data[0].bytes = res[NETDATA_KEY_BYTES_TCP_SENDMSG];
    socket_aggregated_data[1].bytes = res[NETDATA_KEY_BYTES_TCP_CLEANUP_RBUF];
    socket_aggregated_data[3].bytes = res[NETDATA_KEY_BYTES_UDP_RECVMSG];
    socket_aggregated_data[4].bytes = res[NETDATA_KEY_BYTES_UDP_SENDMSG];
}

/**
 * Fill publish apps when necessary.
 *
 * @param current_pid  the PID that I am updating
 * @param eb           the structure with data read from memory.
 */
void ebpf_socket_fill_publish_apps(uint32_t current_pid, ebpf_bandwidth_t *eb)
{
    ebpf_socket_publish_apps_t *curr = socket_bandwidth_curr[current_pid];
    ebpf_socket_publish_apps_t *prev = socket_bandwidth_prev[current_pid];
    if (!curr) {
        ebpf_socket_publish_apps_t *ptr = callocz(2, sizeof(ebpf_socket_publish_apps_t));
        curr = &ptr[0];
        socket_bandwidth_curr[current_pid] = curr;
        prev = &ptr[1];
        socket_bandwidth_prev[current_pid] = prev;
    } else {
        memcpy(prev, curr, sizeof(ebpf_socket_publish_apps_t));
    }

    curr->sent = eb->sent;
    curr->received = eb->received;

    ebpf_socket_update_apps_publish(curr, prev);
}

/**
 * Bandwidth accumulator.
 *
 * @param out the vector with the values to sum
 */
void ebpf_socket_bandwidth_accumulator(ebpf_bandwidth_t *out)
{
    int i, end = (running_on_kernel >= NETDATA_KERNEL_V4_15) ? ebpf_nprocs : 1;
    ebpf_bandwidth_t *total = &out[0];
    for (i = 1; i < end; i++) {
        ebpf_bandwidth_t *move = &out[i];
        total->sent += move->sent;
        total->received += move->received;
    }
}

/**
 *  Update the apps data reading information from the hash table
 */
static void ebpf_socket_update_apps_data()
{
    int fd = map_fd[0];
    ebpf_bandwidth_t *eb = bandwidth_vector;
    uint32_t key;
    struct pid_stat *pids = root_of_pids;
    while (pids) {
        key = pids->pid;

        if (bpf_map_lookup_elem(fd, &key, eb)) {
            pids = pids->next;
            continue;
        }

        ebpf_socket_bandwidth_accumulator(eb);

        ebpf_socket_fill_publish_apps(key, eb);

        if (eb[0].removed)
            bpf_map_delete_elem(fd, &key);

        pids = pids->next;
    }
}

/*****************************************************************
 *
 *  FUNCTIONS WITH THE MAIN LOOP
 *
 *****************************************************************/

/**
 * Main loop for this collector.
 *
 * @param step the number of microseconds used with heart beat
 * @param em   the structure with thread information
 */
static void socket_collector(usec_t step, ebpf_module_t *em)
{
    UNUSED(em);
    UNUSED(step);
    heartbeat_t hb;
    heartbeat_init(&hb);

    int socket_apps_enabled = ebpf_modules[EBPF_MODULE_SOCKET_IDX].apps_charts;
    int socket_global_enabled = ebpf_modules[EBPF_MODULE_SOCKET_IDX].global_charts;
    while (!close_ebpf_plugin) {
        pthread_mutex_lock(&collect_data_mutex);
        pthread_cond_wait(&collect_data_cond_var, &collect_data_mutex);

        if (socket_global_enabled)
            read_hash_global_tables();

        if (socket_apps_enabled)
            ebpf_socket_update_apps_data();

        pthread_mutex_lock(&lock);
        if (socket_global_enabled)
            ebpf_socket_send_data(em);

        if (socket_apps_enabled)
            ebpf_socket_send_apps_data(em, apps_groups_root_target);

        pthread_mutex_unlock(&collect_data_mutex);

        pthread_mutex_unlock(&lock);

        fflush(stdout);
    }
}

/*****************************************************************
 *
 *  FUNCTIONS TO CLOSE THE THREAD
 *
 *****************************************************************/

/**
 * Clean netowrk ports allocated during initializaion.
 *
 * @param ptr a pointer to the link list.
 */
static void clean_network_ports(ebpf_network_viewer_port_list_t *ptr)
{
    if (unlikely(!ptr))
        return;

    while (ptr) {
        ebpf_network_viewer_port_list_t *next = ptr->next;
        freez(ptr->value);
        freez(ptr);
        ptr = next;
    }
}

/**
 * Clean service names
 *
 * Clean the allocated link list that stores names.
 *
 * @param names the link list.
 */
static void clean_service_names(ebpf_network_viewer_dim_name_t *names)
{
    if (unlikely(!names))
        return;

    while (names) {
        ebpf_network_viewer_dim_name_t *next = names->next;
        freez(names->name);
        freez(names);
        names = next;
    }
}

/**
 * Clean hostnames
 *
 * @param hostnames the hostnames to clean
 */
static void clean_hostnames(ebpf_network_viewer_hostname_list_t *hostnames)
{
    if (unlikely(!hostnames))
        return;

    while (hostnames) {
        ebpf_network_viewer_hostname_list_t *next = hostnames->next;
        freez(hostnames->value);
        simple_pattern_free(hostnames->value_pattern);
        freez(hostnames);
        hostnames = next;
    }
}

/**
 * Clean up the main thread.
 *
 * @param ptr thread data.
 */
static void ebpf_socket_cleanup(void *ptr)
{
    UNUSED(ptr);
    freez(socket_aggregated_data);
    freez(socket_publish_aggregated);
    freez(socket_hash_values);

    freez(socket_data.map_fd);
    freez(socket_bandwidth_curr);
    freez(socket_bandwidth_prev);
    freez(bandwidth_vector);

    ebpf_modules[EBPF_MODULE_SOCKET_IDX].enabled = 0;

    clean_network_ports(network_viewer_opt.included_port);
    clean_network_ports(network_viewer_opt.excluded_port);
    clean_service_names(network_viewer_opt.names);
    clean_hostnames(network_viewer_opt.included_hostnames);
    clean_hostnames(network_viewer_opt.excluded_hostnames);
}

/*****************************************************************
 *
 *  FUNCTIONS TO START THREAD
 *
 *****************************************************************/

/**
 * Allocate vectors used with this thread.
 * We are not testing the return, because callocz does this and shutdown the software
 * case it was not possible to allocate.
 *
 * @param length is the length for the vectors used inside the collector.
 */
static void ebpf_socket_allocate_global_vectors(size_t length)
{
    socket_aggregated_data = callocz(length, sizeof(netdata_syscall_stat_t));
    socket_publish_aggregated = callocz(length, sizeof(netdata_publish_syscall_t));
    socket_hash_values = callocz(ebpf_nprocs, sizeof(netdata_idx_t));

    socket_bandwidth_curr = callocz((size_t)pid_max, sizeof(ebpf_socket_publish_apps_t *));
    socket_bandwidth_prev = callocz((size_t)pid_max, sizeof(ebpf_socket_publish_apps_t *));
    bandwidth_vector = callocz((size_t)ebpf_nprocs, sizeof(ebpf_bandwidth_t));
}

void change_socket_event()
{
    socket_probes[0].type = 'p';
    socket_probes[4].type = 'p';
    socket_probes[5].type = 'p';
    socket_probes[7].name = NULL;
}

/**
 * Set local function pointers, this function will never be compiled with static libraries
 */
static void set_local_pointers(ebpf_module_t *em)
{
    map_fd = socket_data.map_fd;

    if (em->mode == MODE_ENTRY) {
        change_socket_event();
    }
}

/*****************************************************************
 *
 *  EBPF SOCKET THREAD
 *
 *****************************************************************/

/**
 * Socket thread
 *
 * Thread used to generate socket charts.
 *
 * @param ptr a pointer to `struct ebpf_module`
 *
 * @return It always return NULL
 */
void *ebpf_socket_thread(void *ptr)
{
    netdata_thread_cleanup_push(ebpf_socket_cleanup, ptr);

    ebpf_module_t *em = (ebpf_module_t *)ptr;
    fill_ebpf_data(&socket_data);

    if (!em->enabled)
        goto endsocket;

    pthread_mutex_lock(&lock);

    ebpf_socket_allocate_global_vectors(NETDATA_MAX_SOCKET_VECTOR);

    if (ebpf_update_kernel(&socket_data)) {
        pthread_mutex_unlock(&lock);
        goto endsocket;
    }

    set_local_pointers(em);
    if (ebpf_load_program(
            ebpf_plugin_dir, em->thread_id, em->mode, kernel_string, em->thread_name, socket_data.map_fd)) {
        pthread_mutex_unlock(&lock);
        goto endsocket;
    }

    ebpf_global_labels(
        socket_aggregated_data, socket_publish_aggregated, socket_dimension_names, socket_id_names,
        NETDATA_MAX_SOCKET_VECTOR);

    ebpf_create_global_charts(em);

    pthread_mutex_unlock(&lock);

    socket_collector((usec_t)(em->update_time * USEC_PER_SEC), em);

endsocket:
    netdata_thread_cleanup_pop(1);
    return NULL;
}

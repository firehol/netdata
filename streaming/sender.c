// SPDX-License-Identifier: GPL-3.0-or-later

#include "rrdpush.h"

extern struct config stream_config;
extern int netdata_use_ssl_on_stream;
extern char *netdata_ssl_ca_path;
extern char *netdata_ssl_ca_file;

// Collector thread starting a transmission
void sender_start(struct sender_state *s) {
    netdata_mutex_lock(&s->mutex);
    buffer_flush(s->build);
}

// Collector thread finishing a transmission
void sender_commit(struct sender_state *s) {
    cbuffer_add_unsafe(s->host->sender->buffer, buffer_tostring(s->host->sender->build), s->host->sender->build->len);
    buffer_flush(s->build);
    netdata_mutex_unlock(&s->mutex);
}


static inline void rrdpush_sender_thread_close_socket(RRDHOST *host) {
    host->rrdpush_sender_connected = 0;

    if(host->rrdpush_sender_socket != -1) {
        close(host->rrdpush_sender_socket);
        host->rrdpush_sender_socket = -1;
    }
}

static inline void rrdpush_sender_add_host_variable_to_buffer_nolock(RRDHOST *host, RRDVAR *rv) {
    calculated_number *value = (calculated_number *)rv->value;

    buffer_sprintf(
            host->sender->build
            , "VARIABLE HOST %s = " CALCULATED_NUMBER_FORMAT "\n"
            , rv->name
            , *value
    );

    debug(D_STREAM, "RRDVAR pushed HOST VARIABLE %s = " CALCULATED_NUMBER_FORMAT, rv->name, *value);
}

void rrdpush_sender_send_this_host_variable_now(RRDHOST *host, RRDVAR *rv) {
    if(host->rrdpush_send_enabled && host->rrdpush_sender_spawn && host->rrdpush_sender_connected) {
        sender_start(host->sender);
        rrdpush_sender_add_host_variable_to_buffer_nolock(host, rv);
        sender_commit(host->sender);
    }
}


static int rrdpush_sender_thread_custom_host_variables_callback(void *rrdvar_ptr, void *host_ptr) {
    RRDVAR *rv = (RRDVAR *)rrdvar_ptr;
    RRDHOST *host = (RRDHOST *)host_ptr;

    if(unlikely(rv->options & RRDVAR_OPTION_CUSTOM_HOST_VAR && rv->type == RRDVAR_TYPE_CALCULATED)) {
        rrdpush_sender_add_host_variable_to_buffer_nolock(host, rv);

        // return 1, so that the traversal will return the number of variables sent
        return 1;
    }

    // returning a negative number will break the traversal
    return 0;
}

static void rrdpush_sender_thread_send_custom_host_variables(RRDHOST *host) {
    int ret = rrdvar_callback_for_all_host_variables(host, rrdpush_sender_thread_custom_host_variables_callback, host);
    (void)ret;

    debug(D_STREAM, "RRDVAR sent %d VARIABLES", ret);
}

// resets all the chart, so that their definitions
// will be resent to the central netdata
static void rrdpush_sender_thread_reset_all_charts(RRDHOST *host) {
    rrdhost_rdlock(host);

    RRDSET *st;
    rrdset_foreach_read(st, host) {
        rrdset_flag_clear(st, RRDSET_FLAG_UPSTREAM_EXPOSED);

        st->upstream_resync_time = 0;

        rrdset_rdlock(st);

        RRDDIM *rd;
        rrddim_foreach_read(rd, st)
            rd->exposed = 0;

        rrdset_unlock(st);
    }

    rrdhost_unlock(host);
}

static inline void rrdpush_sender_thread_data_flush(RRDHOST *host) {
    netdata_mutex_lock(&host->sender->mutex);

    size_t len = cbuffer_next_unsafe(host->sender->buffer, NULL);
    if (len)
        error("STREAM %s [send]: discarding %zu bytes of metrics already in the buffer.", host->hostname, len);

    cbuffer_remove_unsafe(host->sender->buffer, len);
    netdata_mutex_unlock(&host->sender->mutex);

    rrdpush_sender_thread_reset_all_charts(host);
    rrdpush_sender_thread_send_custom_host_variables(host);
}

static inline void rrdpush_set_flags_to_newest_stream(RRDHOST *host) {
    host->labels_flag |= LABEL_FLAG_UPDATE_STREAM;
    host->labels_flag &= ~LABEL_FLAG_STOP_STREAM;
}

void rrdpush_encode_variable(stream_encoded_t *se, RRDHOST *host)
{
    se->os_name = (host->system_info->host_os_name)?url_encode(host->system_info->host_os_name):"";
    se->os_id = (host->system_info->host_os_id)?url_encode(host->system_info->host_os_id):"";
    se->os_version = (host->system_info->host_os_version)?url_encode(host->system_info->host_os_version):"";
    se->kernel_name = (host->system_info->kernel_name)?url_encode(host->system_info->kernel_name):"";
    se->kernel_version = (host->system_info->kernel_version)?url_encode(host->system_info->kernel_version):"";
}

void rrdpush_clean_encoded(stream_encoded_t *se)
{
    if (se->os_name)
        freez(se->os_name);

    if (se->os_id)
        freez(se->os_id);

    if (se->os_version)
        freez(se->os_version);

    if (se->kernel_name)
        freez(se->kernel_name);

    if (se->kernel_version)
        freez(se->kernel_version);
}

static int rrdpush_sender_thread_connect_to_master(RRDHOST *host, int default_port, int timeout, size_t *reconnects_counter, char *connected_to, size_t connected_to_size) {
    struct timeval tv = {
            .tv_sec = timeout,
            .tv_usec = 0
    };

    // make sure the socket is closed
    rrdpush_sender_thread_close_socket(host);

    debug(D_STREAM, "STREAM: Attempting to connect...");
    info("STREAM %s [send to %s]: connecting...", host->hostname, host->rrdpush_send_destination);

    host->rrdpush_sender_socket = connect_to_one_of(
            host->rrdpush_send_destination
            , default_port
            , &tv
            , reconnects_counter
            , connected_to
            , connected_to_size
    );

    if(unlikely(host->rrdpush_sender_socket == -1)) {
        error("STREAM %s [send to %s]: failed to connect", host->hostname, host->rrdpush_send_destination);
        return 0;
    }

    info("STREAM %s [send to %s]: initializing communication...", host->hostname, connected_to);

#ifdef ENABLE_HTTPS
    if( netdata_client_ctx ){
        host->ssl.flags = NETDATA_SSL_START;
        if (!host->ssl.conn){
            host->ssl.conn = SSL_new(netdata_client_ctx);
            if(!host->ssl.conn){
                error("Failed to allocate SSL structure.");
                host->ssl.flags = NETDATA_SSL_NO_HANDSHAKE;
            }
        }
        else{
            SSL_clear(host->ssl.conn);
        }

        if (host->ssl.conn)
        {
            if (SSL_set_fd(host->ssl.conn, host->rrdpush_sender_socket) != 1) {
                error("Failed to set the socket to the SSL on socket fd %d.", host->rrdpush_sender_socket);
                host->ssl.flags = NETDATA_SSL_NO_HANDSHAKE;
            } else{
                host->ssl.flags = NETDATA_SSL_HANDSHAKE_COMPLETE;
            }
        }
    }
    else {
        host->ssl.flags = NETDATA_SSL_NO_HANDSHAKE;
    }
#endif

    /* TODO: During the implementation of #7265 switch the set of variables to HOST_* and CONTAINER_* if the
             version negotiation resulted in a high enough version.
    */
    stream_encoded_t se;
    rrdpush_encode_variable(&se, host);

    char http[HTTP_HEADER_SIZE + 1];
    int eol = snprintfz(http, HTTP_HEADER_SIZE,
            "STREAM key=%s&hostname=%s&registry_hostname=%s&machine_guid=%s&update_every=%d&os=%s&timezone=%s&tags=%s&ver=%u"
                 "&NETDATA_SYSTEM_OS_NAME=%s"
                 "&NETDATA_SYSTEM_OS_ID=%s"
                 "&NETDATA_SYSTEM_OS_ID_LIKE=%s"
                 "&NETDATA_SYSTEM_OS_VERSION=%s"
                 "&NETDATA_SYSTEM_OS_VERSION_ID=%s"
                 "&NETDATA_SYSTEM_OS_DETECTION=%s"
                 "&NETDATA_SYSTEM_KERNEL_NAME=%s"
                 "&NETDATA_SYSTEM_KERNEL_VERSION=%s"
                 "&NETDATA_SYSTEM_ARCHITECTURE=%s"
                 "&NETDATA_SYSTEM_VIRTUALIZATION=%s"
                 "&NETDATA_SYSTEM_VIRT_DETECTION=%s"
                 "&NETDATA_SYSTEM_CONTAINER=%s"
                 "&NETDATA_SYSTEM_CONTAINER_DETECTION=%s"
                 "&NETDATA_CONTAINER_OS_NAME=%s"
                 "&NETDATA_CONTAINER_OS_ID=%s"
                 "&NETDATA_CONTAINER_OS_ID_LIKE=%s"
                 "&NETDATA_CONTAINER_OS_VERSION=%s"
                 "&NETDATA_CONTAINER_OS_VERSION_ID=%s"
                 "&NETDATA_CONTAINER_OS_DETECTION=%s"
                 "&NETDATA_SYSTEM_CPU_LOGICAL_CPU_COUNT=%s"
                 "&NETDATA_SYSTEM_CPU_FREQ=%s"
                 "&NETDATA_SYSTEM_TOTAL_RAM=%s"
                 "&NETDATA_SYSTEM_TOTAL_DISK_SIZE=%s"
                 "&NETDATA_PROTOCOL_VERSION=%s"
                 " HTTP/1.1\r\n"
                 "User-Agent: %s/%s\r\n"
                 "Accept: */*\r\n\r\n"
                 , host->rrdpush_send_api_key
                 , host->hostname
                 , host->registry_hostname
                 , host->machine_guid
                 , default_rrd_update_every
                 , host->os
                 , host->timezone
                 , (host->tags) ? host->tags : ""
                 , STREAMING_PROTOCOL_CURRENT_VERSION
                 , se.os_name
                 , se.os_id
                 , (host->system_info->host_os_id_like) ? host->system_info->host_os_id_like : ""
                 , se.os_version
                 , (host->system_info->host_os_version_id) ? host->system_info->host_os_version_id : ""
                 , (host->system_info->host_os_detection) ? host->system_info->host_os_detection : ""
                 , se.kernel_name
                 , se.kernel_version
                 , (host->system_info->architecture) ? host->system_info->architecture : ""
                 , (host->system_info->virtualization) ? host->system_info->virtualization : ""
                 , (host->system_info->virt_detection) ? host->system_info->virt_detection : ""
                 , (host->system_info->container) ? host->system_info->container : ""
                 , (host->system_info->container_detection) ? host->system_info->container_detection : ""
                 , (host->system_info->container_os_name) ? host->system_info->container_os_name : ""
                 , (host->system_info->container_os_id) ? host->system_info->container_os_id : ""
                 , (host->system_info->container_os_id_like) ? host->system_info->container_os_id_like : ""
                 , (host->system_info->container_os_version) ? host->system_info->container_os_version : ""
                 , (host->system_info->container_os_version_id) ? host->system_info->container_os_version_id : ""
                 , (host->system_info->container_os_detection) ? host->system_info->container_os_detection : ""
                 , (host->system_info->host_cores) ? host->system_info->host_cores : ""
                 , (host->system_info->host_cpu_freq) ? host->system_info->host_cpu_freq : ""
                 , (host->system_info->host_ram_total) ? host->system_info->host_ram_total : ""
                 , (host->system_info->host_disk_space) ? host->system_info->host_disk_space : ""
                 , STREAMING_PROTOCOL_VERSION
                 , host->program_name
                 , host->program_version
                 );
    http[eol] = 0x00;
    rrdpush_clean_encoded(&se);

#ifdef ENABLE_HTTPS
    if (!host->ssl.flags) {
        ERR_clear_error();
        SSL_set_connect_state(host->ssl.conn);
        int err = SSL_connect(host->ssl.conn);
        if (err != 1){
            err = SSL_get_error(host->ssl.conn, err);
            error("SSL cannot connect with the server:  %s ",ERR_error_string((long)SSL_get_error(host->ssl.conn,err),NULL));
            if (netdata_use_ssl_on_stream == NETDATA_SSL_FORCE) {
                rrdpush_sender_thread_close_socket(host);
                return 0;
            }else {
                host->ssl.flags = NETDATA_SSL_NO_HANDSHAKE;
            }
        }
        else {
            if (netdata_use_ssl_on_stream == NETDATA_SSL_FORCE) {
                if (netdata_validate_server == NETDATA_SSL_VALID_CERTIFICATE) {
                    if ( security_test_certificate(host->ssl.conn)) {
                        error("Closing the stream connection, because the server SSL certificate is not valid.");
                        rrdpush_sender_thread_close_socket(host);
                        return 0;
                    }
                }
            }
        }
    }
    if(send_timeout(&host->ssl,host->rrdpush_sender_socket, http, strlen(http), 0, timeout) == -1) {
#else
    if(send_timeout(host->rrdpush_sender_socket, http, strlen(http), 0, timeout) == -1) {
#endif
        error("STREAM %s [send to %s]: failed to send HTTP header to remote netdata.", host->hostname, connected_to);
        rrdpush_sender_thread_close_socket(host);
        return 0;
    }

    info("STREAM %s [send to %s]: waiting response from remote netdata...", host->hostname, connected_to);

    ssize_t received;
#ifdef ENABLE_HTTPS
    received = recv_timeout(&host->ssl,host->rrdpush_sender_socket, http, HTTP_HEADER_SIZE, 0, timeout);
    if(received == -1) {
#else
    received = recv_timeout(host->rrdpush_sender_socket, http, HTTP_HEADER_SIZE, 0, timeout);
    if(received == -1) {
#endif
        error("STREAM %s [send to %s]: remote netdata does not respond.", host->hostname, connected_to);
        rrdpush_sender_thread_close_socket(host);
        return 0;
    }

    http[received] = '\0';
    int answer = -1;
    char *version_start = strchr(http, '=');
    uint32_t version;
    if(version_start) {
        version_start++;
        version = (uint32_t)strtol(version_start, NULL, 10);
        answer = memcmp(http, START_STREAMING_PROMPT_VN, (size_t)(version_start - http));
        if(!answer) {
            rrdpush_set_flags_to_newest_stream(host);
        }
    } else {
        answer = memcmp(http, START_STREAMING_PROMPT_V2, strlen(START_STREAMING_PROMPT_V2));
        if(!answer) {
            version = 1;
            rrdpush_set_flags_to_newest_stream(host);
        }
        else {
            answer = memcmp(http, START_STREAMING_PROMPT, strlen(START_STREAMING_PROMPT));
            if(!answer) {
                version = 0;
                host->labels_flag |= LABEL_FLAG_STOP_STREAM;
                host->labels_flag &= ~LABEL_FLAG_UPDATE_STREAM;
            }
        }
    }

    if(answer != 0) {
        error("STREAM %s [send to %s]: server is not replying properly (is it a netdata?).", host->hostname, connected_to);
        rrdpush_sender_thread_close_socket(host);
        return 0;
    }
    host->stream_version = version;

    info("STREAM %s [send to %s]: established communication with a master using protocol version %u - ready to send metrics..."
         , host->hostname
         , connected_to
         , version);

    if(sock_setnonblock(host->rrdpush_sender_socket) < 0)
        error("STREAM %s [send to %s]: cannot set non-blocking mode for socket.", host->hostname, connected_to);

    if(sock_enlarge_out(host->rrdpush_sender_socket) < 0)
        error("STREAM %s [send to %s]: cannot enlarge the socket buffer.", host->hostname, connected_to);

    debug(D_STREAM, "STREAM: Connected on fd %d...", host->rrdpush_sender_socket);

    return 1;
}

static void attempt_to_connect(struct sender_state *state)
{
    state->send_attempts = 0;

    if(state->not_connected_loops == 0 && state->sent_bytes_on_this_connection > 0) {
        // fast re-connection on first disconnect
        sleep_usec(USEC_PER_MS * 500); // milliseconds
    }
    else {
        // slow re-connection on repeating errors
        sleep_usec(USEC_PER_SEC * state->reconnect_delay); // seconds
    }

    if(rrdpush_sender_thread_connect_to_master(state->host, state->default_port, state->timeout,
                                     &state->reconnects_counter, state->connected_to, sizeof(state->connected_to)-1)) {
        state->last_sent_t = now_monotonic_sec();

        // reset the buffer, to properly send charts and metrics
        rrdpush_sender_thread_data_flush(state->host);

        // send from the beginning
        state->begin = 0;

        // make sure the next reconnection will be immediate
        state->not_connected_loops = 0;

        // reset the bytes we have sent for this session
        state->sent_bytes_on_this_connection = 0;

        // let the data collection threads know we are ready
        state->host->rrdpush_sender_connected = 1;
    }
    else {
        // increase the failed connections counter
        state->not_connected_loops++;

        // reset the number of bytes sent
        state->sent_bytes_on_this_connection = 0;
    }
}


// TODO-GAPS Removed buffer lock because does not touch the buffer - check
static void rrdpush_sender_thread_cleanup_callback(void *ptr) {
    RRDHOST *host = (RRDHOST *)ptr;

    rrdhost_wrlock(host);

    info("STREAM %s [send]: sending thread cleans up...", host->hostname);

    rrdpush_sender_thread_close_socket(host);

    // close the pipe
    if(host->rrdpush_sender_pipe[PIPE_READ] != -1) {
        close(host->rrdpush_sender_pipe[PIPE_READ]);
        host->rrdpush_sender_pipe[PIPE_READ] = -1;
    }

    if(host->rrdpush_sender_pipe[PIPE_WRITE] != -1) {
        close(host->rrdpush_sender_pipe[PIPE_WRITE]);
        host->rrdpush_sender_pipe[PIPE_WRITE] = -1;
    }

    if(!host->rrdpush_sender_join) {
        info("STREAM %s [send]: sending thread detaches itself.", host->hostname);
        netdata_thread_detach(netdata_thread_self());
    }

    host->rrdpush_sender_spawn = 0;

    info("STREAM %s [send]: sending thread now exits.", host->hostname);

    rrdhost_unlock(host);
}

void sender_init(struct sender_state *s, RRDHOST *parent) {
    memset(s, 0, sizeof(*s));
    s->host = parent;
    s->buffer = cbuffer_new(1024, 1024*1024);
    s->build = buffer_create(1);
}

void *rrdpush_sender_thread(void *ptr) {
    struct sender_state *s = ptr;
    s->task_id = gettid();

    if(!s->host->rrdpush_send_enabled || !s->host->rrdpush_send_destination ||
       !*s->host->rrdpush_send_destination || !s->host->rrdpush_send_api_key ||
       !*s->host->rrdpush_send_api_key) {
        error("STREAM %s [send]: thread created (task id %d), but host has streaming disabled.",
              s->host->hostname, s->task_id);
        return NULL;
    }

#ifdef ENABLE_HTTPS
    if (netdata_use_ssl_on_stream & NETDATA_SSL_FORCE ){
        security_start_ssl(NETDATA_SSL_CONTEXT_STREAMING);
        security_location_for_context(netdata_client_ctx, netdata_ssl_ca_file, netdata_ssl_ca_path);
    }
#endif

    info("STREAM %s [send]: thread created (task id %d)", s->host->hostname, s->task_id);

    s->timeout = (int)appconfig_get_number(&stream_config, CONFIG_SECTION_STREAM, "timeout seconds", 60);
    s->default_port = (int)appconfig_get_number(&stream_config, CONFIG_SECTION_STREAM, "default port", 19999);
    s->max_size = (size_t)appconfig_get_number(&stream_config, CONFIG_SECTION_STREAM, "buffer size bytes",
                                                  1024 * 1024);
    s->reconnect_delay = (unsigned int)appconfig_get_number(&stream_config, CONFIG_SECTION_STREAM, "reconnect delay seconds", 5);
    //remote_clock_resync_iterations = (unsigned int)appconfig_get_number(&stream_config, CONFIG_SECTION_STREAM, "initial clock resync iterations", remote_clock_resync_iterations); TODO: REMOVING FOR SLEW / GAPFILLING

    // TODO-GAPS Integreate with new state
    // initialize rrdpush globals
    s->host->rrdpush_sender_connected = 0;
    if(pipe(s->host->rrdpush_sender_pipe) == -1) {
        error("STREAM %s [send]: cannot create required pipe. DISABLING STREAMING THREAD", s->host->hostname);
        return NULL;
    }


    struct pollfd fds[2], *ifd, *ofd;       // TODO: state or here??
    nfds_t fdmax;

    ifd = &fds[0];
    ofd = &fds[1];


    netdata_thread_cleanup_push(rrdpush_sender_thread_cleanup_callback, s->host);

    for(; s->host->rrdpush_send_enabled && !netdata_exit ;) {
        // check for outstanding cancellation requests
        netdata_thread_testcancel();

        // connection logic
        if(unlikely(s->host->rrdpush_sender_socket == -1)) {
            attempt_to_connect(s);
            continue;
        }
        if(unlikely(now_monotonic_sec() - s->last_sent_t > s->timeout)) {
            error("STREAM %s [send to %s]: could not send metrics for %d seconds - closing connection - we have sent %zu bytes on this connection via %zu send attempts.", s->host->hostname, s->connected_to, s->timeout, s->sent_bytes_on_this_connection, s->send_attempts);
            rrdpush_sender_thread_close_socket(s->host);
            continue;           // TODO: check, but if socket is closed then fallthrough makes no sense...
        }

        // Wait until buffer opens in the socket or a rrdset_done_push wakes us
        ifd->fd = s->host->rrdpush_sender_pipe[PIPE_READ];
        ifd->events = POLLIN;
        ifd->revents = 0;

        ofd->fd = s->host->rrdpush_sender_socket;
        ofd->revents = 0;
        char *chunk;
        size_t outstanding = cbuffer_next_unsafe(s->host->sender->buffer, &chunk);
        if(ofd->fd != -1 && outstanding) {
            debug(D_STREAM, "STREAM: Requesting data output on streaming socket %d...", ofd->fd);
            ofd->events = POLLOUT;
            fdmax = 2;
            s->send_attempts++;
        }
        else {
            debug(D_STREAM, "STREAM: Not requesting data output on streaming socket %d (nothing to send now)...", ofd->fd);
            ofd->events = 0;
            fdmax = 1;
        }

        debug(D_STREAM, "STREAM: Waiting for poll() events (current buffer chunk %zu bytes)...", outstanding);
        if(unlikely(netdata_exit)) break;
        int retval = poll(fds, fdmax, 1000);
        if(unlikely(netdata_exit)) break;


        if(unlikely(retval == -1)) {
            debug(D_STREAM, "STREAM: poll() failed (current buffer chunk %zu bytes)...", outstanding);

            if(errno == EAGAIN || errno == EINTR) {
                debug(D_STREAM, "STREAM: poll() failed with EAGAIN or EINTR...");
            }
            else {
                error("STREAM %s [send to %s]: failed to poll(). Closing socket.", s->host->hostname, s->connected_to);
                rrdpush_sender_thread_close_socket(s->host);
            }

            continue;
        }

        if(likely(retval)) {
            if (ifd->revents & POLLIN || ifd->revents & POLLPRI) {
                debug(D_STREAM, "STREAM: Data added to send buffer (current buffer chunk %zu bytes)...", outstanding);

                char buffer[1000 + 1];
                if (read(s->host->rrdpush_sender_pipe[PIPE_READ], buffer, 1000) == -1)
                    error("STREAM %s [send to %s]: cannot read from internal pipe.", s->host->hostname,
                          s->connected_to);
            }

            if (ofd->revents & POLLOUT) {
                rrdpush_send_labels(s->host);

                if (outstanding) {
                    struct circular_buffer *cb = s->host->sender->buffer;
                    debug(D_STREAM, "STREAM: Sending data. Buffer r=%zu w=%zu s=%zu, next chunk=%zu", cb->read, cb->write, cb->size, outstanding);

                    // BEGIN RRDPUSH LOCKED SESSION

                    // during this session, data collectors
                    // will not be able to append data to our buffer
                    // but the socket is in non-blocking mode
                    // so, we will not block at send()

                    netdata_thread_disable_cancelability();

                    debug(D_STREAM, "STREAM: Getting exclusive lock on host...");
                    netdata_mutex_lock(&s->host->sender->mutex);

                    debug(D_STREAM, "STREAM: Sending data chunk=%zu...", outstanding);
                    ssize_t ret;
#ifdef ENABLE_HTTPS
                    SSL *conn = s->host->ssl.conn ;
                    if(conn && !s->host->ssl.flags) {
                        ret = SSL_write(conn, chunk, outstanding);
                    } else {
                        ret = send(s->host->rrdpush_sender_socket, chunk, outstanding, MSG_DONTWAIT);
                    }
#else
                    ret = send(s->host->rrdpush_sender_socket, chunk, outstanding, MSG_DONTWAIT);
#endif
                    if (unlikely(ret == -1)) {
                        if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK) {
                            debug(D_STREAM, "STREAM: Send failed - closing socket...");
                            error("STREAM %s [send to %s]: failed to send metrics - closing connection - we have sent %zu bytes on this connection.", s->host->hostname, s->connected_to, s->sent_bytes_on_this_connection);
                            rrdpush_sender_thread_close_socket(s->host);
                        }
                        else {
                            debug(D_STREAM, "STREAM: Send failed - will retry...");
                        }
                    }
                    else if (likely(ret > 0)) {
                        cbuffer_remove_unsafe(s->host->sender->buffer, ret);
                        // DEBUG - dump the string to see it
                        //char c = host->rrdpush_sender_buffer->buffer[begin + ret];
                        //host->rrdpush_sender_buffer->buffer[begin + ret] = '\0';
                        //debug(D_STREAM, "STREAM: sent from %zu to %zd:\n%s\n", begin, ret, &host->rrdpush_sender_buffer->buffer[begin]);
                        //host->rrdpush_sender_buffer->buffer[begin + ret] = c;

                        s->sent_bytes_on_this_connection += ret;
                        s->sent_bytes += ret;

                        debug(D_STREAM, "STREAM: Sent %zd bytes", ret);
                        s->last_sent_t = now_monotonic_sec();
                    }
                    else {
                        debug(D_STREAM, "STREAM: send() returned 0 -> no error but no transmission");
                    }

                    debug(D_STREAM, "STREAM: Releasing exclusive lock on host...");
                    netdata_mutex_unlock(&s->host->sender->mutex);

                    netdata_thread_enable_cancelability();

                    // END RRDPUSH LOCKED SESSION
                }
                else {
                    debug(D_STREAM, "STREAM: we have sent the entire buffer, but we received POLLOUT...");
                }
            }

            if(s->host->rrdpush_sender_socket != -1) {
                char *error = NULL;

                if (unlikely(ofd->revents & POLLERR))
                    error = "socket reports errors (POLLERR)";

                else if (unlikely(ofd->revents & POLLHUP))
                    error = "connection closed by remote end (POLLHUP)";

                else if (unlikely(ofd->revents & POLLNVAL))
                    error = "connection is invalid (POLLNVAL)";

                if(unlikely(error)) {
                    debug(D_STREAM, "STREAM: %s - closing socket...", error);
                    error("STREAM %s [send to %s]: %s - reopening socket - we have sent %zu bytes on this connection.", s->host->hostname, s->connected_to, error, s->sent_bytes_on_this_connection);
                    rrdpush_sender_thread_close_socket(s->host);
                }
            }
        }
        else {
            debug(D_STREAM, "STREAM: poll() timed out.");
        }

        // TODO-GAPS Overflow will be detected when collector fails to write to buffer, check flag in this
        //           loop and tear down for restart
        /*// protection from overflow
        if (buffer_strlen(s->host->rrdpush_sender_buffer) > s->max_size) {
            debug(D_STREAM, "STREAM: Buffer is too big (%zu bytes), bigger than the max (%zu) - flushing it...", buffer_strlen(s->host->rrdpush_sender_buffer), s->max_size);
            errno = 0;
            error("STREAM %s [send to %s]: too many data pending - buffer is %zu bytes long, %zu unsent - we have sent %zu bytes in total, %zu on this connection. Closing connection to flush the data.", s->host->hostname, s->connected_to, s->host->rrdpush_sender_buffer->len, s->host->rrdpush_sender_buffer->len - s->begin, s->sent_bytes, s->sent_bytes_on_this_connection);
            rrdpush_sender_thread_close_socket(s->host);
        }*/
    }

    netdata_thread_cleanup_pop(1);
    return NULL;
}

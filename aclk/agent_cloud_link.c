// SPDX-License-Identifier: GPL-3.0-or-later

#include "libnetdata/libnetdata.h"
#include "../daemon/common.h"
#include "agent_cloud_link.h"

// Read from the config file -- new section [agent_cloud_link]
// Defaults are supplied
int aclk_recv_maximum = 0; // default 20
int aclk_send_maximum = 0; // default 20

int aclk_port = 0;          // default 1883
char *aclk_hostname = NULL; //default localhost
int aclk_subscribed = 0;

int aclk_metadata_submitted = 0;
int cmdpause = 0; // Used to pause query processing

BUFFER *aclk_buffer = NULL;

char *send_http_request(char *host, char *port, char *url, BUFFER *b)
{
    struct timeval timeout = { .tv_sec = 30, .tv_usec = 0 };
    buffer_flush(b);
    buffer_sprintf(
        b,
        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: plain/text\r\nAccept-Language: en-us\r\nUser-Agent: Netdata/rocks\r\n\r\n",
        url, host);
    int sock = connect_to_this_ip46(IPPROTO_TCP, SOCK_STREAM, host, 0, "443", &timeout);

    if (unlikely(!sock)) {
        error("Handshake failed");
        return NULL;
    }

    SSL_CTX *ctx = security_initialize_openssl_client();
    // Certificate chain: not updating the stores - do we need private CA roots?
    // Calls to SSL_CTX_load_verify_locations would go here.
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    int err = SSL_connect(ssl);
    SSL_write(ssl, b->buffer, b->len); // Timeout options?
    int bytes_read = SSL_read(ssl, b->buffer, b->len);
    SSL_shutdown(ssl);
    close(sock);
}

// Set when we have connection up and running from the connection callback
int aclk_connection_initialized = 0;

static netdata_mutex_t aclk_mutex = NETDATA_MUTEX_INITIALIZER;
static netdata_mutex_t query_mutex = NETDATA_MUTEX_INITIALIZER;

#define ACLK_LOCK netdata_mutex_lock(&aclk_mutex)
#define ACLK_UNLOCK netdata_mutex_unlock(&aclk_mutex)

#define QUERY_LOCK netdata_mutex_lock(&query_mutex)
#define QUERY_UNLOCK netdata_mutex_unlock(&query_mutex)

struct aclk_query {
    time_t created;
    time_t run_after; // Delay run until after this time
    int repeat_every;
    int repeat_count;
    bool deleted;
    char *token;
    char *query;
    struct aclk_query *next;
};

struct aclk_query_queue {
    struct aclk_query *aclk_query_head;
    struct aclk_query *aclk_query_tail;
    u_int64_t count;
} aclk_queue = { .aclk_query_head = NULL, .aclk_query_tail = NULL, .count = 0 };

/*
 * Free a query structure when done
 */

void aclk_query_free(struct aclk_query *this_query)
{
    if (unlikely(!this_query))
        return;

    freez(this_query->token);
    freez(this_query->query);
    freez(this_query);
    return;
}

// Returns the entry after which we need to create a new entry to run at the specified time
// If NULL is returned we need to add to HEAD
// Called with locked entries

struct aclk_query *aclk_query_find_position(time_t time_to_run)
{
    struct aclk_query *tmp_query, *last_query;

    last_query = NULL;
    tmp_query = aclk_queue.aclk_query_head;

    while (tmp_query) {
        if (tmp_query->run_after > time_to_run)
            return last_query;
        last_query = tmp_query;
        tmp_query = tmp_query->next;
    }
    return last_query;
}

struct aclk_query *aclk_query_find(char *token, char *query)
{
    struct aclk_query *tmp_query;

    tmp_query = aclk_queue.aclk_query_head;

    while (tmp_query) {
        if (!tmp_query->deleted && (strcmp(tmp_query->token, token) == 0) && (strcmp(tmp_query->query, query) == 0))
            return tmp_query;

        tmp_query = tmp_query->next;
    }
    return NULL;
}

/*
 * Add a query to execute, the result will be send to the specified topic (token)
 */

int aclk_queue_query(char *token, char *query, int run_after, int repeat_every, int repeat_count)
{
    struct aclk_query *new_query, *tmp_query, *last_query;

    run_after = now_realtime_sec() + run_after;

    QUERY_LOCK;
    tmp_query = aclk_query_find(token, query);
    if (unlikely(tmp_query)) {
        //tmp_query->run_after += run_after;
        if (tmp_query->run_after == run_after) {
            info("Query %s:%s exists with same scheduled time", token, query);
            QUERY_UNLOCK;
            return 0;
        }
        info("Query %s:%s exists, deleting and schedule later", token, query);
        tmp_query->deleted = 1;
        //QUERY_UNLOCK;
        //return 0;
    }

    new_query = callocz(1, sizeof(struct aclk_query));
    new_query->token = strdupz(token);
    new_query->query = strdupz(query);
    new_query->next = NULL;
    new_query->created = now_realtime_sec();
    new_query->run_after = run_after;

    info("Added query (%s) (%s)", token, query);

    //run_after = now_realtime_sec() + run_after;

    tmp_query = aclk_query_find_position(run_after);

    if (tmp_query) {
        new_query->next = tmp_query->next;
        tmp_query->next = new_query;
        if (tmp_query == aclk_queue.aclk_query_tail)
            aclk_queue.aclk_query_tail = new_query;
        aclk_queue.count++;
        QUERY_UNLOCK;
        return 0;
    }

    new_query->next = aclk_queue.aclk_query_head;
    aclk_queue.aclk_query_head = new_query;
    aclk_queue.count++;

    QUERY_UNLOCK;
    return 0;

    if (likely(aclk_queue.aclk_query_tail)) {
        aclk_queue.aclk_query_tail->next = new_query;
        aclk_queue.aclk_query_tail = new_query;
        aclk_queue.count++;
        QUERY_UNLOCK;
        return 0;
    }

    if (likely(!aclk_queue.aclk_query_head)) {
        aclk_queue.aclk_query_head = new_query;
        aclk_queue.aclk_query_tail = new_query;
        aclk_queue.count++;
        QUERY_UNLOCK;
        return 0;
    }
    QUERY_UNLOCK;
    return 0;

    //    last_query = NULL;
    //    tmp_query = aclk_queue.aclk_query_head;
    //
    //    while (tmp_query) {
    //        if (run_after < tmp_query->run_after) {
    //            new_query->next = tmp_query;
    //            if (last_query == NULL)
    //                aclk_queue.aclk_query_head = new_query;
    //            else
    //                last_query->next = new_query;
    //            aclk_queue.count++;
    //            QUERY_UNLOCK;
    //            return 0;
    //        }
    //        last_query = tmp_query;
    //        tmp_query = tmp_query->next;
    //    }
}

/*
 * Get the next query to process - NULL if nothing there
 * The caller needs to free memory by calling aclk_query_free()
 *
 *      token
 *      query
 *      The structure itself
 *
 */
struct aclk_query *aclk_queue_pop()
{
    struct aclk_query *this_query;

    QUERY_LOCK;

    if (likely(!aclk_queue.aclk_query_head)) {
        QUERY_UNLOCK;
        return NULL;
    }

    this_query = aclk_queue.aclk_query_head;

    if (this_query->run_after > now_realtime_sec()) {
        info("Query %s will run in %ld seconds", this_query->query, this_query->run_after - now_realtime_sec());
        QUERY_UNLOCK;
        return NULL;
    }

    aclk_queue.count--;
    aclk_queue.aclk_query_head = aclk_queue.aclk_query_head->next;

    if (likely(!aclk_queue.aclk_query_head)) {
        aclk_queue.aclk_query_tail = NULL;
    }

    QUERY_UNLOCK;
    return this_query;
}

// This will give the base topic that the agent will publish messages.
// subtopics will be sent under the base topic e.g.  base_topic/subtopic
// This is called by aclk_init(), to compute the base topic once and have
// it stored internally.
// Need to check if additional logic should be added to make sure that there
// is enough information to determine the base topic at init time

// TODO: Locking may be needed, depends on the calculation of the base topic and also if we need to switch
// that on the fly

char *get_publish_base_topic(PUBLISH_TOPIC_ACTION action)
{
    static char *topic = NULL;

    if (unlikely(!is_agent_claimed()))
        return NULL;

    ACLK_LOCK;

    if (unlikely(action == PUBLICH_TOPIC_FREE)) {
        if (likely(topic)) {
            freez(topic);
            topic = NULL;
        }

        ACLK_UNLOCK;

        return NULL;
    }

    if (unlikely(action == PUBLICH_TOPIC_REBUILD)) {
        ACLK_UNLOCK;
        get_publish_base_topic(PUBLICH_TOPIC_FREE);
        return get_publish_base_topic(PUBLICH_TOPIC_GET);
    }

    if (unlikely(!topic)) {
        char tmp_topic[ACLK_MAX_TOPIC + 1];

        sprintf(tmp_topic, ACLK_TOPIC_STRUCTURE, is_agent_claimed());
        topic = strdupz(tmp_topic);
    }

    ACLK_UNLOCK;
    return topic;
}

// Wait for ACLK connection to be established
int aclk_wait_for_initialization()
{
    if (unlikely(!aclk_connection_initialized)) {
        time_t now = now_realtime_sec();

        while (!aclk_connection_initialized && (now_realtime_sec() - now) < ACLK_INITIALIZATION_WAIT) {
            sleep_usec(USEC_PER_SEC * ACLK_INITIALIZATION_SLEEP_WAIT);
            _link_event_loop(0);
        }

        if (unlikely(!aclk_connection_initialized)) {
            error("ACLK connection cannot be established");
            return 1;
        }
    }
    return 0;
}

/*
 * This function will fetch the next pending command and process it
 *
 */
int aclk_process_query()
{
    struct aclk_query *this_query;
    static time_t last_beat = 0;
    static u_int64_t query_count = 0;
    int rc;
    time_t current_beat;

    if (unlikely(cmdpause))
        return 0;

    current_beat = now_realtime_sec();

    //if (unlikely(current_beat - last_beat < ACLK_HEARTBEAT_INTERVAL && last_beat > 0)) {
    //    return 0;
    // }

    //last_beat = current_beat;

    if (!aclk_connection_initialized)
        return 0;

    this_query = aclk_queue_pop();
    if (likely(!this_query)) {
        //info("No pending queries");
        return 0;
    }

    if (this_query->deleted) {
        info("Garbage collect query %s:%s", this_query->token, this_query->query);
        aclk_query_free(this_query);
        return 1;
    }

    query_count++;
    info(
        "Processsing query #%d  (%s) (%s) queued for %d seconds", query_count, this_query->token, this_query->query,
        now_realtime_sec() - this_query->created);

    if (strncmp((char *)this_query->query, "data:", 5) == 0) {
        struct web_client *w = (struct web_client *)malloc(sizeof(struct web_client));
        memset(w, 0, sizeof(struct web_client));
        w->response.data = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);
        w->response.header = buffer_create(NETDATA_WEB_RESPONSE_HEADER_SIZE);
        w->response.header_output = buffer_create(NETDATA_WEB_RESPONSE_HEADER_SIZE);
        strcpy(w->origin, "*"); // Simulate web_client_create_on_fd()
        w->cookie1[0] = 0;      // Simulate web_client_create_on_fd()
        w->cookie2[0] = 0;      // Simulate web_client_create_on_fd()
        w->acl = 0x1f;

        error_log_limit_unlimited();
        web_client_api_request_v1_data(localhost, w, this_query->query + 5);
        //info("RESP: (%d) %s", w->response.data->len, w->response.data->buffer);
        aclk_send_message("data", w->response.data->buffer);
        error_log_limit_reset();

        buffer_free(w->response.data);
        buffer_free(w->response.header);
        buffer_free(w->response.header_output);
        free(w);
    }

    if (strcmp((char *)this_query->token, "_chart") == 0) {
        aclk_send_single_chart(this_query->query);
    }

    aclk_query_free(this_query);

    return 1;
}

/*
 * Process all pending queries
 *
 */

int aclk_process_queries()
{
    int rc;

    if (unlikely(!aclk_metadata_submitted)) {
        aclk_send_metadata_info();
        aclk_metadata_submitted = 1;
    }

    // Return if no queries pendning
    if (likely(!aclk_queue.count))
        return 1;

    info("Processing %ld queries", aclk_queue.count);

    while (aclk_process_query()) {
        rc = _link_event_loop(0);
    };

    return 1;
}

// Thread cleanup
static void aclk_main_cleanup(void *ptr)
{
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;
    static_thread->enabled = NETDATA_MAIN_THREAD_EXITING;

    info("cleaning up...");

    static_thread->enabled = NETDATA_MAIN_THREAD_EXITED;
}

/**
 * Main agent cloud link thread
 *
 * This thread will simply call the main event loop that handles
 * pending requests - both inbound and outbound
 *
 * @param ptr is a pointer to the netdata_static_thread structure.
 *
 * @return It always returns NULL
 */
void *aclk_main(void *ptr)
{
    netdata_thread_cleanup_push(aclk_main_cleanup, ptr);

    if (unlikely(!aclk_buffer))
        aclk_buffer = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);

    assert(aclk_buffer != NULL);

    while (!netdata_exit) {
        int rc;

        // TODO: This may change when we have enough info from the claiming itself to avoid wasting 60 seconds
        // TODO: Handle the unclaim command as well -- we may need to shutdown the connection
        if (likely(!is_agent_claimed())) {
            sleep_usec(USEC_PER_SEC * 60);
            info("Checking agent claiming status");
            continue;
        }

        if (unlikely(!aclk_connection_initialized)) {
            info("Initializing connection");
            send_http_request(aclk_hostname, "443", "/auth/challenge?id=blah", aclk_buffer);
            if (unlikely(aclk_init(ACLK_INIT))) {
                // TODO: TBD how to handle. We are claimed and we cant init the connection. For now keep trying.
                sleep_usec(USEC_PER_SEC * 60);
                continue;
            } else {
                sleep_usec(USEC_PER_SEC * 1);
            }
            _link_event_loop(ACLK_LOOP_TIMEOUT * 1000);
            continue;
        }

        if (unlikely(!aclk_subscribed)) {
            aclk_subscribed = !aclk_subscribe(ACLK_COMMAND_TOPIC, 2);
        }

        //aclk_heartbeat();

        if (likely(aclk_connection_initialized))
            aclk_process_queries();

        // Call the loop to handle inbound and outbound messages
        rc = _link_event_loop(ACLK_LOOP_TIMEOUT * 1000);

    } // forever
    aclk_shutdown();

    netdata_thread_cleanup_pop(1);
    return NULL;
}

/*
 * Send a message to the cloud, using a base topic and sib_topic
 * The final topic will be in the form <base_topic>/<sub_topic>
 * If base_topic is missing then the global_base_topic will be used (if available)
 *
 */
int aclk_send_message(char *sub_topic, char *message)
{
    int rc;
    static int skip_due_to_shutdown = 0;
    static char *global_base_topic = NULL;
    char topic[ACLK_MAX_TOPIC + 1];
    char *final_topic;

    if (!aclk_connection_initialized)
        return 0;

    if (unlikely(netdata_exit)) {
        if (unlikely(!aclk_connection_initialized))
            return 1;

        ++skip_due_to_shutdown;
        if (unlikely(!(skip_due_to_shutdown % 100)))
            info("%d messages not sent -- shutdown in progress", skip_due_to_shutdown);
        return 1;
    }

    if (unlikely(!message))
        return 0;

    if (unlikely(aclk_wait_for_initialization()))
        return 1;

    if (unlikely(!global_base_topic))
        global_base_topic = GET_PUBLISH_BASE_TOPIC;

    //if (unlikely(!base_topic)) {
    if (unlikely(!global_base_topic))
        final_topic = sub_topic;
    else {
        snprintfz(topic, ACLK_MAX_TOPIC, "%s/%s", global_base_topic, sub_topic);
        final_topic = topic;
    }

    ACLK_LOCK;
    rc = _link_send_message(final_topic, message);
    ACLK_UNLOCK;

    // TODO: Add better handling -- error will flood the logfile here
    if (unlikely(rc))
        error("Failed to send message, error code %d (%s)", rc, _link_strerror(rc));

    return rc;
}

/*
 * Subscribe to a topic in the cloud
 * The final subscription will be in the form
 * /agent/claim_id/<sub_topic>
 */
int aclk_subscribe(char *sub_topic, int qos)
{
    int rc;
    static char *global_base_topic = NULL;
    char topic[ACLK_MAX_TOPIC + 1];
    char *final_topic;

    if (!aclk_connection_initialized)
        return 0;

    if (unlikely(netdata_exit)) {
        return 1;
    }

    if (unlikely(aclk_wait_for_initialization()))
        return 1;

    if (unlikely(!global_base_topic))
        global_base_topic = GET_PUBLISH_BASE_TOPIC;

    if (unlikely(!global_base_topic))
        final_topic = sub_topic;
    else {
        snprintfz(topic, ACLK_MAX_TOPIC, "%s/%s", global_base_topic, sub_topic);
        final_topic = topic;
    }

    //info("Sending message: (%s) - (%s)", final_topic, message);
    ACLK_LOCK;
    rc = _link_subscribe(final_topic, qos);
    ACLK_UNLOCK;

    // TODO: Add better handling -- error will flood the logfile here
    if (unlikely(rc))
        error("Failed to send message, error code %d (%s)", rc, _link_strerror(rc));

    return rc;
}

// This is called from a callback when the link goes up
void aclk_connect(void *ptr)
{
    info("Connection detected");
    return;
}

// This is called from a callback when the link goes down
void aclk_disconnect(void *ptr)
{
    info("Disconnect detected");
    aclk_subscribed = 0;
    aclk_metadata_submitted = 0;
    return;
}

void aclk_shutdown()
{
    int rc;

    info("Shutdown initiated");
    aclk_connection_initialized = 0;
    _link_shutdown();
    info("Shutdown complete");
}

int aclk_init(ACLK_INIT_ACTION action)
{
    static int init = 0;
    int rc;

    // Check if we should do reinit
    if (unlikely(action == ACLK_REINIT)) {
        if (unlikely(!init))
            return 0;

        // TODO: handle reinit
        info("reinit requested");
        aclk_shutdown();
    }

    if (unlikely(!init)) {
        aclk_send_maximum = config_get_number(CONFIG_SECTION_ACLK, "agent cloud link send maximum", 20);
        aclk_recv_maximum = config_get_number(CONFIG_SECTION_ACLK, "agent cloud link receive maximum", 20);

        aclk_hostname = config_get(CONFIG_SECTION_ACLK, "agent cloud link hostname", "localhost");
        aclk_port = config_get_number(CONFIG_SECTION_ACLK, "agent cloud link port", 1883);

        info("Maximum parallel outgoing messages %d", aclk_send_maximum);
        info("Maximum parallel incoming messages %d", aclk_recv_maximum);

        // This will setup the base publish topic internally
        get_publish_base_topic(PUBLICH_TOPIC_GET);
        init = 1;
    } else
        return 0;

    // initialize the low level link to the cloud
    rc = _link_lib_init(aclk_hostname, aclk_port, aclk_connect, aclk_disconnect);
    if (unlikely(rc)) {
        error("Failed to initialize the agent cloud link library");
        return 1;
    }

    return 0;
}

int aclk_heartbeat()
{
    static time_t last_beat = 0;
    time_t current_beat;

    current_beat = now_realtime_sec();

    // Skip the first time and initialize the time mark instead
    if (unlikely(!last_beat)) {
        last_beat = current_beat;
        return 0;
    }

    if (unlikely(current_beat - last_beat >= ACLK_HEARTBEAT_INTERVAL)) {
        last_beat = current_beat;
        aclk_send_message("heartbeat", "ping");
    }
    return 0;
}

// encapsulate contents into metadata message as per ACLK documentation
void aclk_create_metadata_message(BUFFER *dest, char *type, BUFFER *contents)
{
    buffer_sprintf(
        dest,
        "\t{\"type\": \"%s\",\n"
        "\t\"contents\": %s\n\t}",
        type, contents->buffer);
}

// Send info metadata message to the cloud if the link is established
// or on request
int aclk_send_metadata_info()
{
    ACLK_LOCK;

    //    if (unlikely(!aclk_buffer))
    //        aclk_buffer = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);

    buffer_flush(aclk_buffer);

    BUFFER *info_json = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);

    web_client_api_request_v1_info_fill_buffer(localhost, info_json);

    aclk_create_metadata_message(aclk_buffer, "info", info_json);

    buffer_free(info_json);

    aclk_buffer->contenttype = CT_APPLICATION_JSON;

    ACLK_UNLOCK;

    aclk_send_message(ACLK_METADATA_TOPIC, aclk_buffer->buffer);
    //
    //    buffer_flush(aclk_buffer);
    //    aclk_buffer->contenttype = CT_APPLICATION_JSON;
    //    charts2json(localhost, aclk_buffer);
    //
    //    aclk_send_message(ACLK_METADATA_TOPIC, aclk_buffer->buffer);

    return 0;
}

//rrd_stats_api_v1_chart(RRDSET *st, BUFFER *buf)

int aclk_send_single_chart(char *chart)
{
    ACLK_LOCK;

    buffer_flush(aclk_buffer);

    RRDSET *st = rrdset_find(localhost, chart);

    if (!st)
        st = rrdset_find_byname(localhost, chart);

    if (!st)
        return 1;

    aclk_buffer->contenttype = CT_APPLICATION_JSON;

    BUFFER *info_json = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);

    rrdset2json(st, info_json, NULL, NULL);

    aclk_create_metadata_message(aclk_buffer, "chart", info_json);

    buffer_free(info_json);

    //st->last_accessed_time = now_realtime_sec();
    //_rrdset2json(st, aclk_buffer, NULL, NULL);

    ACLK_UNLOCK;
    aclk_send_message(ACLK_METADATA_TOPIC, aclk_buffer->buffer);
    return 0;
}

//rrd_stats_api_v1_chart
int aclk_collect_active_charts()
{
    RRDHOST *host;
    BUFFER *wb;

    host = localhost;

    size_t c;
    RRDSET *st;

    c = 0;
    rrdhost_rdlock(host);
    rrdset_foreach_read(st, host)
    {
        if (rrdset_is_available_for_viewers(st)) {
            info("Chart: %s", st->id);

            c++;
        }
        aclk_send_single_chart(st->id);
    }
    info("Found %d charts", c);

    //        RRDCALC *rc;
    //        for(rc = host->alarms; rc ; rc = rc->next) {
    //            if(rc->rrdset)
    //                alarms++;
    //        }
    rrdhost_unlock(host);

    //        buffer_sprintf(wb
    //            , "\n\t}"
    //              ",\n\t\"charts_count\": %zu"
    //              ",\n\t\"dimensions_count\": %zu"
    //              ",\n\t\"alarms_count\": %zu"
    //              ",\n\t\"rrd_memory_bytes\": %zu"
    //              ",\n\t\"hosts_count\": %zu"
    //              ",\n\t\"hosts\": ["
    //            , c
    //            , dimensions
    //            , alarms
    //            , memory
    //            , rrd_hosts_available
    //        );

    //        if(unlikely(rrd_hosts_available > 1)) {
    //            rrd_rdlock();
    //
    //            size_t found = 0;
    //            RRDHOST *h;
    //            rrdhost_foreach_read(h) {
    //                if(!rrdhost_should_be_removed(h, host, now)) {
    //                    buffer_sprintf(wb
    //                        , "%s\n\t\t{"
    //                          "\n\t\t\t\"hostname\": \"%s\""
    //                          "\n\t\t}"
    //                        , (found > 0) ? "," : ""
    //                        , h->hostname
    //                    );
    //
    //                    found++;
    //                }
    //            }
    //
    //            rrd_unlock();
    //        }
    //        else {
    //            buffer_sprintf(wb
    //                , "\n\t\t{"
    //                  "\n\t\t\t\"hostname\": \"%s\""
    //                  "\n\t\t}"
    //                , host->hostname
    //            );
    //        }
    //
    //        buffer_sprintf(wb, "\n\t]\n}\n");
    //    }

    return 0;
}

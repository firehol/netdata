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
int waiting_init = 1;
int cmdpause = 0; // Used to pause query processing

BUFFER *aclk_buffer = NULL;

int cloud_to_agent_parse(JSON_ENTRY *e)
{
    struct aclk_request *data = e->callback_data;

    switch(e->type) {
        case JSON_OBJECT:
            e->callback_function = cloud_to_agent_parse;
            break;
        case JSON_ARRAY:
            e->callback_function = cloud_to_agent_parse;
            break;
        case JSON_STRING:
            if (!strcmp(e->name, ACLK_JSON_IN_MSGID)) {
                data->msg_id = strdupz(e->data.string);
                break;
            }
            if (!strcmp(e->name, ACLK_JSON_IN_TYPE)) {
                data->type_id = strdupz(e->data.string);
                break;
            }
            if (!strcmp(e->name, ACLK_JSON_IN_TOPIC)) {
                data->topic = strdupz(e->data.string);
                break;
            }
            if (!strcmp(e->name, ACLK_JSON_IN_URL)) {
                data->url = strdupz(e->data.string);
                break;
            }
            break;
        case JSON_NUMBER:
            if (!strcmp(e->name, ACLK_JSON_IN_VERSION)) {
                data->version = atol(e->data.string);
                break;
            }
            break;

        case JSON_BOOLEAN:
            break;

        case JSON_NULL:
            break;
    }
    return 0;
}

char *send_http_request(char *host, char *port, char *url, BUFFER *b)
{
    struct timeval timeout = { .tv_sec = 30, .tv_usec = 0 };

    buffer_flush(b);
    buffer_sprintf(
        b,
        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: plain/text\r\nAccept-Language: en-us\r\nUser-Agent: Netdata/rocks\r\n\r\n",
        url, host);
    int sock = connect_to_this_ip46(IPPROTO_TCP, SOCK_STREAM, host, 0, "443", &timeout);

    if (unlikely(sock == -1)) {
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

pthread_cond_t  query_cond_wait = PTHREAD_COND_INITIALIZER;
pthread_mutex_t query_lock_wait = PTHREAD_MUTEX_INITIALIZER;

#define QUERY_THREAD_LOCK pthread_mutex_lock(&query_lock_wait);
#define QUERY_THREAD_UNLOCK pthread_mutex_unlock(&query_lock_wait)
#define QUERY_THREAD_WAKEUP pthread_cond_signal(&query_cond_wait)


struct aclk_query {
    time_t created;
    time_t run_after; // Delay run until after this time
    char *topic;      // Topic to respond to
    char *data;       // Internal data (NULL if request from the cloud)
    char *msg_id;     // msg_id generated by the cloud (NULL if internal)
    char *query;      // The actual query
    u_char deleted;     // Mark deleted for garbage collect
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

    freez(this_query->topic);
    freez(this_query->query);
    if (this_query->data)
        freez(this_query->data);
    if (this_query->msg_id)
        freez(this_query->msg_id);
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

// Need to have a lock before calling this
struct aclk_query *aclk_query_find(char *topic, char *data, char *msg_id, char *query)
{
    struct aclk_query *tmp_query;

    tmp_query = aclk_queue.aclk_query_head;

    while (tmp_query) {
        if (likely(!tmp_query->deleted)) {
            if (strcmp(tmp_query->topic, topic) == 0 && (strcmp(tmp_query->query, query) == 0)) {
                if ((!data || (data && strcmp(data, tmp_query->data) == 0)) &&
                    (!msg_id || (msg_id && strcmp(msg_id, tmp_query->msg_id) == 0)))
                    return tmp_query;
            }
        }
        tmp_query = tmp_query->next;
    }
    return NULL;
}

/*
 * Add a query to execute, the result will be send to the specified topic
 */

int aclk_queue_query(char *topic, char *data, char *msg_id, char *query, int run_after, int internal)
{
    struct aclk_query *new_query, *tmp_query, *last_query;

    // Ignore all commands while we wait for the agent to initialize
    if (unlikely(waiting_init))
        return 0;

    run_after = now_realtime_sec() + run_after;

    QUERY_LOCK;
    tmp_query = aclk_query_find(topic, data, msg_id, query);
    if (unlikely(tmp_query)) {
        if (tmp_query->run_after == run_after) {
            QUERY_UNLOCK;
            QUERY_THREAD_WAKEUP;
            return 0;
        }
        tmp_query->deleted = 1;
    }

    new_query = callocz(1, sizeof(struct aclk_query));
    if (internal) {
        new_query->topic = strdupz(topic);
        new_query->query = strdupz(query);
    } else {
        new_query->topic = topic;
        new_query->query = query;
        new_query->msg_id = msg_id;
    }

    if (data)
        new_query->data = strdupz(data);

    new_query->next = NULL;
    new_query->created = now_realtime_sec();
    new_query->run_after = run_after;

    info("Added query (%s) (%s)", topic, query);

    tmp_query = aclk_query_find_position(run_after);

    if (tmp_query) {
        new_query->next = tmp_query->next;
        tmp_query->next = new_query;
        if (tmp_query == aclk_queue.aclk_query_tail)
            aclk_queue.aclk_query_tail = new_query;
        aclk_queue.count++;
        QUERY_UNLOCK;
        QUERY_THREAD_WAKEUP;
        return 0;
    }

    new_query->next = aclk_queue.aclk_query_head;
    aclk_queue.aclk_query_head = new_query;
    aclk_queue.count++;

    QUERY_UNLOCK;
    QUERY_THREAD_WAKEUP;
    return 0;

//    if (likely(aclk_queue.aclk_query_tail)) {
//        aclk_queue.aclk_query_tail->next = new_query;
//        aclk_queue.aclk_query_tail = new_query;
//        aclk_queue.count++;
//        QUERY_UNLOCK;
//        return 0;
//    }
//
//    if (likely(!aclk_queue.aclk_query_head)) {
//        aclk_queue.aclk_query_head = new_query;
//        aclk_queue.aclk_query_tail = new_query;
//        aclk_queue.count++;
//        QUERY_UNLOCK;
//        return 0;
//    }
//    QUERY_UNLOCK;
//    return 0;
}

inline int aclk_submit_request(struct aclk_request *request)
{
    return aclk_queue_query(request->topic, NULL, request->msg_id, request->url, 0, 0);
}

/*
 * Get the next query to process - NULL if nothing there
 * The caller needs to free memory by calling aclk_query_free()
 *
 *      topic
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

    if (unlikely(cmdpause))
        return 0;

    if (!aclk_connection_initialized)
        return 0;

    this_query = aclk_queue_pop();
    if (likely(!this_query)) {
        //info("No pending queries");
        return 0;
    }

    if (unlikely(this_query->deleted)) {
        info("Garbage collect query %s:%s", this_query->topic, this_query->query);
        aclk_query_free(this_query);
        return 1;
    }

    query_count++;
    info(
        "Query #%d (%s) (%s) in queue %d seconds", query_count, this_query->topic, this_query->query,
        now_realtime_sec() - this_query->created);

    if (strncmp((char *)this_query->query, "/api/v1/", 8) == 0) {
        struct web_client *w = (struct web_client *)callocz(1, sizeof(struct web_client));
        w->response.data = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);
        strcpy(w->origin, "*"); // Simulate web_client_create_on_fd()
        w->cookie1[0] = 0;      // Simulate web_client_create_on_fd()
        w->cookie2[0] = 0;      // Simulate web_client_create_on_fd()
        w->acl = 0x1f;

        char *mysep = strchr(this_query->query, '?');
        if (mysep) {
            strncpyz(w->decoded_query_string, mysep, NETDATA_WEB_REQUEST_URL_SIZE);
            *mysep = '\0';
        } else
            strncpyz(w->decoded_query_string, this_query->query, NETDATA_WEB_REQUEST_URL_SIZE);

        mysep = strrchr(this_query->query, '/');

        rc = web_client_api_request_v1(localhost, w, mysep ? mysep + 1 : "noop");

        //TODO: handle bad response perhaps in a different way. For now it does to the payload
        //if (rc == HTTP_RESP_OK || 1) {
            buffer_flush(aclk_buffer);

            aclk_create_metadata_message(aclk_buffer, mysep ? mysep + 1 : "noop", this_query->msg_id, w->response.data);
            aclk_buffer->contenttype = CT_APPLICATION_JSON;
            aclk_send_message(this_query->topic, aclk_buffer->buffer);
        //} else
        //   error("Query RESP: %s", w->response.data->buffer);

        buffer_free(w->response.data);
        freez(w);
        aclk_query_free(this_query);
        return 1;
    }

    if (strcmp((char *)this_query->topic, "_chart") == 0) {
        aclk_send_single_chart(this_query->data, this_query->query);
    }

    aclk_query_free(this_query);

    return 1;
}

// Launch a query processing thread

/*
 * Process all pending queries
 * Return 0 if no queries were processed, 1 otherwise
 *
 */

int aclk_process_queries()
{
    int rc;

    if (unlikely(cmdpause))
        return 0;

    // Return if no queries pending
    if (likely(!aclk_queue.count))
        return 0;

    info("Processing %ld queries", aclk_queue.count);

    while (aclk_process_query()) {
        //rc = _link_event_loop(0);
    };

    return 1;
}

static void aclk_query_thread_cleanup(void *ptr)
{
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;
    static_thread->enabled = NETDATA_MAIN_THREAD_EXITING;

    info("cleaning up...");

    static_thread->enabled = NETDATA_MAIN_THREAD_EXITED;
}

#define QUERY_LOCK
/**
 * MAin query processing thread
 *
 */
void *aclk_query_main_thread(void *ptr)
{
    netdata_thread_cleanup_push(aclk_query_thread_cleanup, ptr);

    while (!netdata_exit) {
        int rc;

        QUERY_THREAD_LOCK;

        if (unlikely(!aclk_metadata_submitted)) {
            aclk_send_metadata();
            aclk_metadata_submitted = 1;
        }

        if (unlikely(pthread_cond_wait(&query_cond_wait, &query_lock_wait)))
            sleep_usec(USEC_PER_SEC * 1);

        if (likely(aclk_connection_initialized && !netdata_exit)) {
            while (aclk_process_queries()) {
                // Sleep for a few ms and retry maybe we have something to process
                // before going to sleep
                // TODO: This needs improvement to avoid missed queries
                sleep_usec(USEC_PER_MS * 100);
            }
        }

        QUERY_THREAD_UNLOCK;

    } // forever
    info("Shuttign down query processing thread");
    netdata_thread_cleanup_pop(1);
    return NULL;
}

// Thread cleanup
static void aclk_main_cleanup(void *ptr)
{
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;
    static_thread->enabled = NETDATA_MAIN_THREAD_EXITING;

    info("cleaning up...");

    QUERY_THREAD_WAKEUP;

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
    //netdata_thread_t *query_thread;
    struct netdata_static_thread query_thread;

    memset(&query_thread, 0, sizeof(query_thread));

    netdata_thread_cleanup_push(aclk_main_cleanup, ptr);

    if (unlikely(!aclk_buffer))
        aclk_buffer = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);

    assert(aclk_buffer != NULL);

    //netdata_thread_cleanup_push(aclk_query_thread_cleanup, ptr);
    //netdata_thread_create(&query_thread.thread , "ACLKQ", NETDATA_THREAD_OPTION_DEFAULT, aclk_query_main_thread, &query_thread);
    info("Waiting for netdata to be ready");
    while (!netdata_ready) {
        sleep_usec(USEC_PER_MS * 300);
    }
    info("Waiting %d seconds for the agent to initialize", ACLK_STARTUP_WAIT);
    sleep_usec(USEC_PER_SEC * ACLK_STARTUP_WAIT);

    // Ok mark we are ready to accept incoming requests
    waiting_init = 0;

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
            static int initializing = 0;

            if (likely(initializing)) {
                _link_event_loop(ACLK_LOOP_TIMEOUT * 1000);
                continue;
            }
            initializing=1;
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

        if (unlikely(!query_thread.thread))
            netdata_thread_create(&query_thread.thread , "ACLKQ", NETDATA_THREAD_OPTION_DEFAULT,
                aclk_query_main_thread, &query_thread);

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

    if (likely(init))
        return 0;

    aclk_send_maximum = config_get_number(CONFIG_SECTION_ACLK, "agent cloud link send maximum", 20);
    aclk_recv_maximum = config_get_number(CONFIG_SECTION_ACLK, "agent cloud link receive maximum", 20);

    aclk_hostname = config_get(CONFIG_SECTION_ACLK, "agent cloud link hostname", "localhost");
    aclk_port = config_get_number(CONFIG_SECTION_ACLK, "agent cloud link port", 1883);

    info("Maximum parallel outgoing messages %d", aclk_send_maximum);
    info("Maximum parallel incoming messages %d", aclk_recv_maximum);

    // This will setup the base publish topic internally
    //get_publish_base_topic(PUBLICH_TOPIC_GET);

    // initialize the low level link to the cloud
    rc = _link_lib_init(aclk_hostname, aclk_port, aclk_connect, aclk_disconnect);
    if (unlikely(rc)) {
        error("Failed to initialize the agent cloud link library");
        return 1;
    }
    init = 1;

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

// Use this to disable encoding of quotes and newlines so that
// MQTT subscriber can display more readable data on screen

void aclk_create_header(BUFFER *dest, char *type, char *msg_id)
{
    uuid_t uuid;
    char uuid_str[36 + 1];

    if (unlikely(!msg_id)) {
        uuid_generate(uuid);
        uuid_unparse(uuid, uuid_str);
        msg_id = uuid_str;
    }

    buffer_sprintf(
        dest,
        "\t{\"type\": \"%s\",\n"
        "\t\"msg-id\": \"%s\",\n"
        "\t\"version\": %d,\n"
        "\t\"payload\": ",
        type, msg_id, ACLK_VERSION);
}

#define EYE_FRIENDLY 1

// encapsulate contents into metadata message as per ACLK documentation
void aclk_create_metadata_message(BUFFER *dest, char *type, char *msg_id, BUFFER *contents)
{
#ifndef EYE_FRIENDLY
    char *tmp_buffer = mallocz(NETDATA_WEB_RESPONSE_INITIAL_SIZE);
    char *src, *dst;
#endif

    buffer_sprintf(
        dest,
        "\t{\"type\": \"%s\",\n"
        "\t\"msg-id\": \"%s\",\n"
        "\t\"payload\": %s\n\t}",
        type, msg_id ? msg_id : "", contents->buffer);

#ifndef EYE_FRIENDLY
    //TODO: this is the initial escaping, It will expanded
    src = dest->buffer;
    dst = tmp_buffer;
    while (*src) {
        switch (*src) {
            case '0x0a':
            case '\n':
                *dst++ = '\\';
                *dst++ = 'n';
                break;
            case '\"':
                *dst++ = '\\';
                *dst++ = '\"';
                break;
            case '\'':
                *dst++ = '\\';
                *dst++ = '\"';
                break;
            default:
                *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';

    buffer_flush(dest);
    buffer_sprintf(dest, "%s", tmp_buffer);

    freez(tmp_buffer);
#endif
    return;
}

//TODO: this has been changed in the latest specs. We need to pack the data in one MQTT
//message with a payload and has a list of json objects
int aclk_send_alarm_metadata()
{
    //TODO: improve locking on the buffer -- same lock is used for the message send
    //improve error handling
    ACLK_LOCK;
    buffer_flush(aclk_buffer);
    // Alarms configuration
    aclk_create_header(aclk_buffer, "alarms", NULL);
    health_alarms2json(localhost, aclk_buffer, 1);
    buffer_sprintf(aclk_buffer,"\n}");
    ACLK_UNLOCK;
    aclk_send_message(ACLK_ALARMS_TOPIC, aclk_buffer->buffer);

    // Alarms log
    ACLK_LOCK;
    buffer_flush(aclk_buffer);
    aclk_create_header(aclk_buffer, "alarms_log", NULL);
    health_alarm_log2json(localhost, aclk_buffer, 0);
    buffer_sprintf(aclk_buffer,"\n}");
    ACLK_UNLOCK;
    aclk_send_message(ACLK_ALARMS_TOPIC, aclk_buffer->buffer);

    return 0;
}


// Send info metadata message to the cloud if the link is established
// or on request
int aclk_send_metadata()
{
    ACLK_LOCK;

    buffer_flush(aclk_buffer);

    aclk_create_header(aclk_buffer, "connect", NULL);
    buffer_sprintf(aclk_buffer,"{\n\t \"info\" : ");
    web_client_api_request_v1_info_fill_buffer(localhost, aclk_buffer);
    buffer_sprintf(aclk_buffer,", \n\t \"charts\" : ");
    aclk_send_charts(localhost, aclk_buffer);
    buffer_sprintf(aclk_buffer,"\n}\n}");
    aclk_buffer->contenttype = CT_APPLICATION_JSON;

    ACLK_UNLOCK;

    aclk_send_message(ACLK_METADATA_TOPIC, aclk_buffer->buffer);

    aclk_send_alarm_metadata();

    return 0;
}

//rrd_stats_api_v1_chart(RRDSET *st, BUFFER *buf)

int aclk_send_single_chart(char *hostname, char *chart)
{
    RRDHOST *target_host;
    ACLK_LOCK;

    buffer_flush(aclk_buffer);

    target_host = rrdhost_find_by_hostname(hostname, 0);
    if (!target_host)
        return 1;

    RRDSET *st = rrdset_find(target_host, chart);

    if (!st)
        st = rrdset_find_byname(target_host, chart);

    if (!st) {
        info("FAILED to find chart %s", chart);
        return 1;
    }

    aclk_buffer->contenttype = CT_APPLICATION_JSON;

    buffer_flush(aclk_buffer);

    aclk_create_header(aclk_buffer, "chart", NULL);

    aclk_rrdset2json(st, aclk_buffer, hostname, target_host == localhost ? 0 : 1);
    buffer_sprintf(aclk_buffer,"\n}\n}");


    ACLK_UNLOCK;
    aclk_send_message(ACLK_METADATA_TOPIC, aclk_buffer->buffer);
    return 0;
}

//rrd_stats_api_v1_chart
// This is modeled after void charts2json(RRDHOST *host, BUFFER *wb) {

int aclk_send_charts(RRDHOST *host, BUFFER *wb) {
    static char *custom_dashboard_info_js_filename = NULL;
    size_t c, dimensions = 0, memory = 0, alarms = 0;
    RRDSET *st;

    time_t now = now_realtime_sec();

    if(unlikely(!custom_dashboard_info_js_filename))
        custom_dashboard_info_js_filename = config_get(CONFIG_SECTION_WEB, "custom dashboard_info.js", "");

    buffer_sprintf(wb, "{\n"
                       "\t\"hostname\": \"%s\""
                       ",\n\t\"version\": \"%s\""
                       ",\n\t\"release_channel\": \"%s\""
                       ",\n\t\"os\": \"%s\""
                       ",\n\t\"timezone\": \"%s\""
                       ",\n\t\"update_every\": %d"
                       ",\n\t\"history\": %ld"
                       ",\n\t\"memory_mode\": \"%s\""
                       ",\n\t\"custom_info\": \"%s\""
                       ",\n\t\"charts\": {"
        , host->hostname
        , host->program_version
        , get_release_channel()
        , host->os
        , host->timezone
        , host->rrd_update_every
        , host->rrd_history_entries
        , rrd_memory_mode_name(host->rrd_memory_mode)
        , custom_dashboard_info_js_filename
    );

    c = 0;
    rrdhost_rdlock(host);
    rrdset_foreach_read(st, host) {
        if(rrdset_is_available_for_viewers(st)) {
            if(c) buffer_strcat(wb, ",");
            buffer_strcat(wb, "\n\t\t\"");
            buffer_strcat(wb, st->id);
            buffer_strcat(wb, "\": ");
            aclk_rrdset2json(st, wb, host->hostname, host == localhost ? 0 : 1);

            c++;
            st->last_accessed_time = now;
        }
    }

    RRDCALC *rc;
    for(rc = host->alarms; rc ; rc = rc->next) {
        if(rc->rrdset)
            alarms++;
    }
    rrdhost_unlock(host);

    buffer_sprintf(wb
        , "\n\t}"
          ",\n\t\"charts_count\": %zu"
          ",\n\t\"dimensions_count\": %zu"
          ",\n\t\"alarms_count\": %zu"
          ",\n\t\"rrd_memory_bytes\": %zu"
          ",\n\t\"hosts_count\": %zu"
          ",\n\t\"hosts\": ["
        , c
        , dimensions
        , alarms
        , memory
        , rrd_hosts_available
    );

    if(unlikely(rrd_hosts_available > 1)) {
        rrd_rdlock();

        size_t found = 0;
        RRDHOST *h;
        rrdhost_foreach_read(h) {
            if(!rrdhost_should_be_removed(h, host, now)) {
                buffer_sprintf(wb
                    , "%s\n\t\t{"
                      "\n\t\t\t\"hostname\": \"%s\""
                      "\n\t\t}"
                    , (found > 0) ? "," : ""
                    , h->hostname
                );

                found++;
            }
        }

        rrd_unlock();
    }
    else {
        buffer_sprintf(wb
            , "\n\t\t{"
              "\n\t\t\t\"hostname\": \"%s\""
              "\n\t\t}"
            , host->hostname
        );
    }

    buffer_sprintf(wb, "\n\t]\n}\n");
    return 0;
}


void aclk_rrdset2json(RRDSET *st, BUFFER *wb, char *hostname, int is_slave)
{
    rrdset_rdlock(st);

    time_t first_entry_t = rrdset_first_entry_t(st);
    time_t last_entry_t = rrdset_last_entry_t(st);

    buffer_sprintf(
        wb,
        "\t\t{\n"
        //"\t\t\t\"hostname\": \"%s\",\n"
        //"\t\t\t\"is_slave\": \"%d\",\n"
        "\t\t\t\"id\": \"%s\",\n"
        "\t\t\t\"name\": \"%s\",\n"
        "\t\t\t\"type\": \"%s\",\n"
        "\t\t\t\"family\": \"%s\",\n"
        "\t\t\t\"context\": \"%s\",\n"
        "\t\t\t\"title\": \"%s (%s)\",\n"
        "\t\t\t\"priority\": %ld,\n"
        "\t\t\t\"plugin\": \"%s\",\n"
        "\t\t\t\"module\": \"%s\",\n"
        "\t\t\t\"enabled\": %s,\n"
        "\t\t\t\"units\": \"%s\",\n"
        "\t\t\t\"data_url\": \"/api/v1/data?chart=%s\",\n"
        "\t\t\t\"chart_type\": \"%s\",\n"
        //"\t\t\t\"duration\": %ld,\n"
        //"\t\t\t\"first_entry\": %ld,\n"
        //"\t\t\t\"last_entry\": %ld,\n"
        "\t\t\t\"update_every\": %d,\n"
        "\t\t\t\"dimensions\": {\n",
        //hostname, is_slave,
        st->id, st->name, st->type, st->family, st->context, st->title, st->name, st->priority,
        st->plugin_name ? st->plugin_name : "", st->module_name ? st->module_name : "",
        rrdset_flag_check(st, RRDSET_FLAG_ENABLED) ? "true" : "false", st->units, st->name,
        rrdset_type_name(st->chart_type),
        //last_entry_t - first_entry_t + st->update_every //st->entries * st->update_every
        //,
        //first_entry_t //rrdset_first_entry_t(st)
        //,
        //last_entry_t //rrdset_last_entry_t(st)
        //,
        st->update_every);

    unsigned long memory = st->memsize;

    size_t dimensions = 0;
    RRDDIM *rd;
    rrddim_foreach_read(rd, st)
    {
        if (rrddim_flag_check(rd, RRDDIM_FLAG_HIDDEN) || rrddim_flag_check(rd, RRDDIM_FLAG_OBSOLETE))
            continue;

        memory += rd->memsize;

        buffer_sprintf(
            wb,
            "%s"
            "\t\t\t\t\"%s\": { \"name\": \"%s\" }",
            dimensions ? ",\n" : "", rd->id, rd->name);

        dimensions++;
    }

    //if(dimensions_count) *dimensions_count += dimensions;
    //if(memory_used) *memory_used += memory;

    buffer_sprintf(wb, "\n\t\t\t},\n\t\t\t\"chart_variables\": ");
    health_api_v1_chart_custom_variables2json(st, wb);

    buffer_strcat(wb, ",\n\t\t\t\"green\": ");
    buffer_rrd_value(wb, st->green);
    buffer_strcat(wb, ",\n\t\t\t\"red\": ");
    buffer_rrd_value(wb, st->red);

//    buffer_strcat(wb, ",\n\t\t\t\"alarms\": {\n");
//    size_t alarms = 0;
//    RRDCALC *rc;
//    for (rc = st->alarms; rc; rc = rc->rrdset_next) {
//        buffer_sprintf(
//            wb,
//            "%s"
//            "\t\t\t\t\"%s\": {\n"
//            "\t\t\t\t\t\"id\": %u,\n"
//            "\t\t\t\t\t\"status\": \"%s\",\n"
//            "\t\t\t\t\t\"units\": \"%s\",\n"
//            "\t\t\t\t\t\"update_every\": %d\n"
//            "\t\t\t\t}",
//            (alarms) ? ",\n" : "", rc->name, rc->id, rrdcalc_status2string(rc->status), rc->units, rc->update_every);
//
//        alarms++;
//    }

    buffer_sprintf(wb, "\n\t\t\t}\n\t\t}");

    rrdset_unlock(st);
}

int    aclk_update_chart(RRDHOST *host, char *chart_name)
{
    if (host != localhost)
        return 0;

    aclk_queue_query("_chart", host->hostname, NULL, chart_name, 2, 1);
    return 0;
}

int    aclk_update_alarm(RRDHOST *host, char *alarm_name)
{
    if (host != localhost)
        return 0;

    aclk_queue_query("_alarm", host->hostname, NULL, alarm_name, 2, 1);
    return 0;
}


//TODO: add and check the incoming type e.g http
int aclk_handle_cloud_request(char *payload)
{
    struct aclk_request cloud_to_agent = { .msg_id = NULL, .topic = NULL, .url = NULL, .version = 1};

    int rc = json_parse(payload, &cloud_to_agent, cloud_to_agent_parse);

    if (unlikely(JSON_OK != rc)) {
        error("Malformed json request (%s)", payload);
        return 1;
    }

    if (unlikely(!cloud_to_agent.url || !cloud_to_agent.topic)) {
        return 1;
    }

    aclk_submit_request(&cloud_to_agent);

    return 0;
}

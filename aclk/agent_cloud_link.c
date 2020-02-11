// SPDX-License-Identifier: GPL-3.0-or-later

#include "libnetdata/libnetdata.h"
#include "agent_cloud_link.h"

// Read from the config file -- new section [agent_cloud_link]
// Defaults are supplied
int aclk_recv_maximum = 0; // default 20
int aclk_send_maximum = 0; // default 20

int aclk_port = 0;          // default 1883
char *aclk_hostname = NULL; //default localhost
int aclk_subscribed = 0;

int aclk_metadata_submitted = 0;
int agent_state = 0;
time_t last_init_sequence = 0;
int waiting_init = 1;
int cmdpause = 0; // Used to pause query processing

BUFFER *aclk_buffer = NULL;
char *global_base_topic = NULL;

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

//char *send_http_request(char *host, char *port, char *url, BUFFER *b)
//{
//    struct timeval timeout = { .tv_sec = 30, .tv_usec = 0 };
//
//    buffer_flush(b);
//    buffer_sprintf(
//        b,
//        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: plain/text\r\nAccept-Language: en-us\r\nUser-Agent: Netdata/rocks\r\n\r\n",
//        url, host);
//    int sock = connect_to_this_ip46(IPPROTO_TCP, SOCK_STREAM, host, 0, "443", &timeout);
//
//    if (unlikely(sock == -1)) {
//        error("Handshake failed");
//        return NULL;
//    }
//
//    SSL_CTX *ctx = security_initialize_openssl_client();
//    // Certificate chain: not updating the stores - do we need private CA roots?
//    // Calls to SSL_CTX_load_verify_locations would go here.
//    SSL *ssl = SSL_new(ctx);
//    SSL_set_fd(ssl, sock);
//    int err = SSL_connect(ssl);
//    SSL_write(ssl, b->buffer, b->len); // Timeout options?
//    int bytes_read = SSL_read(ssl, b->buffer, b->len);
//    SSL_shutdown(ssl);
//    close(sock);
//}

// Set when we have connection up and running from the connection callback
int aclk_connection_initialized = 0;

static netdata_mutex_t aclk_mutex = NETDATA_MUTEX_INITIALIZER;
static netdata_mutex_t query_mutex = NETDATA_MUTEX_INITIALIZER;
static netdata_mutex_t collector_mutex = NETDATA_MUTEX_INITIALIZER;

#define ACLK_LOCK netdata_mutex_lock(&aclk_mutex)
#define ACLK_UNLOCK netdata_mutex_unlock(&aclk_mutex)

#define COLLECTOR_LOCK netdata_mutex_lock(&collector_mutex)
#define COLLECTOR_UNLOCK netdata_mutex_unlock(&collector_mutex)

#define QUERY_LOCK netdata_mutex_lock(&query_mutex)
#define QUERY_UNLOCK netdata_mutex_unlock(&query_mutex)

pthread_cond_t  query_cond_wait = PTHREAD_COND_INITIALIZER;
pthread_mutex_t query_lock_wait = PTHREAD_MUTEX_INITIALIZER;

#define QUERY_THREAD_LOCK pthread_mutex_lock(&query_lock_wait);
#define QUERY_THREAD_UNLOCK pthread_mutex_unlock(&query_lock_wait)
#define QUERY_THREAD_WAKEUP pthread_cond_signal(&query_cond_wait)


/*
 * Maintain a list of collectors and chart count
 * If all the charts of a collector are deleted
 * then a new metadata dataset must be send to the cloud
 *
 */
struct _collector {
    time_t created;
    u_int32_t count;       //chart count
    u_int32_t hostname_hash;
    u_int32_t plugin_hash;
    u_int32_t module_hash;
    char *hostname;
    char *plugin_name;
    char *module_name;
    struct _collector *next;
};


struct _collector *collector_list = NULL;

struct aclk_query {
    time_t created;
    time_t run_after; // Delay run until after this time
    ACLK_CMD cmd;     // What command is this
    char *topic;      // Topic to respond to
    char *data;       // Internal data (NULL if request from the cloud)
    char *msg_id;     // msg_id generated by the cloud (NULL if internal)
    char *query;      // The actual query
    u_char deleted;   // Mark deleted for garbage collect
    struct aclk_query *next;
};

struct aclk_query_queue {
    struct aclk_query *aclk_query_head;
    struct aclk_query *aclk_query_tail;
    u_int64_t count;
} aclk_queue = { .aclk_query_head = NULL, .aclk_query_tail = NULL, .count = 0 };

/*
 * After a connection failure -- delay in milliseconds
 * When a connection is established, the delay function
 * should be called with mode 0 to reset the fail count
 */
unsigned long int aclk_delay(int mode)
{
    static int fail = 0;

    if (!mode) {
        fail = 0;
        return 0;
    }

    if (fail == 0) {
        srandom(time(NULL));
        fail++;
        return 0;
    }

    unsigned long int delay = (unsigned long int)((powl(ACLK_DELAY_SEED, fail)) * 1000 + (random() % 1618));

    return MIN(delay, ACLK_MAX_BACKOFF_DELAY);
}

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
struct aclk_query *aclk_query_find(char *topic, char *data, char *msg_id, char *query, ACLK_CMD cmd, struct aclk_query **last_query)
{
    struct aclk_query *tmp_query, *prev_query;

    tmp_query = aclk_queue.aclk_query_head;
    prev_query = NULL;
    while (tmp_query) {
        if (likely(!tmp_query->deleted)) {
            if (strcmp(tmp_query->topic, topic) == 0 && (strcmp(tmp_query->query, query) == 0)) {
                if ((!data || (data && strcmp(data, tmp_query->data) == 0)) &&
                    (!msg_id || (msg_id && strcmp(msg_id, tmp_query->msg_id) == 0))) {

                    if (likely(last_query))
                        *last_query = prev_query;
                    return tmp_query;
                }
            }
        }
        prev_query = tmp_query;
        tmp_query = tmp_query->next;
    }
    return NULL;
}

/*
 * Add a query to execute, the result will be send to the specified topic
 */

int aclk_queue_query(char *topic, char *data, char *msg_id, char *query, int run_after, int internal, ACLK_CMD aclk_cmd)
{
    struct aclk_query *new_query, *tmp_query;

    // Ignore all commands while we wait for the agent to initialize
    if (unlikely(waiting_init))
        return 0;

    // Ignore all commands if agent not stable
    if (agent_state == 0) {
        last_init_sequence = now_realtime_sec();
        return 0;
    }

    run_after = now_realtime_sec() + run_after;

    QUERY_LOCK;
    struct aclk_query *last_query = NULL;

    //last_query = NULL;
    tmp_query = aclk_query_find(topic, data, msg_id, query, aclk_cmd, &last_query);
    if (unlikely(tmp_query)) {
        if (tmp_query->run_after == run_after) {
            QUERY_UNLOCK;
            QUERY_THREAD_WAKEUP;
            return 0;
        }

        if (last_query)
            last_query->next = tmp_query->next;
        else
            aclk_queue.aclk_query_head = tmp_query->next;

#ifdef ACLK_DEBUG
        info("Removing double entry");
#endif
        aclk_query_free(tmp_query);
        aclk_queue.count--;
        //tmp_query->deleted = 1;
    }

    new_query = callocz(1, sizeof(struct aclk_query));
    new_query->cmd = aclk_cmd;
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

#ifdef ACLK_DEBUG
    info("Added query (%s) (%s)", topic, query);
#endif

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
}

inline int aclk_submit_request(struct aclk_request *request)
{
    return aclk_queue_query(request->topic, NULL, request->msg_id, request->url, 0, 0, ACLK_CMD_CLOUD);
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

    // Get rid of the deleted entries
    while (this_query && this_query->deleted) {
        aclk_queue.count--;

        aclk_queue.aclk_query_head = aclk_queue.aclk_query_head->next;

        if (likely(!aclk_queue.aclk_query_head)) {
            aclk_queue.aclk_query_tail = NULL;
        }

        aclk_query_free(this_query);

        this_query = aclk_queue.aclk_query_head;
    }

    if (likely(!this_query)) {
        QUERY_UNLOCK;
        return NULL;
    }

    if (!this_query->deleted && this_query->run_after > now_realtime_sec()) {
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

char *get_topic(char *sub_topic, char *final_topic, int max_size)
{
    if (unlikely(!global_base_topic))
        global_base_topic = GET_PUBLISH_BASE_TOPIC;

    if (unlikely(!global_base_topic))
        return sub_topic;

    snprintfz(final_topic, max_size, "%s/%s", global_base_topic, sub_topic);

    return final_topic;
}


/*
 * Free a collector structure
 */

static void _free_collector(struct _collector *collector)
{

    if (likely(collector->plugin_name))
        freez(collector->plugin_name);

    if (likely(collector->module_name))
        freez(collector->module_name);

    if (likely(collector->hostname))
        freez(collector->hostname);

    freez(collector);
}

/*
 * This will report the collector list
 *
 */
#ifdef ACLK_DEBUG
static void _dump_connector_list()
{

    struct _collector  *tmp_collector;

    COLLECTOR_LOCK;

    info("DUMPING ALL COLLECTORS");

    if (unlikely(!collector_list || !collector_list->next)) {
        COLLECTOR_UNLOCK;
        info("DUMPING ALL COLLECTORS -- nothing found");
        return;
    }

    // Note that the first entry is "dummy"
    tmp_collector = collector_list->next;

    while (tmp_collector) {
        info(
            "COLLECTOR %s : [%s:%s] count = %u", tmp_collector->hostname,
            tmp_collector->plugin_name ? tmp_collector->plugin_name : "",
            tmp_collector->module_name ? tmp_collector->module_name : "", tmp_collector->count);

        tmp_collector = tmp_collector->next;

    }
    info("DUMPING ALL COLLECTORS DONE");
    COLLECTOR_UNLOCK;
}
#endif

/*
 * This will cleanup the collector list
 *
 */
static void _reset_connector_list()
{
    struct _collector  *tmp_collector, *next_collector;

    COLLECTOR_LOCK;

    if (unlikely(!collector_list || !collector_list->next)) {
        COLLECTOR_UNLOCK;
        return;
    }

    // Note that the first entry is "dummy"
    tmp_collector  = collector_list->next;
    collector_list->count = 0;
    collector_list->next  = NULL;

    // We broke the link; we can unlock
    COLLECTOR_UNLOCK;

    while (tmp_collector) {
        next_collector = tmp_collector->next;
        _free_collector(tmp_collector);
        tmp_collector = next_collector;
    }
}


/*
 * Find a collector (if it exists)
 * Must lock before calling this
 * If last_collector is not null, it will return the previous collector in the linked
 * list (used in collector delete)
 */
static struct _collector *_find_collector(const char *hostname, const char *plugin_name, const char *module_name, struct _collector **last_collector)
{
    struct _collector *tmp_collector, *prev_collector;
    uint32_t plugin_hash;
    uint32_t module_hash;
    uint32_t hostname_hash;

    if (unlikely(!collector_list)) {
        collector_list = callocz(1, sizeof(struct _collector));
        return NULL;
    }

    if (unlikely(!collector_list->next))
        return NULL;

    plugin_hash = plugin_name?simple_hash(plugin_name):1;
    module_hash = module_name?simple_hash(module_name):1;
    hostname_hash = simple_hash(hostname);

    // Note that the first entry is "dummy"
    tmp_collector  = collector_list->next;
    prev_collector = collector_list;
    while (tmp_collector) {
        if (plugin_hash == tmp_collector->plugin_hash &&
            module_hash == tmp_collector->module_hash &&
            hostname_hash == tmp_collector->hostname_hash &&
            (!strcmp(hostname, tmp_collector->hostname)) &&
            (!plugin_name || !tmp_collector->plugin_name || !strcmp(plugin_name, tmp_collector->plugin_name)) &&
            (!module_name || !tmp_collector->module_name || !strcmp(module_name, tmp_collector->module_name))) {

            if (unlikely(last_collector))
                *last_collector = prev_collector;

            return tmp_collector;
        }

        prev_collector = tmp_collector;
        tmp_collector = tmp_collector->next;
    }

    return tmp_collector;
}

/*
 * Called to delete a collector
 * It will reduce the count (chart_count) and will remove it
 * from the linked list if the count reaches zero
 * The structure will be returned to the caller to free
 * the resources
 *
 */
static struct _collector *_del_collector(const char *hostname, const char *plugin_name, const char *module_name)
{
    struct _collector  *tmp_collector, *prev_collector = NULL;

    tmp_collector = _find_collector(hostname, plugin_name, module_name, &prev_collector);

    if (likely(tmp_collector)) {
        --tmp_collector->count;
        if (unlikely(!tmp_collector->count))
            prev_collector->next = tmp_collector->next;
    }
    return tmp_collector;
}


/*
 * Add a new collector (plugin / module) to the list
 * If it already exists just update the chart count
 *
 * Lock before calling
 */
static struct _collector  *_add_collector(const char *hostname, const char *plugin_name, const char *module_name)
{
    struct _collector  *tmp_collector;

    //info("Delay in case of failure = %llu", aclk_delay(1));

    tmp_collector = _find_collector(hostname, plugin_name, module_name, NULL);

    if (unlikely(!tmp_collector)) {

        tmp_collector = callocz(1, sizeof(struct _collector));
        tmp_collector->hostname_hash = simple_hash(hostname);
        tmp_collector->plugin_hash = plugin_name?simple_hash(plugin_name):1;
        tmp_collector->module_hash = module_name?simple_hash(module_name):1;

        tmp_collector->hostname = strdupz(hostname);
        tmp_collector->plugin_name = plugin_name?strdupz(plugin_name):NULL;
        tmp_collector->module_name = module_name?strdupz(module_name):NULL;

        tmp_collector->next = collector_list->next;
        collector_list->next = tmp_collector;
    }
    tmp_collector->count++;
#ifdef ACLK_DEBUG
    info("ADD COLLECTOR %s [%s:%s] -- count %u", hostname, plugin_name?plugin_name:"*", module_name?module_name:"*", tmp_collector->count);
#endif
    return tmp_collector;
}

/*
 * Add a new collector to the list
 * If it exists, update the chart count
 */
void aclk_add_collector(const char *hostname, const char *plugin_name, const char *module_name)
{
    struct _collector  *tmp_collector;

    COLLECTOR_LOCK;

    tmp_collector = _add_collector(hostname, plugin_name, module_name);

    if (unlikely(tmp_collector->count != 1)) {
        COLLECTOR_UNLOCK;
        return;
    }

    // TODO: QUEUE command to update the cloud here
    // aclk_queue_query("on_connect", "n/a", "n/a", "n/a", 2, 1, ACLK_CMD_ONCONNECT);

    COLLECTOR_UNLOCK;
}

/*
 * Delete a collector from the list
 * If the chart count reaches zero the collector will be removed
 * from the list by calling del_collector.
 *
 * This function will release the memory used and schedule
 * a cloud update
 */
void aclk_del_collector(const char *hostname, const char *plugin_name, const char *module_name)
{
    struct _collector  *tmp_collector;

    COLLECTOR_LOCK;

    tmp_collector = _del_collector(hostname, plugin_name, module_name);

    if (unlikely(!tmp_collector || tmp_collector->count)) {
        COLLECTOR_UNLOCK;
        return;
    }
#ifdef ACLK_DEBUG
    info("DEL COLLECTOR [%s:%s] -- count %u", plugin_name?plugin_name:"*", module_name?module_name:"*", tmp_collector->count);
#endif

    // TODO: Queue command for update to the cloud

    COLLECTOR_UNLOCK;
    // TODO: Queue command for update to the cloud
    // aclk_queue_query("on_connect", "n/a", "n/a", "n/a", 2, 1, ACLK_CMD_ONCONNECT);

    _free_collector(tmp_collector);
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
    static u_int64_t query_count = 0;
    //int rc;

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
#ifdef ACLK_DEBUG
        info("Garbage collect query %s:%s", this_query->topic, this_query->query);
#endif
        aclk_query_free(this_query);
        return 1;
    }
    query_count++;

    switch (this_query->cmd) {
        case ACLK_CMD_ONCONNECT:
            info("EXECUTING on connect metadata command");
            aclk_send_metadata();
            aclk_metadata_submitted = 1;
            aclk_query_free(this_query);
            return 1;
        case ACLK_CMD_CHART:
            info("EXECUTING a chart update command");
            aclk_send_single_chart(this_query->data, this_query->query);
            aclk_query_free(this_query);
            return 1;
        case ACLK_CMD_ALARM:
            info("EXECUTING an alarm update command");
            //aclk_send_single_chart(this_query->data, this_query->query);
            aclk_query_free(this_query);
            return 1;
        case ACLK_CMD_ALARMS:
            info("EXECUTING an alarm update command");
            aclk_send_alarm_metadata();
            aclk_query_free(this_query);
            return 1;
        default:
            break;
    }

#ifdef ACLK_DEBUG
    info(
        "Query #%d (%s) (%s) in queue %d seconds", (int) query_count, this_query->topic, this_query->query,
        (int) (now_realtime_sec() - this_query->created));
#endif

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

        // TODO: ignore return code for now
        web_client_api_request_v1(localhost, w, mysep ? mysep + 1 : "noop");

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

//    if (strcmp((char *)this_query->topic, "_chart") == 0) {
//        aclk_send_single_chart(this_query->data, this_query->query);
//    }

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
    if (unlikely(cmdpause))
        return 0;

    // Return if no queries pending
    if (likely(!aclk_queue.count))
        return 0;

    info("Processing %d queries", (int ) aclk_queue.count);

    //TODO: may consider possible throttling here
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

    COLLECTOR_LOCK;

    _reset_connector_list();
    freez(collector_list);

    COLLECTOR_UNLOCK;

    static_thread->enabled = NETDATA_MAIN_THREAD_EXITED;
}

/**
 * MAin query processing thread
 *
 */
void *aclk_query_main_thread(void *ptr)
{
    netdata_thread_cleanup_push(aclk_query_thread_cleanup, ptr);


    while (!netdata_exit) {

        while (!agent_state && !netdata_exit) {
            info("Waiting for agent collectors to initialize");
            sleep_usec(USEC_PER_SEC * ACLK_STABLE_TIMEOUT);
            if ((now_realtime_sec() - last_init_sequence) > ACLK_STABLE_TIMEOUT) {
                agent_state = 1;
                info("AGENT is stable");
#ifdef ACLK_DEBUG
                _dump_connector_list();
#endif
            }
        }

        if (unlikely(netdata_exit))
            break;

        if (unlikely(agent_state == 1)) {
            agent_state = 2;
            // Queue commands that need to run after agent stable

            aclk_queue_query("on_connect", "n/a", "n/a", "n/a", 0, 1, ACLK_CMD_ONCONNECT);

            //TODO: Rewrite to remove this part of the code since we already do it in the
            // the loop below
            if (likely(aclk_connection_initialized && !netdata_exit))
                aclk_process_queries();
        }

        QUERY_THREAD_LOCK;

        // TODO: Need to check if there are queries awaiting already
        if (unlikely(pthread_cond_wait(&query_cond_wait, &query_lock_wait)))
            sleep_usec(USEC_PER_SEC * 1);

        if (likely(aclk_connection_initialized && !netdata_exit)) {
            aclk_process_queries();
        }

        QUERY_THREAD_UNLOCK;

    } // forever
    info("Shutting down query processing thread");
    netdata_thread_cleanup_pop(1);
    return NULL;
}

// Thread cleanup
static void aclk_main_cleanup(void *ptr)
{
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;
    static_thread->enabled = NETDATA_MAIN_THREAD_EXITING;

    info("cleaning up...");

    // Wakeup thread to cleanup
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
    struct netdata_static_thread *query_thread;

    netdata_thread_cleanup_push(aclk_main_cleanup, ptr);

    if (unlikely(!aclk_buffer))
        aclk_buffer = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);

    assert(aclk_buffer != NULL);

    query_thread = callocz(1, sizeof(query_thread));

    info("Waiting for netdata to be ready");
    while (!netdata_ready) {
        sleep_usec(USEC_PER_MS * 300);
    }

    last_init_sequence = now_realtime_sec();

    while (!netdata_exit) {
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
            initializing = 1;
            info("Initializing connection");
            //send_http_request(aclk_hostname, "443", "/auth/challenge?id=blah", aclk_buffer);
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
        if (unlikely(!query_thread->thread)) {
            query_thread->thread = mallocz(sizeof(netdata_thread_t));
            netdata_thread_create(
                query_thread->thread, "ACLKQ", NETDATA_THREAD_OPTION_DEFAULT, aclk_query_main_thread, query_thread);
        }

        //TODO: Check if there is a return code
        _link_event_loop(ACLK_LOOP_TIMEOUT * 1000);

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

    final_topic = get_topic(sub_topic, topic, ACLK_MAX_TOPIC);

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
    //static char *global_base_topic = NULL;
    char topic[ACLK_MAX_TOPIC + 1];
    char *final_topic;

    if (!aclk_connection_initialized)
        return 0;

    if (unlikely(netdata_exit)) {
        return 1;
    }

    if (unlikely(aclk_wait_for_initialization()))
        return 1;

    final_topic = get_topic(sub_topic, topic, ACLK_MAX_TOPIC);

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
    (void) ptr;
    info("Connection detected");
    waiting_init = 0;
    return;
}

// This is called from a callback when the link goes down
void aclk_disconnect(void *ptr)
{
    (void) ptr;
    info("Disconnect detected");
    aclk_subscribed = 0;
    aclk_metadata_submitted = 0;
    waiting_init = 1;
    aclk_delay(0);
}

void aclk_shutdown()
{
    info("Shutdown initiated");
    aclk_connection_initialized = 0;
    _link_shutdown();
    info("Shutdown complete");
}

int aclk_init(ACLK_INIT_ACTION action)
{
    (void) action;

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
    global_base_topic = GET_PUBLISH_BASE_TOPIC;
    init = 1;

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
        "\t\"timestamp\": \"%u\",\n"
        "\t\"version\": %d,\n"
        "\t\"payload\": ",
        type, msg_id, now_realtime_sec(), ACLK_VERSION);
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
void aclk_send_alarm_metadata()
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
    charts2json(localhost, aclk_buffer);
    buffer_sprintf(aclk_buffer,"\n}\n}");
    aclk_buffer->contenttype = CT_APPLICATION_JSON;

    ACLK_UNLOCK;

    aclk_send_message(ACLK_METADATA_TOPIC, aclk_buffer->buffer);

    aclk_send_alarm_metadata();

    return 0;
}

// Trigged by a health reload, sends the alarm metadata
void aclk_alarm_reload()
{
    if (unlikely(!agent_state))
        return;

    aclk_queue_query("_alarm", localhost->hostname, NULL, "alarms", 2, 1, ACLK_CMD_ALARMS);
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

    rrdset2json(st, aclk_buffer, NULL, NULL);
    buffer_sprintf(aclk_buffer,"\n}\n}");


    ACLK_UNLOCK;
    aclk_send_message(ACLK_METADATA_TOPIC, aclk_buffer->buffer);
    return 0;
}

int    aclk_update_chart(RRDHOST *host, char *chart_name)
{
    (void) host;
    (void) chart_name;
#ifndef ENABLE_ACLK
    return 0;
#else
    if (host != localhost)
        return 0;

    aclk_queue_query("_chart", host->hostname, NULL, chart_name, 2, 1, ACLK_CMD_CHART);
    return 0;
#endif
}

int    aclk_update_alarm(RRDHOST *host, ALARM_ENTRY *ae)
{
    if (host != localhost)
        return 0;

    //TODO: Build the payload message
    //aclk_queue_query("_alarm", host->hostname, NULL, ae->name ? ae->name : "unknown", 2, 1, ACLK_CMD_ALARM);
    return 0;
}


//TODO: add and check the incoming type e.g http
int aclk_handle_cloud_request(char *payload)
{
    struct aclk_request cloud_to_agent = { .type_id = NULL, .msg_id = NULL, .topic = NULL, .url = NULL, .version = 0};

    int rc = json_parse(payload, &cloud_to_agent, cloud_to_agent_parse);

    if (unlikely(
            JSON_OK != rc || !cloud_to_agent.url || !cloud_to_agent.topic || !cloud_to_agent.msg_id ||
            !cloud_to_agent.type_id || !cloud_to_agent.version > ACLK_VERSION)) {

        if (cloud_to_agent.version > ACLK_VERSION)
            error("Unsupported version in JSON request %d", cloud_to_agent.version);

        error("Malformed json request (%s)", payload);

        if (cloud_to_agent.url)
            freez(cloud_to_agent.url);

        if (cloud_to_agent.type_id)
            freez(cloud_to_agent.type_id);

        if (cloud_to_agent.msg_id)
            freez(cloud_to_agent.msg_id);

        if (cloud_to_agent.topic)
            freez(cloud_to_agent.topic);

        return 1;
    }

    aclk_submit_request(&cloud_to_agent);

    return 0;
}

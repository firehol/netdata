// SPDX-License-Identifier: GPL-3.0-or-later

#include "sqlite_functions.h"

#define ENABLE_CACHE_CHARTS 1
#define ENABLE_CACHE_DIMENSIONS 1


#define error_report(x, args...) { errno = 0; error(x, ##args); }

const char *database_config[] = {
    "PRAGMA auto_vacuum=incremental; PRAGMA synchronous=1 ; PRAGMA journal_mode=WAL; PRAGMA temp_store=MEMORY;",
    "PRAGMA journal_size_limit=17179869184;",
    "ATTACH ':memory:' as ram;",
    "CREATE TABLE IF NOT EXISTS host (host_id blob PRIMARY KEY, hostname text, "
    "registry_hostname text, update_every int, os text, timezone text, tags text);",
    "CREATE TABLE IF NOT EXISTS chart (chart_id blob PRIMARY KEY, host_id blob, type text, id text, name text, "
    "family text, context text, title text, unit text, plugin text, module text, priority int, update_every int, "
    "chart_type int, memory_mode int, history_entries);",
    "CREATE TABLE IF NOT EXISTS dimension(dim_id blob PRIMARY KEY, chart_id blob, id text, name text, "
    "multiplier int, divisor int , algorithm int, options text);",

    "CREATE TABLE IF NOT EXISTS chart_active(chart_id blob PRIMARY KEY, date_created int);",
    "CREATE TABLE IF NOT EXISTS dimension_active(dim_id blob primary key, date_created int);",
    "CREATE INDEX IF NOT EXISTS ind_host on chart (host_id);",
    "CREATE INDEX IF NOT EXISTS ind_chart on dimension (chart_id);",

    "delete from chart_active;",
    "delete from dimension_active;",
    NULL
};

sqlite3 *db = NULL;

static uv_mutex_t sqlite_lookup;
static uv_mutex_t sqlite_add_page;
static uint32_t page_size;
static uint32_t page_count;
static uint32_t free_page_count;
//static uint32_t delete_rows;

/*
 * Database parameters
 */
uint32_t sqlite_disk_quota_mb; // quota specified in the database
uint32_t desired_pages = 0;

static int execute_insert(sqlite3_stmt *res)
{
    int rc;

    int retry_count = SQLITE_INSERT_MAX;
    while (retry_count-- && ((rc = sqlite3_step(res)) != SQLITE_DONE)) {
        if (rc != SQLITE_BUSY)
            break;
        usleep(SQLITE_INSERT_DELAY * USEC_PER_MS);
    }
    return rc;
}

/*
 * Marks a chart with UUID as active
 * Input: UUID
 */
static void store_active_chart(uuid_t *chart_uuid)
{
    sqlite3_stmt *res;
    int rc;

    if (unlikely(!db)) {
        errno = 0;
        error_report("Database has not been initialized");
        return;
    }

    rc = sqlite3_prepare_v2(db, SQL_STORE_ACTIVE_CHART, -1, &res, 0);
    if (rc != SQLITE_OK) {
        errno = 0;
        error_report("Failed to prepare statement to store active chart, rc = %d", rc);
        return;
    }
    rc = sqlite3_bind_blob(res, 1, chart_uuid, sizeof(*chart_uuid), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        errno = 0;
        error_report("Failed to bind input parameter to store active chart, rc = %d", rc);
        goto done;
    }
    rc = execute_insert(res);
    if (rc != SQLITE_DONE) {
        errno = 0;
        error_report("Failed to store active chart, rc = %d", rc);
    }

done:
    rc = sqlite3_finalize(res);
    if (unlikely(rc != SQLITE_OK)) {
        errno = 0;
        error_report("Failed to finalize statement in store dimension, rc = %d", rc);
    }
    return;
}

/*
 * Marks a dimension with UUID as active
 * Input: UUID
 */
static void store_active_dimension(uuid_t *dimension_uuid)
{
    sqlite3_stmt *res = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, SQL_STORE_ACTIVE_DIMENSION, -1, &res, 0);
    if (rc != SQLITE_OK) {
        error_report("Failed to prepare statement to update active dimensions");
        goto done;
    }
    rc = sqlite3_bind_blob(res, 1, dimension_uuid, sizeof(*dimension_uuid), SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK)) {
        error_report("Failed to bind parameter to update active dimensions");
        goto done;
    }
    rc = execute_insert(res);
    if (unlikely(rc != SQLITE_DONE)) {
        error_report("Failed to mark dimension as active, rc = %d", rc);
    }

done:
    rc = sqlite3_finalize(res);
    if (unlikely(rc != SQLITE_OK))
        error_report("Failed to finalize statement in store dimension, rc = %d", rc);
    return;
}


/*
 * Initialize the SQLite database
 * Return 0 on success
 */
int sql_init_database(void)
{
    char *err_msg = NULL;
    char sqlite_database[FILENAME_MAX + 1];
    int rc;

    fatal_assert(0 == uv_mutex_init(&sqlite_lookup));
    fatal_assert(0 == uv_mutex_init(&sqlite_add_page));

    snprintfz(sqlite_database, FILENAME_MAX, "%s/netdata-meta.db", netdata_configured_cache_dir);
    rc = sqlite3_open(sqlite_database, &db);
    if (rc != SQLITE_OK) {
        error_report("Failed to initialize database at %s", sqlite_database);
        return 1;
    }

    info("SQLite database %s initialization", sqlite_database);

    for (int i = 0; database_config[i]; i++) {
        debug(D_SQLITE, "Executing %s", database_config[i]);
        rc = sqlite3_exec(db, database_config[i], 0, 0, &err_msg);
        if (rc != SQLITE_OK) {
            error_report("SQLite error during database setup, rc = %d (%s)", rc, err_msg);
            error_report("SQLite failed statement %s", database_config[i]);
            sqlite3_free(err_msg);
            return 1;
        }
    }
    info("SQLite database initialization completed");
    return 0;
}

/*
 * Close the sqlite database
 */

void sql_close_database(void)
{
    int rc;
    if (unlikely(!db))
        return;

    info("Closing SQLite database");
    rc = sqlite3_close(db);
    if (rc != SQLITE_OK) {
        error_report("Error %d while closing the SQLite database", rc);
    }
    return;
        //        uv_mutex_lock(&sqlite_add_page);
        //        if (pending_page_inserts) {
        //            info("Writing final transactions %u", pending_page_inserts);
        //            sqlite3_exec(db_page, "COMMIT TRANSACTION;", 0, 0, &err_msg);
        //            pending_page_inserts = 0;
        //        }
}

/*
 * Return the database size in MiB
 */
int sql_database_size(void)
{
    sqlite3_stmt *chk_size;
    int rc;

    rc = sqlite3_prepare_v2(db, "pragma page_count;", -1, &chk_size, 0);
    if (rc != SQLITE_OK)
        return 0;

    if (sqlite3_step(chk_size) == SQLITE_ROW)
        page_count = sqlite3_column_int(chk_size, 0);

    sqlite3_finalize(chk_size);

    rc = sqlite3_prepare_v2(db, "pragma freelist_count;", -1, &chk_size, 0);
    if (rc != SQLITE_OK)
        return 0;

    if (sqlite3_step(chk_size) == SQLITE_ROW)
        free_page_count = sqlite3_column_int(chk_size, 0);

    sqlite3_finalize(chk_size);

    if (unlikely(!page_size)) {
        rc = sqlite3_prepare_v2(db, "pragma page_size;", -1, &chk_size, 0);
        if (rc != SQLITE_OK)
            return 0;

        if (sqlite3_step(chk_size) == SQLITE_ROW)
            page_size = (uint32_t)sqlite3_column_int(chk_size, 0);

        sqlite3_finalize(chk_size);
        desired_pages = (sqlite_disk_quota_mb * 0.95) * (1024 * 1024 / page_size);
        info(
            "Database desired size is %u pages (page size is %u bytes). Current size is %u pages (includes %u free pages)",
            desired_pages, page_size, page_count, free_page_count);
    }

    return ((page_count - free_page_count) / 1024) * (page_size / 1024);
}

/*
 * Add a UUID in memory for fast lookup
 *
 * Caller should lock the uuid_cache
 *   Under host holds charts
 *   Under chart holds dimensions
 *
 */

static inline void add_in_uuid_cache(struct uuid_cache **uuid_cache, uuid_t *uuid, const char *type, const char *id, const char *name)
{
    //info("add_in_uuid_cache: Adding type=%s, id=%s, name=%s", type?type:"Dimension", id, name?name:"<null>");
    struct uuid_cache *item = mallocz(sizeof(*item));
    uuid_copy(item->uuid, *uuid);
    item->id = id ? strdupz(id) : NULL;
    item->type = type ? strdupz(type) : NULL;
    item->name = name ? strdupz(name) : NULL;
    item->next = *uuid_cache;
    *uuid_cache = item;
    return;
}

/*
 * Destroy the uuid cache used during startup
 * Normally it should be empty if all charts and dimensions are active
 * TODO: Cleanup the cache after the agent starts to save memory
 * charts / dimensions that will be activated will just query the database
 */
void free_uuid_cache(struct uuid_cache **uuid_cache)
{
    struct uuid_cache *item;
    while ((item = *uuid_cache)) {
        *uuid_cache = item->next;
        freez(item->id);
        freez(item->type);
        freez(item->name);
        freez(item);
    }
    return;
}

/*
 * Find a type,id,name in cache -- remove if found
 * Caller must lock the uuid_cache
 */
uuid_t *find_in_uuid_cache(struct uuid_cache **uuid_cache, const char *type, const char *id, const char *name)
{
    struct uuid_cache *item;
    uuid_t *uuid = NULL;

    while ((item = *uuid_cache)) {
        if ((!strcmp(item->id, id)) && (!type || (!strcmp(item->type, type))) &&
            ((!name && !item->name) || (name && item->name && !strcmp(item->name, name)))) {
            uuid = mallocz(sizeof(uuid_t));
            uuid_copy(*uuid, item->uuid);
            *uuid_cache = item->next;
            freez(item->id);
            freez(item->type);
            freez(item->name);
            freez(item);
            break;
        }
        uuid_cache = &(item)->next;
    }
    return uuid;
}

int sql_cache_host_charts(RRDHOST *host)
{
#ifdef ENABLE_CACHE_CHARTS
    int rc;
    sqlite3_stmt *res = NULL;

    if (!db)
        return 0;

    if (!res) {
        rc = sqlite3_prepare_v2(
            db, "select chart_id, type, id, name from chart where host_id = @host;", -1, &res, 0);
        if (rc != SQLITE_OK)
            return 0;
    }

    rc = sqlite3_bind_blob(res, 1, &host->host_uuid, sizeof(host->host_uuid), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        error_report("Failed to bind host_uuid to find host charts");
        sqlite3_finalize(res);
        return 0;
    }
    int count = 0;
    while (sqlite3_step(res) == SQLITE_ROW) {
        add_in_uuid_cache(
            &host->uuid_cache, (uuid_t *)sqlite3_column_blob(res, 0), (const char *)sqlite3_column_text(res, 1),
            (const char *)sqlite3_column_text(res, 2), (const char *)sqlite3_column_text(res, 3));
        count++;
    }
    sqlite3_finalize(res);
    return count;
#else
    return 0;
#endif
}

int sql_cache_chart_dimensions(RRDSET *st)
{
#ifdef ENABLE_CACHE_DIMENSIONS
    int rc;
    sqlite3_stmt *res = NULL;
    if (!db) {
        error_report("Database has not been initialized");
        return 0;
    }

    if (!res) {
        rc = sqlite3_prepare_v2(db, "select dim_id, id, name from dimension where chart_id = @chart;", -1, &res, 0);
        if (rc != SQLITE_OK) {
            error_report("Failed to prepare statement to find chart dimensions");
            return 0;
        }
    }

    rc = sqlite3_bind_blob(res, 1, st->chart_uuid, sizeof(st->chart_uuid), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        error_report("Failed to bind chart_uuid to find chart dimensions");
        sqlite3_finalize(res);
        return 0;
    }
    int count = 0;
    while (sqlite3_step(res) == SQLITE_ROW) {
        add_in_uuid_cache(
            &st->state->uuid_cache, (uuid_t *)sqlite3_column_blob(res, 0), NULL,
            (const char *)sqlite3_column_text(res, 1), (const char *)sqlite3_column_text(res, 2));
        count++;
    }
    sqlite3_finalize(res);
    return count;
#else
    return 0;
#endif
}


uuid_t *sql_find_dim_uuid(RRDSET *st, RRDDIM *rd)
{
    sqlite3_stmt *res = NULL;
    uuid_t *uuid = NULL;
    int rc;

    uuid = find_in_uuid_cache(&st->state->uuid_cache, NULL, rd->id, rd->name);

    if (uuid) {
#ifdef NETDATA_INTERNAL_CHECKS
        char  uuid_str[37];
        uuid_unparse_lower(*uuid, uuid_str);
        debug(D_SQLITE, "Found UUID %s for dimension %s", uuid_str, rd->name);
#endif
        goto found;
    }

    //netdata_mutex_lock(&sqlite_find_uuid);
    if (!res) {
        rc = sqlite3_prepare_v2(db, SQL_FIND_DIMENSION_UUID, -1, &res, 0);
        if (rc != SQLITE_OK) {
            error_report("Failed to bind prepare statement to lookup dimension UUID in the database");
            //netdata_mutex_unlock(&sqlite_find_uuid);
            return NULL;
        }
    }

    rc = sqlite3_bind_blob(res, 1, st->chart_uuid, sizeof(st->chart_uuid), SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 2, rd->id, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 3, rd->name, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_step(res);

    uuid = mallocz(sizeof(uuid_t));
    if (likely(rc == SQLITE_ROW))
        uuid_copy(*uuid, sqlite3_column_blob(res, 0));
    else {
        uuid_generate(*uuid);
        rc = sql_store_dimension(uuid, st->chart_uuid, rd->id, rd->name, rd->multiplier, rd->divisor, rd->algorithm);
        if (unlikely(rc)) {
            error_report("Failed to store dimension metadata in the database");
        }
    }
    sqlite3_reset(res);
    sqlite3_finalize(res);

found:
    store_active_dimension(uuid);
    // netdata_mutex_unlock(&sqlite_find_uuid);
    return uuid;

bind_fail:
    error_report("Failed to bind input parameter to perform dimension UUID database lookup, rc = %d", rc);
    return NULL;
}

/*
 * Do a database lookup to find the UUID of a chart
 *
 */
uuid_t *sql_find_chart_uuid(RRDHOST *host, RRDSET *st, const char *type, const char *id, const char *name)
{
    sqlite3_stmt *res = NULL;
    uuid_t *uuid = NULL;
    int rc;

    uuid = find_in_uuid_cache(&host->uuid_cache, type, id, name);

    if (uuid) {
#ifdef NETDATA_INTERNAL_CHECKS
        char  uuid_str[37];
        uuid_unparse_lower(*uuid, uuid_str);
        debug(D_SQLITE, "Found UUID %s for chart %s.%s", uuid_str, type, name ? name : id);
#endif
        goto found;
    }

    if (!res) {
        rc = sqlite3_prepare_v2(db, SQL_FIND_CHART_UUID, -1, &res, 0);
        if (rc != SQLITE_OK) {
            error_report("Failed to bind prepare statement to lookup chart UUID in the database");
            return NULL;
        }
    }

//    int dim_id = sqlite3_bind_parameter_index(res, "@host");
//    int id_id = sqlite3_bind_parameter_index(res, "@id");
//    int name_id = sqlite3_bind_parameter_index(res, "@name");

    rc = sqlite3_bind_blob(res, 1, &host->host_uuid, sizeof(host->host_uuid), SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
            goto bind_fail;

    rc = sqlite3_bind_text(res, 2, type, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 3, id, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 4, name ? name : id, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_step(res);

    uuid = mallocz(sizeof(uuid_t));
    if (likely(rc == SQLITE_ROW))
        uuid_copy(*uuid, sqlite3_column_blob(res, 0));
    else {
        uuid_generate(*uuid);

#ifdef NETDATA_INTERNAL_CHECKS
        char uuid_str[37];
        uuid_unparse_lower(*uuid, uuid_str);
        debug(D_SQLITE,"Generating uuid [%s] for chart %s under host %s", uuid_str, st->id, host->hostname);
#endif
        rc = sql_store_chart(
            uuid, &host->host_uuid, st->type, id, name, st->family, st->context, st->title, st->units, st->plugin_name,
            st->module_name, st->priority, st->update_every, st->chart_type, st->rrd_memory_mode, st->entries);

        if (unlikely(rc)) {
            error_report("Failed to store chart metadata in the database");
        }
    }
    sqlite3_reset(res);
    sqlite3_finalize(res);

found:
    store_active_chart(uuid);
    return uuid;

bind_fail:
    error_report("Failed to bind input parameter to perform chart UUID database lookup, rc = %d", rc);
    return NULL;
}

// Functions to create host, chart, dimension in the database

int sql_store_host(
    uuid_t *host_uuid, const char *hostname, const char *registry_hostname, int update_every, const char *os,
    const char *tzone, const char *tags)
{
    sqlite3_stmt *res;
    int rc;

    if (unlikely(!db)) {
        error_report("Database has not been initialized");
        return 1;
    }

    rc = sqlite3_prepare_v2(db, SQL_STORE_HOST, -1, &res, 0);
    if (unlikely(rc != SQLITE_OK)) {
        error_report("Failed to prepare statement to store host, rc = %d", rc);
        return 1;
    }

    rc = sqlite3_bind_blob(res, 1, host_uuid, sizeof(*host_uuid), SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 2, hostname, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 3, registry_hostname, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_int(res, 4, update_every);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 5, os, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 6, tzone, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 7, tags, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_step(res);
    if (unlikely(rc != SQLITE_DONE)) {
        error_report("Failed to store host %s, rc = %d", hostname, rc);
    }

    rc = sqlite3_finalize(res);
    if (unlikely(rc != SQLITE_OK)) {
        error_report("Failed to finalize statement to store host %s, rc = %d", hostname, rc);
    }
    return 0;

bind_fail:
    error_report("Failed to bind parameter to store host %s, rc = %d", hostname, rc);
    return 1;
}

/*
 * Store a chart in the database
 */

int sql_store_chart(
    uuid_t *chart_uuid, uuid_t *host_uuid, const char *type, const char *id, const char *name, const char *family,
    const char *context, const char *title, const char *units, const char *plugin, const char *module, long priority,
    int update_every, int chart_type, int memory_mode, long history_entries)
{
    sqlite3_stmt *res;
    int rc, param = 0;

    if (unlikely(!db)) {
        error_report("Database has not been initialized");
        return 1;
    }

    rc = sqlite3_prepare_v2(db, SQL_STORE_CHART, -1, &res, 0);
    if (unlikely(rc != SQLITE_OK)) {
        error_report("Failed to prepare statement to store chart, rc = %d", rc);
        return 1;
    }

    param++;
    rc = sqlite3_bind_blob(res, 1, chart_uuid, sizeof(*chart_uuid), SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_blob(res, 2, host_uuid, sizeof(*host_uuid), SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_text(res, 3, type, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_text(res, 4, id, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    if (name) {
        rc = sqlite3_bind_text(res, 5, name, -1, SQLITE_TRANSIENT);
        if (unlikely(rc != SQLITE_OK))
            goto bind_fail;
    }

    param++;
    rc = sqlite3_bind_text(res, 6, family, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_text(res, 7, context, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_text(res, 8, title, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_text(res, 9, units, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_text(res, 10, plugin, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_text(res, 11, module, -1, SQLITE_TRANSIENT);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_int(res, 12, priority);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_int(res, 13, update_every);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_int(res, 14, chart_type);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_int(res, 15, memory_mode);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    param++;
    rc = sqlite3_bind_int(res, 16, history_entries);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_step(res);
    if (unlikely(rc != SQLITE_DONE)) {
        error_report("Failed to store chart, rc = %d", rc);
    }

    rc = sqlite3_finalize(res);
    if (unlikely(rc != SQLITE_OK)) {
        error_report("Failed to finalize statement in chart store function, rc = %d", rc);
    }

    return 0;

bind_fail:
    error_report("Failed to bind parameter %d to store chart, rc = %d", param, rc);
    return 1;
}

/*
 * Store a dimension
 */
int sql_store_dimension(
    uuid_t *dim_uuid, uuid_t *chart_uuid, const char *id, const char *name, collected_number multiplier,
    collected_number divisor, int algorithm)
{
    sqlite3_stmt *res;
    int rc;

    if (unlikely(!db)) {
        error_report("Database has not been initialized");
        return 1;
    }

    rc = sqlite3_prepare_v2(db, SQL_STORE_DIMENSION, -1, &res, 0);
    if (unlikely(rc != SQLITE_OK)) {
        error_report("Failed to prepare statement to store dimension, rc = %d", rc);
        return 1;
    }

    rc = sqlite3_bind_blob(res, 1, dim_uuid, sizeof(*dim_uuid), SQLITE_STATIC);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_blob(res, 2, chart_uuid, sizeof(*chart_uuid), SQLITE_STATIC);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 3, id, -1, SQLITE_STATIC);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_text(res, 4, name, -1, SQLITE_STATIC);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_int(res, 5, multiplier);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_int(res, 6, divisor);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_bind_int(res, 7, algorithm);
    if (unlikely(rc != SQLITE_OK))
        goto bind_fail;

    rc = sqlite3_step(res);
    if (unlikely(rc != SQLITE_DONE)) {
        error_report("Failed to store chart, rc = %d", rc);
    }

    rc = sqlite3_finalize(res);
    if (unlikely(rc != SQLITE_OK)) {
        error_report("Failed to finalize statement in store dimension, rc = %d", rc);
    }

    return 0;

bind_fail:
    error_report("Failed to bind parameter to store dimension, rc = %d", rc);
    return 1;
}

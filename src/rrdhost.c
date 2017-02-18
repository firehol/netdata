#define NETDATA_RRD_INTERNALS 1
#include "common.h"

// ----------------------------------------------------------------------------
// RRDHOST index

int rrdhost_compare(void* a, void* b) {
    if(((RRDHOST *)a)->hash_machine_guid < ((RRDHOST *)b)->hash_machine_guid) return -1;
    else if(((RRDHOST *)a)->hash_machine_guid > ((RRDHOST *)b)->hash_machine_guid) return 1;
    else return strcmp(((RRDHOST *)a)->machine_guid, ((RRDHOST *)b)->machine_guid);
}

avl_tree_lock rrdhost_root_index = {
        .avl_tree = { NULL, rrdhost_compare },
        .rwlock = AVL_LOCK_INITIALIZER
};

RRDHOST *rrdhost_find(const char *guid, uint32_t hash) {
    RRDHOST tmp;
    strncpyz(tmp.machine_guid, guid, GUID_LEN);
    tmp.hash_machine_guid = (hash)?hash:simple_hash(tmp.machine_guid);

    return (RRDHOST *)avl_search_lock(&(rrdhost_root_index), (avl *) &tmp);
}

#define rrdhost_index_add(rrdhost) (RRDHOST *)avl_insert_lock(&(rrdhost_root_index), (avl *)(rrdhost))
#define rrdhost_index_del(rrdhost) (RRDHOST *)avl_remove_lock(&(rrdhost_root_index), (avl *)(rrdhost))


// ----------------------------------------------------------------------------
// RRDHOST - internal helpers

static inline void rrdhost_init_hostname(RRDHOST *host, const char *hostname) {
    freez(host->hostname);
    host->hostname = strdupz(hostname);
    host->hash_hostname = simple_hash(host->hostname);
}

static inline void rrdhost_init_machine_guid(RRDHOST *host, const char *machine_guid) {
    strncpy(host->machine_guid, machine_guid, GUID_LEN);
    host->machine_guid[GUID_LEN] = '\0';
    host->hash_machine_guid = simple_hash(host->machine_guid);
}

// ----------------------------------------------------------------------------
// RRDHOST - add a host

RRDHOST *rrdhost_create(const char *hostname, const char *guid) {
    RRDHOST *host = callocz(1, sizeof(RRDHOST));

    pthread_rwlock_init(&(host->rrdset_root_rwlock), NULL);

    rrdhost_init_hostname(host, hostname);
    rrdhost_init_machine_guid(host, guid);

    avl_init_lock(&(host->rrdset_root_index), rrdset_compare);
    avl_init_lock(&(host->rrdset_root_index_name), rrdset_compare_name);
    avl_init_lock(&(host->rrdfamily_root_index), rrdfamily_compare);
    avl_init_lock(&(host->variables_root_index), rrdvar_compare);

    host->health_log.next_log_id = 1;
    host->health_log.next_alarm_id = 1;
    host->health_log.max = 1000;
    host->health_log.next_log_id =
    host->health_log.next_alarm_id = (uint32_t)now_realtime_sec();
    pthread_rwlock_init(&(host->health_log.alarm_log_rwlock), NULL);

    if(rrdhost_index_add(host) != host)
        fatal("Cannot add host '%s' to index. It already exists.", hostname);

    debug(D_RRDHOST, "Added host '%s'", host->hostname);
    return host;
}

// ----------------------------------------------------------------------------
// RRDHOST global / startup initialization

RRDHOST *localhost = NULL;

void rrd_init(char *hostname) {
    localhost = rrdhost_create(hostname, registry_get_this_machine_guid());
}

// ----------------------------------------------------------------------------
// RRDHOST - locks

void rrdhost_rwlock(RRDHOST *host) {
    pthread_rwlock_wrlock(&host->rrdset_root_rwlock);
}

void rrdhost_rdlock(RRDHOST *host) {
    pthread_rwlock_rdlock(&host->rrdset_root_rwlock);
}

void rrdhost_unlock(RRDHOST *host) {
    pthread_rwlock_unlock(&host->rrdset_root_rwlock);
}

void rrdhost_check_rdlock_int(RRDHOST *host, const char *file, const char *function, const unsigned long line) {
    int ret = pthread_rwlock_trywrlock(&host->rrdset_root_rwlock);

    if(ret == 0)
        fatal("RRDHOST '%s' should be read-locked, but it is not, at function %s() at line %lu of file '%s'", host->hostname, function, line, file);
}

void rrdhost_check_wrlock_int(RRDHOST *host, const char *file, const char *function, const unsigned long line) {
    int ret = pthread_rwlock_tryrdlock(&host->rrdset_root_rwlock);

    if(ret == 0)
        fatal("RRDHOST '%s' should be write-locked, but it is not, at function %s() at line %lu of file '%s'", host->hostname, function, line, file);
}

void rrdhost_free(RRDHOST *host) {
    if(!host) return;

    info("Freeing all memory for host '%s'...", host->hostname);

    rrdhost_rwlock(host);

    RRDSET *st;
    for(st = host->rrdset_root; st ;) {
        RRDSET *next = st->next;

        pthread_rwlock_wrlock(&st->rwlock);

        while(st->variables)
            rrdsetvar_free(st->variables);

        while(st->alarms)
            rrdsetcalc_unlink(st->alarms);

        while(st->dimensions)
            rrddim_free(st, st->dimensions);

        if(unlikely(rrdset_index_del(host, st) != st))
            error("RRDSET: INTERNAL ERROR: attempt to remove from index chart '%s', removed a different chart.", st->id);

        rrdset_index_del_name(host, st);

        st->rrdfamily->use_count--;
        if(!st->rrdfamily->use_count)
            rrdfamily_free(host, st->rrdfamily);

        pthread_rwlock_unlock(&st->rwlock);

        if(st->mapped == RRD_MEMORY_MODE_SAVE || st->mapped == RRD_MEMORY_MODE_MAP) {
            debug(D_RRD_CALLS, "Unmapping stats '%s'.", st->name);
            munmap(st, st->memsize);
        }
        else
            freez(st);

        st = next;
    }
    host->rrdset_root = NULL;

    freez(host->hostname);
    rrdhost_unlock(host);
    freez(host);

    info("Host memory cleanup completed...");
}

void rrdhost_save(RRDHOST *host) {
    if(!host) return;

    info("Saving host '%s' database...", host->hostname);

    RRDSET *st;
    RRDDIM *rd;

    // we get an write lock
    // to ensure only one thread is saving the database
    rrdhost_rwlock(host);

    for(st = host->rrdset_root; st ; st = st->next) {
        pthread_rwlock_rdlock(&st->rwlock);

        if(st->mapped == RRD_MEMORY_MODE_SAVE) {
            debug(D_RRD_CALLS, "Saving stats '%s' to '%s'.", st->name, st->cache_filename);
            savememory(st->cache_filename, st, st->memsize);
        }

        for(rd = st->dimensions; rd ; rd = rd->next) {
            if(likely(rd->memory_mode == RRD_MEMORY_MODE_SAVE)) {
                debug(D_RRD_CALLS, "Saving dimension '%s' to '%s'.", rd->name, rd->cache_filename);
                savememory(rd->cache_filename, rd, rd->memsize);
            }
        }

        pthread_rwlock_unlock(&st->rwlock);
    }

    rrdhost_unlock(host);
}

void rrdhost_free_all(void) {
    RRDHOST *host = localhost;

    // FIXME: lock all hosts

    while(host) {
        RRDHOST *next = host = host->next;
        rrdhost_free(host);
        host = next;
    }

    localhost = NULL;

    // FIXME: unlock all hosts
}

void rrdhost_save_all(void) {
    RRDHOST *host;
    for(host = localhost; host ; host = host->next)
        rrdhost_save(host);
}

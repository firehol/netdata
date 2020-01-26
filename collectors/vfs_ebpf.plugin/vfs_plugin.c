// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/time.h>
#include <sys/resource.h>

#include "vfs_plugin.h"

// callback required by eval()
int health_variable_lookup(const char *variable, uint32_t hash, struct rrdcalc *rc, calculated_number *result) {
    (void)variable;
    (void)hash;
    (void)rc;
    (void)result;
    return 0;
};

void send_statistics( const char *action, const char *action_result, const char *action_data) {
    (void) action;
    (void) action_result;
    (void) action_data;
    return;
}

// callbacks required by popen()
void signals_block(void) {};
void signals_unblock(void) {};
void signals_reset(void) {};

// required by get_system_cpus()
char *netdata_configured_host_prefix = "";

// callback required by fatal()
void netdata_cleanup_and_exit(int ret) {
    exit(ret);
}

// ----------------------------------------------------------------------
//From apps.plug
#define MAX_COMPARE_NAME 100
#define MAX_NAME 100

// ----------------------------------------------------------------------------
// internal flags
// handled in code (automatically set)

static int apps_charts = 1;
static int proc_pid_cmdline_is_needed = 0; // 1 when we need to read /proc/cmdline

struct target {
    char compare[MAX_COMPARE_NAME + 1];
    uint32_t comparehash;
    size_t comparelen;

    char id[MAX_NAME + 1];
    uint32_t idhash;

    char name[MAX_NAME + 1];

    int hidden;             // if set, we set the hidden flag on the dimension
    int ends_with;
    int starts_with;        // if set, the compare string matches only the
    // beginning of the command

    struct target *target;  // the one that will be reported to netdata
    struct target *next;
};

struct target *apps_groups_root_target = NULL;

static struct target *get_apps_groups_target(const char *id, struct target *target, const char *name) {
    int tdebug = 0, thidden = target?target->hidden:0, ends_with = 0;
    const char *nid = id;

    // extract the options
    while(nid[0] == '-' || nid[0] == '+' || nid[0] == '*') {
        if(nid[0] == '-') thidden = 1;
        if(nid[0] == '+') tdebug = 1;
        if(nid[0] == '*') ends_with = 1;
        nid++;
    }

    uint32_t hash = simple_hash(id);

    // find if it already exists
    struct target *w, *last = apps_groups_root_target;
    for(w = apps_groups_root_target ; w ; w = w->next) {
        if(w->idhash == hash && memcmp(nid, w->id, MAX_NAME) == 0)
            return w;

        last = w;
    }

    if(unlikely(!target)) {
        while(*name == '-') {
            if(*name == '-') thidden = 1;
            name++;
        }

        for(target = apps_groups_root_target ; target != NULL ; target = target->next) {
            if(!target->target && strcmp(name, target->name) == 0)
                break;
        }
    }

    if(target && target->target)
        fatal("Internal Error: request to link process '%s' to target '%s' which is linked to target '%s'", id, target->id, target->target->id);

    w = callocz(sizeof(struct target), 1);
    strncpyz(w->id, nid, MAX_NAME);
    w->idhash = simple_hash(w->id);

    if(unlikely(!target))
        // copy the name
        strncpyz(w->name, name, MAX_NAME);
    else
        // copy the id
        strncpyz(w->name, nid, MAX_NAME);

    strncpyz(w->compare, nid, MAX_COMPARE_NAME);
    size_t len = strlen(w->compare);
    if(w->compare[len - 1] == '*') {
        w->compare[len - 1] = '\0';
        w->starts_with = 1;
    }
    w->ends_with = ends_with;

    if(w->starts_with && w->ends_with)
        proc_pid_cmdline_is_needed = 1;

    w->comparehash = simple_hash(w->compare);
    w->comparelen = strlen(w->compare);

    w->hidden = thidden;

    w->target = target;

    // append it, to maintain the order in apps_groups.conf
    if(last) last->next = w;
    else apps_groups_root_target = w;

    return w;
}

static int read_apps_groups_conf(const char *path, const char *file) {
    char filename[FILENAME_MAX + 1];

    snprintfz(filename, FILENAME_MAX, "%s/apps_%s.conf", path, file);

    procfile *ff = procfile_open(filename, " :\t", PROCFILE_FLAG_DEFAULT);
    if(!ff) return 1;

    procfile_set_quotes(ff, "'\"");

    ff = procfile_readall(ff);
    if(!ff)
        return 1;

    size_t line, lines = procfile_lines(ff);

    for(line = 0; line < lines ;line++) {
        size_t word, words = procfile_linewords(ff, line);
        if(!words) continue;

        char *name = procfile_lineword(ff, line, 0);
        if(!name || !*name) continue;

        // find a possibly existing target
        struct target *w = NULL;

        // loop through all words, skipping the first one (the name)
        for(word = 0; word < words ;word++) {
            char *s = procfile_lineword(ff, line, word);
            if(!s || !*s) continue;
            if(*s == '#') break;

            // is this the first word? skip it
            if(s == name) continue;

            struct target *n = get_apps_groups_target(s, w, name);
            if(!n) {
                error("Cannot create target '%s' (line %zu, word %zu)", s, line, word);
                continue;
            }

            // just some optimization
            // to avoid searching for a target for each process
            if(!w) w = n->target?n->target:n;
        }
    }

    return 0;
}

// ----------------------------------------------------------------------
//Netdata eBPF library
void *libnetdata = NULL;
int (*load_bpf_file)(char *) = NULL;
int *map_fd = NULL;

//Libbpf (It is necessary to have at least kernel 4.10)
int (*bpf_map_lookup_elem)(int, const void *, void *);

static char *plugin_dir = NULL;
static char *user_config_dir = NULL;
static char *stock_config_dir = NULL;

//Global vectors
netdata_syscall_stat_t *file_syscall = NULL;
netdata_publish_syscall_t *publish_file = NULL;

static int update_every = 1;
static int thread_finished = 0;
static int close_plugin = 0;

pthread_mutex_t lock;

void clean_apps_groups() {
    struct target *w = apps_groups_root_target, *next;
    while (w) {
        next = w->next;
        free(w);
        w = next;
    }
}

static void int_exit(int sig)
{
    close_plugin = 1;

    //When both threads were not finished case I try to go in front this address, the collector will crash
    if(!thread_finished) {
        return;
    }

    if(file_syscall) {
        free(file_syscall);
    }

    if(publish_file) {
        free(publish_file);
    }

    if(libnetdata) {
        dlclose(libnetdata);
    }

    if(apps_groups_root_target) {
        clean_apps_groups();
    }

    exit(sig);
}

static void netdata_create_chart(char *family
                                , char *name
                                , char *msg
                                , char *axis
                                , char *web
                                , int order
                                , netdata_publish_syscall_t *move
                                , int end)
                                {
    printf("CHART %s.%s '' '%s' '%s' '%s' '' line %d 1 ''\n"
            , family
            , name
            , msg
            , axis
            , web
            , order);

    int i = 0;
    while (move && i < end) {
        printf("DIMENSION %s '' absolute 1 1\n", move->dimension);

        move = move->next;
        i++;
    }
}

static void netdata_create_io_chart(char *family, char *name, char *msg, char *axis, char *web, int order) {
    printf("CHART %s.%s '' '%s' '%s' '%s' '' line %d 1 ''\n"
            , family
            , name
            , msg
            , axis
            , web
            , order);

    printf("DIMENSION %s '' absolute 1 1\n", NETDATA_VFS_DIM_IN_FILE_BYTES );
    printf("DIMENSION %s '' absolute 1 1\n", NETDATA_VFS_DIM_OUT_FILE_BYTES );
}

static void netdata_create_charts() {
    netdata_create_chart(NETDATA_VFS_FAMILY
                         ,NETDATA_VFS_FILE_OPEN_COUNT
                         , "Number of calls for file IO."
                         , "Number of calls"
                         , NETDATA_WEB_GROUP
                         , 970
                         , publish_file
                         , 1);

    netdata_create_chart(NETDATA_VFS_FAMILY
                         , NETDATA_VFS_FILE_CLEAN_COUNT
                         , "Number of calls for file IO."
                         , "Number of calls"
                         , NETDATA_WEB_GROUP
                         , 971
                         , &publish_file[1]
                         , 1);

    netdata_create_chart(NETDATA_VFS_FAMILY
                        , NETDATA_VFS_FILE_WRITE_COUNT
                        , "Number of calls for file IO."
                        , "Number of calls"
                        , NETDATA_WEB_GROUP
                        , 972
                        , &publish_file[NETDATA_IN_START_BYTE]
                        , 1);

    netdata_create_chart(NETDATA_VFS_FAMILY
                         , NETDATA_VFS_FILE_READ_COUNT
                         , "Number of calls for file IO."
                         , "Number of calls"
                         , NETDATA_WEB_GROUP
                         , 973
                         , &publish_file[NETDATA_OUT_START_BYTE]
                         , 1);

    netdata_create_chart(NETDATA_VFS_FAMILY
                        , NETDATA_VFS_FILE_ERR_COUNT
                        , "Number of calls for file IO."
                        , "Number of calls"
                        , NETDATA_WEB_GROUP
                        , 974
                        , publish_file
                        , NETDATA_MAX_FILE_VECTOR);

    netdata_create_chart(NETDATA_VFS_FAMILY
                         , NETDATA_EXIT_SYSCALL
                         , "Number of calls to exit."
                         , "Number of calls"
                         , NETDATA_WEB_GROUP
                         , 975
                         , &publish_file[4]
                         , 2);

    netdata_create_chart(NETDATA_VFS_FAMILY
                         , NETDATA_PROCESS_SYSCALL
                         , "Number of calls to start process."
                         , "Number of calls"
                         , NETDATA_WEB_GROUP
                         , 976
                         , &publish_file[6]
                         , 1);

    netdata_create_io_chart(NETDATA_VFS_FAMILY
                            , NETDATA_VFS_IO_FILE_BYTES
                            , "Number of bytes read and written."
                            , "bytes/s"
                            , NETDATA_WEB_GROUP
                            , 977);
}

static void netdata_update_publish(netdata_publish_syscall_t *publish
                                  , netdata_publish_vfs_common_t *pvc
                                  , netdata_syscall_stat_t *input) {
    netdata_publish_syscall_t *move = publish;
    while(move) {
        if(input->call != move->pcall) {
            //This condition happens to avoid initial values with dimensions higher than normal values.
            if(move->pcall) {
                move->ncall = (input->call > move->pcall)?input->call - move->pcall: move->pcall - input->call;
                move->nbyte = (input->bytes > move->pbyte)?input->bytes - move->pbyte: move->pbyte - input->bytes;
                move->nerr = (input->ecall > move->nerr)?input->ecall - move->nerr: move->nerr - input->ecall;
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

    pvc->write = publish[2].nbyte;
    pvc->read = -publish[3].nbyte;
}

static void write_count_chart(char *name,netdata_publish_syscall_t *move, int end) {
    printf( "BEGIN %s.%s\n"
            , NETDATA_VFS_FAMILY
            , name);

    int i = 0;
    while (move && i < end) {
        printf("SET %s = %lld\n", move->dimension, (long long) move->ncall);

        move = move->next;
        i++;
    }

    printf("END\n");
}

static void write_err_chart(char *name,netdata_publish_syscall_t *move) {
    printf( "BEGIN %s.%s\n"
            , NETDATA_VFS_FAMILY
            , name);

    while (move) {
        printf("SET %s = %lld\n", move->dimension, (long long) move->nerr);

        move = move->next;
    }

    printf("END\n");
}

/*
static void write_bytes_chart(char *name,netdata_publish_syscall_t *move, int end) {
    printf( "BEGIN %s.%s\n"
            , NETDATA_VFS_FAMILY
            , name);

    int i = 0;
    while (move && i < end) {
        printf("SET %s = %lld\n", move->dimension, (long long) move->nbyte);

        move = move->next;
        i++;
    }

    printf("END\n");
}
*/

static void write_io_chart(netdata_publish_vfs_common_t *pvc) {
    printf( "BEGIN %s.%s\n"
            , NETDATA_VFS_FAMILY
            , NETDATA_VFS_IO_FILE_BYTES);

    printf("SET %s = %lld\n", NETDATA_VFS_DIM_IN_FILE_BYTES , (long long) pvc->write);
    printf("SET %s = %lld\n", NETDATA_VFS_DIM_OUT_FILE_BYTES , (long long) pvc->read);

    printf("END\n");
}

static void netdata_publish_data() {
    netdata_publish_vfs_common_t pvc;
    netdata_update_publish(publish_file, &pvc, file_syscall);

    write_count_chart(NETDATA_VFS_FILE_OPEN_COUNT, publish_file, 1);
    write_count_chart(NETDATA_VFS_FILE_CLEAN_COUNT, &publish_file[1], 1);
    write_count_chart(NETDATA_VFS_FILE_WRITE_COUNT, &publish_file[NETDATA_IN_START_BYTE], 1);
    write_count_chart(NETDATA_VFS_FILE_READ_COUNT, &publish_file[NETDATA_OUT_START_BYTE], 1);
    write_count_chart(NETDATA_EXIT_SYSCALL, &publish_file[4], 2);
    write_count_chart(NETDATA_PROCESS_SYSCALL, &publish_file[6], 1);
    write_err_chart(NETDATA_VFS_FILE_ERR_COUNT, publish_file);

 //   write_bytes_chart(NETDATA_VFS_OUT_FILE_BYTES, &publish_file[NETDATA_OUT_START_BYTE], 1);

    write_io_chart(&pvc);
}

void *vfs_publisher(void *ptr)
{
    (void)ptr;
    netdata_create_charts();

    usec_t step = update_every * USEC_PER_SEC;
    heartbeat_t hb;
    heartbeat_init(&hb);
    while(!close_plugin) {
        usec_t dt = heartbeat_next(&hb, step);
        (void)dt;

        pthread_mutex_lock(&lock);
        netdata_publish_data();
        pthread_mutex_unlock(&lock);

        fflush(stdout);
    }

    return NULL;
}

static void move_from_kernel2user() {
    uint32_t idx;
    uint32_t val;
    uint32_t res[NETDATA_GLOBAL_VECTOR];

    for (idx = 0; idx < NETDATA_GLOBAL_VECTOR; idx++) {
        if(!bpf_map_lookup_elem(map_fd[1], &idx, &val)) {
            res[idx] = val;
        }
    }

    file_syscall[0].call = res[0];
    file_syscall[1].call = res[8];
    file_syscall[2].call = res[2];
    file_syscall[3].call = res[5];
    file_syscall[4].call = res[10];
    file_syscall[5].call = res[11];
    file_syscall[6].call = res[12];

    file_syscall[0].ecall = res[1];
    file_syscall[1].ecall = res[3];
    file_syscall[2].ecall = res[9];
    file_syscall[3].ecall = res[6];

    file_syscall[2].bytes = (uint64_t)res[4];
    file_syscall[3].bytes = (uint64_t)res[7];
    /*
    struct netdata_pid_stat_t nps;

    uint64_t call[NETDATA_MAX_FILE_VECTOR], bytes[2];
    uint32_t ecall[NETDATA_MAX_FILE_VECTOR];

    memset(call, 0, sizeof(call));
    memset(bytes, 0, sizeof(bytes));
    memset(ecall, 0, sizeof(ecall));

    DIR *dir = opendir("/proc");
    if (!dir)
        return;

    struct dirent *de;
    while ((de = readdir(dir))) {
        if (!(de->d_type == DT_DIR))
            continue;

        if ((de->d_name[0] == '.' && de->d_name[1] == '\0')
            || (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == '\0')
            || (!isdigit(de->d_name[0])))
            continue;

        long int val = strtol(de->d_name, NULL, 10);
        if (val != LONG_MIN && val != LONG_MAX) {
            uint32_t tid = (uint32_t )val;
            if(!bpf_map_lookup_elem(map_fd[0], &tid, &nps)) {
                call[0] += nps.open_call;
                ecall[0] += nps.open_err;

                call[1] += nps.unlink_call;
                ecall[1] += nps.unlink_err;

                call[2] += nps.write_call;
                ecall[2] += nps.write_err;
                bytes[0] += nps.write_bytes;

                call[3] += nps.read_call;
                ecall[3] += nps.read_err;
                bytes[1] += nps.read_bytes;

                call[4] += nps.exit_call;
            }
        }
    }

    closedir(dir);

    file_syscall[0].call = call[0];
    file_syscall[1].call = call[1];
    file_syscall[2].call = call[2];
    file_syscall[3].call = call[3];
    file_syscall[4].call = call[4];

    file_syscall[0].ecall = ecall[0];
    file_syscall[1].ecall = ecall[1];
    file_syscall[2].ecall = ecall[2];
    file_syscall[3].ecall = ecall[3];

    file_syscall[2].bytes = bytes[0];
    file_syscall[3].bytes = bytes[1];
     */
}

void *vfs_collector(void *ptr)
{
    (void)ptr;

    usec_t step = 778879ULL;
    heartbeat_t hb;
    heartbeat_init(&hb);
    while(!close_plugin) {
        usec_t dt = heartbeat_next(&hb, step);
        (void)dt;

        pthread_mutex_lock(&lock);
        move_from_kernel2user();
        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

void set_file_values() {
    int i;

    static char *file_names[NETDATA_MAX_FILE_VECTOR] = { "open", "unlink", "write", "read", "exit", "release_task", "fork" };

    netdata_syscall_stat_t *is = file_syscall;
    netdata_syscall_stat_t *prev = NULL;

    netdata_publish_syscall_t *pio = publish_file;
    netdata_publish_syscall_t *publish_prev = NULL;
    for (i = 0; i < NETDATA_MAX_FILE_VECTOR; i++) {
        if(prev) {
            prev->next = &is[i];
        }
        prev = &is[i];

        pio[i].dimension = file_names[i];
        if(publish_prev) {
            publish_prev->next = &pio[i];
        }
        publish_prev = &pio[i];
    }
}

int allocate_global_vectors() {
    file_syscall = callocz(NETDATA_MAX_FILE_VECTOR, sizeof(netdata_syscall_stat_t));
    if(!file_syscall) {
        return -1;
    }

    publish_file = callocz(NETDATA_MAX_FILE_VECTOR, sizeof(netdata_publish_syscall_t));
    if(!publish_file) {
        return -1;
    }

    return 0;
}

static void build_complete_path(char *out, size_t length, char *filename) {
    if(plugin_dir){
        snprintf(out, length, "%s/%s", plugin_dir, filename);
    } else {
        snprintf(out, length, "%s", filename);
    }
}

static int vfs_load_libraries()
{
    char *error = NULL;
    char lpath[4096];

    build_complete_path(lpath, 4096, "libnetdata_ebpf.so");
    libnetdata = dlopen(lpath, RTLD_LAZY);
    if (!libnetdata) {
        error("[VFS] Cannot load %s.", lpath);
        return -1;
    } else {
        load_bpf_file = dlsym(libnetdata, "load_bpf_file");
        if ((error = dlerror()) != NULL) {
            error("[VFS] Cannot find load_bpf_file: %s", error);
            return -1;
        }

        map_fd =  dlsym(libnetdata, "map_fd");
        if ((error = dlerror()) != NULL) {
            fputs(error, stderr);
            return -1;
        }

        bpf_map_lookup_elem = dlsym(libnetdata, "bpf_map_lookup_elem");
        if ((error = dlerror()) != NULL) {
            fputs(error, stderr);
            return -1;
        }
    }

    return 0;
}

int vfs_load_ebpf()
{
    char lpath[4096];

    build_complete_path(lpath, 4096, "netdata_ebpf_vfs.o" );
    if (load_bpf_file(lpath) ) {
        error("[VFS] Cannot load program: %s.", lpath);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    //set name
    program_name = "vfs.plugin";

    //disable syslog
    error_log_syslog = 0;

    // set errors flood protection to 100 logs per hour
    error_log_errors_per_period = 100;
    error_log_throttle_period = 3600;

    if (argc > 1) {
        update_every = (int)strtol(argv[1], NULL, 10);
    }

    struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
    if (setrlimit(RLIMIT_MEMLOCK, &r)) {
        error("[NETWORK VIEWER] setrlimit(RLIMIT_MEMLOCK)");
        return 1;
    }

    //Get environment variables
    plugin_dir = getenv("NETDATA_PLUGINS_DIR");
    user_config_dir = getenv("NETDATA_USER_CONFIG_DIR");
    if(!user_config_dir)
        user_config_dir = CONFIG_DIR;

    stock_config_dir = getenv("NETDATA_STOCK_CONFIG_DIR");
    if(!stock_config_dir)
        stock_config_dir = LIBCONFIG_DIR;

    if(vfs_load_libraries()) {
        error("[VFS] Cannot load library.");
        thread_finished++;
        int_exit(2);
    }

    signal(SIGINT, int_exit);
    signal(SIGTERM, int_exit);

    if (vfs_load_ebpf()) {
        thread_finished++;
        int_exit(3);
    }

    if(allocate_global_vectors()) {
        thread_finished++;
        error("[VFS] Cannot allocate necessary vectors.");
        int_exit(4);
    }

    set_file_values();

    if (pthread_mutex_init(&lock, NULL)) {
        thread_finished++;
        int_exit(5);
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_t thread[2];

    if(read_apps_groups_conf(user_config_dir, "groups")) {
        info("[VFS] Cannot read process groups configuration file '%s/apps_groups.conf'. Will try '%s/apps_groups.conf'", user_config_dir, stock_config_dir);

        if(read_apps_groups_conf(stock_config_dir, "groups")) {
            error("Cannot read process groups '%s/apps_groups.conf'. There are no internal defaults. we will collect only global data.", stock_config_dir);
            apps_charts = 0;
        }
    }

    int i;
    int end = 2;

    for ( i = 0; i < end ; i++ ) {
        if ( ( pthread_create(&thread[i], &attr, (!i)?vfs_publisher:vfs_collector, NULL) ) ) {
            error("[VFS] Cannot create threads.");
            thread_finished++;
            int_exit(0);
            return 7;
        }

    }

    for ( i = 0; i < end ; i++ ) {
        if ( (pthread_join(thread[i], NULL) ) ) {
            error("[VFS] Cannot join threads.");
            thread_finished++;
            int_exit(0);
            return 7;
        }
    }

    thread_finished++;
    int_exit(0);

    return 0;
}

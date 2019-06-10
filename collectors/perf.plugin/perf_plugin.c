// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../libnetdata/libnetdata.h"

#include <linux/perf_event.h>

#define PLUGIN_PERF_NAME "perf.plugin"

#define NETDATA_CHART_PRIO_PERF_CPU_CYCLES            8701
#define NETDATA_CHART_PRIO_PERF_CACHE_LL              8906

// callback required by fatal()
void netdata_cleanup_and_exit(int ret) {
    exit(ret);
}

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

// callback required by eval()
int health_variable_lookup(const char *variable, uint32_t hash, struct rrdcalc *rc, calculated_number *result) {
    (void)variable;
    (void)hash;
    (void)rc;
    (void)result;
    return 0;
};

// required by get_system_cpus()
char *netdata_configured_host_prefix = "";

// Variables

#define RRD_TYPE_PERF "perf"

#define NO_FD -1
#define ALL_PIDS -1
#define UINT64_SIZE 8

static int debug = 0;

static int update_every = 1;

typedef enum perf_event_id {
    // Hardware counters
    EV_ID_CPU_CYCLES,
    EV_ID_INSTRUCTIONS,
    EV_ID_CACHE_REFERENCES,
    EV_ID_CACHE_MISSES,
    EV_ID_BRANCH_INSTRUCTIONS,
    EV_ID_BRANCH_MISSES,
    EV_ID_BUS_CYCLES,
    EV_ID_STALLED_CYCLES_FRONTEND,
    EV_ID_STALLED_CYCLES_BACKEND,
    EV_ID_REF_CPU_CYCLES,

    // Software counters
    EV_ID_CPU_CLOCK,
    EV_ID_TASK_CLOCK,
    EV_ID_PAGE_FAULTS,
    EV_ID_CONTEXT_SWITCHES,
    EV_ID_CPU_MIGRATIONS,
    EV_ID_PAGE_FAULTS_MIN,
    EV_ID_PAGE_FAULTS_MAJ,
    EV_ID_ALIGNMENT_FAULTS,
    EV_ID_EMULATION_FAULTS,

    // Hardware cache counters
    EV_ID_L1D_READ_ACCESS,
    EV_ID_L1D_READ_MISS,
    EV_ID_L1D_WRITE_ACCESS,
    EV_ID_L1D_WRITE_MISS,
    EV_ID_L1D_PREFETCH_ACCESS,

    EV_ID_L1I_READ_ACCESS,
    EV_ID_L1I_READ_MISS,

    EV_ID_LL_READ_ACCESS,
    EV_ID_LL_READ_MISS,
    EV_ID_LL_WRITE_ACCESS,
    EV_ID_LL_WRITE_MISS,

    EV_ID_DTLB_READ_ACCESS,
    EV_ID_DTLB_READ_MISS,
    EV_ID_DTLB_WRITE_ACCESS,
    EV_ID_DTLB_WRITE_MISS,

    EV_ID_ITLB_READ_ACCESS,
    EV_ID_ITLB_READ_MISS,

    EV_ID_PBU_READ_ACCESS,

    EV_ID_END
} perf_event_id_t;

enum perf_event_group {
    EV_GROUP_0,
    EV_GROUP_1,
    EV_GROUP_2,
    EV_GROUP_3,
    EV_GROUP_4,
    EV_GROUP_5,

    EV_GROUP_NUM
};

static int number_of_cpus;

static int *group_leader_fds[EV_GROUP_NUM];

static struct perf_event {
    perf_event_id_t id;

    int type;
    int config;

    int **group_leader_fd;
    int *fd;

    int disabled;
    int updated;

    uint64_t value;
} perf_events[] = {
    // Hardware counters
    {EV_ID_CPU_CYCLES,              PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,              &group_leader_fds[EV_GROUP_0], NULL, 0, 0, 0},
    {EV_ID_INSTRUCTIONS,            PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,            &group_leader_fds[EV_GROUP_1], NULL, 0, 0, 0},
    {EV_ID_CACHE_REFERENCES,        PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES,        &group_leader_fds[EV_GROUP_1], NULL, 0, 0, 0},
    {EV_ID_CACHE_MISSES,            PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,            &group_leader_fds[EV_GROUP_1], NULL, 0, 0, 0},
    {EV_ID_BRANCH_INSTRUCTIONS,     PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,     &group_leader_fds[EV_GROUP_1], NULL, 0, 0, 0},
    {EV_ID_BRANCH_MISSES,           PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,           &group_leader_fds[EV_GROUP_1], NULL, 0, 0, 0},
    {EV_ID_BUS_CYCLES,              PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES,              &group_leader_fds[EV_GROUP_0], NULL, 0, 0, 0},
    {EV_ID_STALLED_CYCLES_FRONTEND, PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND, &group_leader_fds[EV_GROUP_0], NULL, 0, 0, 0},
    {EV_ID_STALLED_CYCLES_BACKEND,  PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND,  &group_leader_fds[EV_GROUP_0], NULL, 0, 0, 0},
    {EV_ID_REF_CPU_CYCLES,          PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES,          &group_leader_fds[EV_GROUP_0], NULL, 0, 0, 0},

    // Software counters
    {EV_ID_CPU_CLOCK,        PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,        &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_TASK_CLOCK,       PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK,       &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_PAGE_FAULTS,      PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS,      &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_CONTEXT_SWITCHES, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES, &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_CPU_MIGRATIONS,   PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS,   &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_PAGE_FAULTS_MIN,  PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN,  &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_PAGE_FAULTS_MAJ,  PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ,  &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_ALIGNMENT_FAULTS, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_ALIGNMENT_FAULTS, &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},
    {EV_ID_EMULATION_FAULTS, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_EMULATION_FAULTS, &group_leader_fds[EV_GROUP_2], NULL, 0, 0, 0},

    // Hardware cache counters
    {EV_ID_L1D_READ_ACCESS,     PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D)  | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_3], NULL, 0, 0, 0},
    {EV_ID_L1D_READ_MISS,       PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D)  | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_3], NULL, 0, 0, 0},
    {EV_ID_L1D_WRITE_ACCESS,    PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D)  | (PERF_COUNT_HW_CACHE_OP_WRITE    << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_3], NULL, 0, 0, 0},
    {EV_ID_L1D_WRITE_MISS,      PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D)  | (PERF_COUNT_HW_CACHE_OP_WRITE    << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_3], NULL, 0, 0, 0},
    {EV_ID_L1D_PREFETCH_ACCESS, PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D)  | (PERF_COUNT_HW_CACHE_OP_PREFETCH << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_3], NULL, 0, 0, 0},

    {EV_ID_L1I_READ_ACCESS,     PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1I)  | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},
    {EV_ID_L1I_READ_MISS,       PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1I)  | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},

    {EV_ID_LL_READ_ACCESS,      PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_LL)   | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},
    {EV_ID_LL_READ_MISS,        PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_LL)   | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},
    {EV_ID_LL_WRITE_ACCESS,     PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_LL)   | (PERF_COUNT_HW_CACHE_OP_WRITE    << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},
    {EV_ID_LL_WRITE_MISS,       PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_LL)   | (PERF_COUNT_HW_CACHE_OP_WRITE    << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},

    {EV_ID_DTLB_READ_ACCESS,    PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_DTLB) | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},
    {EV_ID_DTLB_READ_MISS,      PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_DTLB) | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},
    {EV_ID_DTLB_WRITE_ACCESS,   PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_DTLB) | (PERF_COUNT_HW_CACHE_OP_WRITE    << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_4], NULL, 0, 0, 0},
    {EV_ID_DTLB_WRITE_MISS,     PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_DTLB) | (PERF_COUNT_HW_CACHE_OP_WRITE    << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_5], NULL, 0, 0, 0},

    {EV_ID_ITLB_READ_ACCESS,    PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_ITLB) | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_5], NULL, 0, 0, 0},
    {EV_ID_ITLB_READ_MISS,      PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_ITLB) | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS   << 16), &group_leader_fds[EV_GROUP_5], NULL, 0, 0, 0},

    {EV_ID_PBU_READ_ACCESS,     PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_BPU)  | (PERF_COUNT_HW_CACHE_OP_READ     << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16), &group_leader_fds[EV_GROUP_5], NULL, 0, 0, 0},

    {EV_ID_END, 0, 0, NULL, NULL, 0, 0, 0}
};

static int perf_init() {
    int cpu;
    struct perf_event_attr perf_event_attr;
    struct perf_event *current_event = NULL;
    unsigned long flags = 0;

    number_of_cpus = (int)get_system_cpus();

    // initialize all perf event file descriptors
    for(current_event = &perf_events[0]; current_event->id != EV_ID_END; current_event++) {
        current_event->fd = mallocz(number_of_cpus * sizeof(int));
        memset(current_event->fd, NO_FD, number_of_cpus * sizeof(int));

        *current_event->group_leader_fd = mallocz(number_of_cpus * sizeof(int));
        memset(*current_event->group_leader_fd, NO_FD, number_of_cpus * sizeof(int));
    }

    memset(&perf_event_attr, 0, sizeof(perf_event_attr));

    for(cpu = 0; cpu < number_of_cpus; cpu++) {
        for(current_event = &perf_events[0]; current_event->id != EV_ID_END; current_event++) {
            if(unlikely(current_event->disabled)) continue;

            perf_event_attr.type = current_event->type;
            perf_event_attr.config = current_event->config;

            int fd, group_leader_fd = *(*current_event->group_leader_fd + cpu);

            fd = syscall(
                __NR_perf_event_open,
                &perf_event_attr,
                ALL_PIDS,
                cpu,
                group_leader_fd,
                flags
            );

            if(group_leader_fd == NO_FD) group_leader_fd = fd;

            if(fd < 0) {
                switch errno {
                    case EACCES:
                        error("PERF: Cannot access to the PMU: Permission denied");
                        break;
                    case EBUSY:
                        error("PERF: Another event already has exclusive access to the PMU");
                        break;
                    default:
                        error("PERF: Cannot open perf event");
                }
                error("PERF: Disabling event %u", current_event->id);
                current_event->disabled = 1;
            }

            *(current_event->fd + cpu) = fd;
            *(*current_event->group_leader_fd + cpu) = group_leader_fd;

            if(unlikely(debug)) fprintf(stderr, "perf.plugin: event id = %u, cpu = %d, fd = %d, leader_fd = %d\n", current_event->id, cpu, fd, group_leader_fd);
        }
    }

    return 0;
}

static void perf_free(void) {
    struct perf_event *current_event = NULL;

    for(current_event = &perf_events[0]; current_event->id != EV_ID_END; current_event++) {
        free(current_event->fd);
        free(*current_event->group_leader_fd);
    }
}

static int perf_collect() {
    int cpu;
    struct perf_event *current_event = NULL;
    uint64_t value;

    for(current_event = &perf_events[0]; current_event->id != EV_ID_END; current_event++) {
        current_event->updated = 0;
        current_event->value = 0;

        for(cpu = 0; cpu < number_of_cpus; cpu++) {
            if(unlikely(current_event->disabled)) continue;

            ssize_t read_size = read(current_event->fd[cpu], &value, UINT64_SIZE);

            if(likely(read_size == UINT64_SIZE)) {
                current_event->value += value;
                current_event->updated = 1;
            }
            else {
                error("Cannot update value for event %u", current_event->id);
                return 1;
            }
        }
        if(unlikely(debug)) fprintf(stderr, "perf.plugin: successfully read event id = %u, value = %lu\n", current_event->id, current_event->value);
    }

    return 0;
}

static void perf_send_metrics() {
    static int new_chart_generated = 0;

    if(!new_chart_generated) {
        new_chart_generated = 1;

        printf("CHART %s.%s '' 'CPU cycles' 'cycles/s' %s '' line %d %d %s\n"
               , RRD_TYPE_PERF
               , "cpu_cycles"
               , RRD_TYPE_PERF
               , NETDATA_CHART_PRIO_PERF_CPU_CYCLES
               , update_every
               , PLUGIN_PERF_NAME
        );
        printf("DIMENSION %s '' incremental 1 1\n", "cycles");
    }

    printf(
           "BEGIN %s.%s\n"
           , RRD_TYPE_PERF
           , "cpu_cycles"
    );
    printf(
           "SET %s = %lld\n"
           , "cycles"
           , (collected_number) perf_events[EV_ID_CPU_CYCLES].value
    );
    printf("END\n");
}

int main(int argc, char **argv) {

    // ------------------------------------------------------------------------
    // initialization of netdata plugin

    program_name = "perf.plugin";

    // disable syslog
    error_log_syslog = 0;

    // set errors flood protection to 100 logs per hour
    error_log_errors_per_period = 100;
    error_log_throttle_period = 3600;

    // ------------------------------------------------------------------------
    // parse command line parameters

    int i, freq = 0;
    for(i = 1; i < argc ; i++) {
        if(isdigit(*argv[i]) && !freq) {
            int n = str2i(argv[i]);
            if(n > 0 && n < 86400) {
                freq = n;
                continue;
            }
        }
        else if(strcmp("version", argv[i]) == 0 || strcmp("-version", argv[i]) == 0 || strcmp("--version", argv[i]) == 0 || strcmp("-v", argv[i]) == 0 || strcmp("-V", argv[i]) == 0) {
            printf("perf.plugin %s\n", VERSION);
            exit(0);
        }
        else if(strcmp("debug", argv[i]) == 0) {
            debug = 1;
            continue;
        }
        else if(strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
            fprintf(stderr,
                    "\n"
                    " netdata perf.plugin %s\n"
                    " Copyright (C) 2019 Netdata Inc.\n"
                    " Released under GNU General Public License v3 or later.\n"
                    " All rights reserved.\n"
                    "\n"
                    " This program is a data collector plugin for netdata.\n"
                    "\n"
                    " Available command line options:\n"
                    "\n"
                    "  COLLECTION_FREQUENCY    data collection frequency in seconds\n"
                    "                          minimum: %d\n"
                    "\n"
                    "  debug                   enable verbose output\n"
                    "                          default: disabled\n"
                    "\n"
                    "  -v\n"
                    "  -V\n"
                    "  --version               print version and exit\n"
                    "\n"
                    "  -h\n"
                    "  --help                  print this message and exit\n"
                    "\n"
                    " For more information:\n"
                    " https://github.com/netdata/netdata/tree/master/collectors/perf.plugin\n"
                    "\n"
                    , VERSION
                    , update_every
            );
            exit(1);
        }

        error("perf.plugin: ignoring parameter '%s'", argv[i]);
    }

    errno = 0;

    if(freq >= update_every)
        update_every = freq;
    else if(freq)
        error("update frequency %d seconds is too small for PERF. Using %d.", freq, update_every);

    if(debug) fprintf(stderr, "perf.plugin: calling perf_init()\n");
    int perf = !perf_init();

    // ------------------------------------------------------------------------
    // the main loop

    if(debug) fprintf(stderr, "perf.plugin: starting data collection\n");

    time_t started_t = now_monotonic_sec();

    size_t iteration;
    usec_t step = update_every * USEC_PER_SEC;

    heartbeat_t hb;
    heartbeat_init(&hb);
    for(iteration = 0; 1; iteration++) {
        usec_t dt = heartbeat_next(&hb, step);

        if(unlikely(netdata_exit)) break;

        if(debug && iteration)
            fprintf(stderr, "perf.plugin: iteration %zu, dt %llu usec\n"
                    , iteration
                    , dt
            );

        if(likely(perf)) {
            if(debug) fprintf(stderr, "perf.plugin: calling perf_collect()\n");
            perf = !perf_collect();

            if(likely(perf)) {
                if(debug) fprintf(stderr, "perf.plugin: calling perf_send_metrics()\n");
                perf_send_metrics();
            }
        }

        fflush(stdout);

        // restart check (14400 seconds)
        if(now_monotonic_sec() - started_t > 14400) break;
    }

    info("PERF process exiting");
    perf_free();
}

// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../libnetdata/libnetdata.h"

#define PLUGIN_XENSTAT_NAME "xenstat.plugin"

#define NETDATA_CHART_PRIO_XENSTAT_NODE_CPUS              8701
#define NETDATA_CHART_PRIO_XENSTAT_NODE_CPU_FREQ          8702
#define NETDATA_CHART_PRIO_XENSTAT_NODE_MEM               8703
#define NETDATA_CHART_PRIO_XENSTAT_NODE_TMEM              8704
#define NETDATA_CHART_PRIO_XENSTAT_NODE_DOMAINS           8705

#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_CPU             8901
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VCPU            8902
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_MEM             8903

#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_TMEM_PAGES      8904
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_TMEM_OPERATIONS 8905

#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VBD_OO_REQ      8906
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VBD_REQUESTS    8907
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VBD_SECTORS     8908

#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_BYTES       8909
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_PACKETS     8910
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_ERRORS      8911
#define NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_DROPS       8912

#define TYPE_LENGTH_MAX 1024

#define CHART_IS_OBSOLETE     1
#define CHART_IS_NOT_OBSOLETE 0

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

static int debug = 0;

static int netdata_update_every = 1;

#ifdef HAVE_LIBXENSTAT
#include <xenstat.h>
#include <libxl.h>

static xenstat_handle *xhandle = NULL;
static libxl_ctx *ctx = NULL;
static libxl_dominfo info;

struct vcpu_metrics {
    unsigned int id;

    unsigned int online;
    unsigned long long ns;

    int chart_generated;
    int updated;

    struct vcpu_metrics *next;
};

struct tmem_metrics {
    unsigned long long curr_eph_pages;
    unsigned long long succ_eph_gets;
    unsigned long long succ_pers_puts;
    unsigned long long succ_pers_gets;

    int pages_chart_generated;
    int operation_chart_generated;
};

struct vbd_metrics {
    unsigned int id;

    unsigned int error;
    unsigned long long oo_reqs;
    unsigned long long rd_reqs;
    unsigned long long wr_reqs;
    unsigned long long rd_sects;
    unsigned long long wr_sects;

    int oo_req_chart_generated;
    int requests_chart_generated;
    int sectors_chart_generated;
    int updated;

    struct vbd_metrics *next;
};

struct network_metrics {
    unsigned int id;

    unsigned long long rbytes;
    unsigned long long rpackets;
    unsigned long long rerrs;
    unsigned long long rdrops;

    unsigned long long tbytes;
    unsigned long long tpackets;
    unsigned long long terrs;
    unsigned long long tdrops;

    int bytes_chart_generated;
    int packets_chart_generated;
    int errors_chart_generated;
    int drops_chart_generated;
    int updated;

    struct network_metrics *next;
};

struct domain_metrics {
    char *uuid;
    uint32_t hash;

    unsigned int id;
    char *name;

    unsigned long long cpu_ns;
    unsigned long long cur_mem;
    unsigned long long max_mem;

    struct tmem_metrics tmem;
    struct vcpu_metrics *vcpu_root;
    struct vbd_metrics *vbd_root;
    struct network_metrics *network_root;

    int cpu_chart_generated;
    int vcpu_chart_generated;
    int num_vcpus_changed;
    int mem_chart_generated;
    int updated;

    struct domain_metrics *next;
};

struct node_metrics{
    unsigned long long tot_mem;
    unsigned long long free_mem;
    long freeable_mb;
    int num_domains;
    unsigned int num_cpus;
    unsigned long long node_cpu_hz;

    struct domain_metrics *domain_root;
};

static struct node_metrics node_metrics = {
        .domain_root = NULL
};

static inline struct domain_metrics *domain_metrics_get(const char *uuid, uint32_t hash) {
    struct domain_metrics *d = NULL, *last = NULL;
    for(d = node_metrics.domain_root; d ; last = d, d = d->next) {
        if(unlikely(d->hash == hash && !strcmp(d->uuid, uuid)))
            return d;
    }

    d = callocz(1, sizeof(struct domain_metrics));
    d->uuid = strdupz(uuid);
    d->hash = hash;

    if(!last) {
        d->next = node_metrics.domain_root;
        node_metrics.domain_root = d;
    }
    else {
        d->next = last->next;
        last->next = d;
    }

    return d;
}

static void vcpu_metrics_collect(struct domain_metrics *d, xenstat_domain *domain) {
    static unsigned int last_num_vcpus = 0;
    unsigned int num_vcpus = 0;
    xenstat_vcpu *vcpu = NULL;
    struct vcpu_metrics *vcpu_m = NULL, *last_vcpu_m = NULL;

    num_vcpus = xenstat_domain_num_vcpus(domain);
    if(unlikely(num_vcpus != last_num_vcpus)) {
        d->num_vcpus_changed = 1;
        last_num_vcpus = num_vcpus;
    }

    for(vcpu_m = d->vcpu_root; vcpu_m ; vcpu_m = vcpu_m->next)
        vcpu_m->updated = 0;

    vcpu_m = d->vcpu_root;

    unsigned int  i;
    for(i = 0; i < num_vcpus; i++) {
        if(unlikely(!vcpu_m)) {
            vcpu_m = callocz(1, sizeof(struct vcpu_metrics));

            if(i == 0) d->vcpu_root = vcpu_m;
            else last_vcpu_m->next = vcpu_m;
        }

        vcpu_m->id = i;

        vcpu = xenstat_domain_vcpu(domain, i);

        vcpu_m->online = xenstat_vcpu_online(vcpu);
        vcpu_m->ns = xenstat_vcpu_ns(vcpu);

        vcpu_m->updated = 1;

        last_vcpu_m = vcpu_m;
        vcpu_m = vcpu_m->next;
    }
}

static void vbd_metrics_collect(struct domain_metrics *d, xenstat_domain *domain) {
    unsigned int num_vbds = xenstat_domain_num_vbds(domain);
    xenstat_vbd *vbd = NULL;
    struct vbd_metrics *vbd_m = NULL, *last_vbd_m = NULL;

    for(vbd_m = d->vbd_root; vbd_m ; vbd_m = vbd_m->next)
        vbd_m->updated = 0;

    vbd_m = d->vbd_root;

    unsigned int  i;
    for(i = 0; i < num_vbds; i++) {
        if(unlikely(!vbd_m)) {
            vbd_m = callocz(1, sizeof(struct vbd_metrics));

            if(i == 0) d->vbd_root = vbd_m;
            else last_vbd_m->next = vbd_m;
        }

        vbd_m->id = i;

        vbd = xenstat_domain_vbd(domain, i);

        vbd_m->error    = xenstat_vbd_error(vbd);
        vbd_m->oo_reqs  = xenstat_vbd_oo_reqs(vbd);
        vbd_m->rd_reqs  = xenstat_vbd_rd_reqs(vbd);
        vbd_m->wr_reqs  = xenstat_vbd_wr_reqs(vbd);
        vbd_m->rd_sects = xenstat_vbd_rd_sects(vbd);
        vbd_m->wr_sects = xenstat_vbd_wr_sects(vbd);

        vbd_m->updated = 1;

        last_vbd_m = vbd_m;
        vbd_m = vbd_m->next;
    }
}

static void network_metrics_collect(struct domain_metrics *d, xenstat_domain *domain) {
    unsigned int num_networks = xenstat_domain_num_networks(domain);
    xenstat_network *network = NULL;
    struct network_metrics *network_m = NULL, *last_network_m = NULL;

    for(network_m = d->network_root; network_m ; network_m = network_m->next)
        network_m->updated = 0;

    network_m = d->network_root;

    unsigned int  i;
    for(i = 0; i < num_networks; i++) {
        if(unlikely(!network_m)) {
            network_m = callocz(1, sizeof(struct network_metrics));

            if(i == 0) d->network_root = network_m;
            else last_network_m->next = network_m;
        }

        network_m->id = i;

        network = xenstat_domain_network(domain, i);

        network_m->rbytes   = xenstat_network_rbytes(network);
        network_m->rpackets = xenstat_network_rpackets(network);
        network_m->rerrs    = xenstat_network_rerrs(network);
        network_m->rdrops   = xenstat_network_rdrop(network);

        network_m->tbytes   = xenstat_network_tbytes(network);
        network_m->tpackets = xenstat_network_tpackets(network);
        network_m->terrs    = xenstat_network_terrs(network);
        network_m->tdrops   = xenstat_network_tdrop(network);

        network_m->updated = 1;

        last_network_m = network_m;
        network_m = network_m->next;
    }
}

static int xenstat_collect() {
    static xenstat_node *node = NULL;

    // mark all old metrics as not-updated
    struct domain_metrics *d;
    for(d = node_metrics.domain_root; d ; d = d->next)
        d->updated = 0;

    if (likely(node))
        xenstat_free_node(node);
    node = xenstat_get_node(xhandle, XENSTAT_ALL);
    if (unlikely(!node)) {
        printf("XENSTAT: failed to retrieve statistics from libxenstat\n");
        return 1;
    }

    node_metrics.tot_mem = xenstat_node_tot_mem(node);
    node_metrics.free_mem = xenstat_node_free_mem(node);
    node_metrics.freeable_mb = xenstat_node_freeable_mb(node);
    node_metrics.num_domains = xenstat_node_num_domains(node);
    node_metrics.num_cpus = xenstat_node_num_cpus(node);
    node_metrics.node_cpu_hz = xenstat_node_cpu_hz(node);

    int i;
    for(i = 0; i < node_metrics.num_domains; i++) {
        xenstat_domain *domain = NULL;
        char uuid[LIBXL_UUID_FMTLEN + 1];

        domain = xenstat_node_domain_by_index(node, i);

        // get domain UUID
        unsigned int id = xenstat_domain_id(domain);
        if(libxl_domain_info(ctx, &info, id)) {
            error("XENSTAT: cannot get domain info.");
        }
        else {
            snprintfz(uuid, LIBXL_UUID_FMTLEN, LIBXL_UUID_FMT "\n", LIBXL_UUID_BYTES(info.uuid));
        }

        uint32_t hash = simple_hash(uuid);
        d = domain_metrics_get(uuid, hash);

        d->id = id;
        d->name = xenstat_domain_name(domain);
        netdata_fix_chart_id(d->name);
        d->cpu_ns = xenstat_domain_cpu_ns(domain);
        d->cur_mem = xenstat_domain_cur_mem(domain);
        d->max_mem = xenstat_domain_max_mem(domain);

        xenstat_tmem *tmem = xenstat_domain_tmem(domain);
        d->tmem.curr_eph_pages = xenstat_tmem_curr_eph_pages(tmem);
        d->tmem.succ_eph_gets = xenstat_tmem_succ_eph_gets(tmem);
        d->tmem.succ_pers_puts = xenstat_tmem_succ_pers_puts(tmem);
        d->tmem.succ_pers_gets = xenstat_tmem_succ_pers_gets(tmem);

        vcpu_metrics_collect(d, domain);
        vbd_metrics_collect(d, domain);
        network_metrics_collect(d, domain);

        d->updated = 1;
    }

    return 0;
}

static void xenstat_send_node_metrics() {
    static int mem_chart_generated = 0, tmem_chart_generated = 0, domains_chart_generated = 0, cpus_chart_generated = 0, cpu_freq_chart_generated = 0;

    // ----------------------------------------------------------------

    if(!mem_chart_generated) {
        mem_chart_generated = 1;
        printf("CHART xenstat.mem '' 'Memory Usage' 'MiB' 'memory' '' stacked %d %d '' %s\n"
               , NETDATA_CHART_PRIO_XENSTAT_NODE_MEM
               , netdata_update_every
               , PLUGIN_XENSTAT_NAME
        );
        printf("DIMENSION %s '' absolute 1 %d\n", "free", netdata_update_every * 1024 * 1024);
        printf("DIMENSION %s '' absolute 1 %d\n", "used", netdata_update_every * 1024 * 1024);
    }

    printf(
            "BEGIN xenstat.mem\n"
            "SET free = %lld\n"
            "SET used = %lld\n"
            "END\n"
            , (collected_number) node_metrics.free_mem
            , (collected_number) (node_metrics.tot_mem - node_metrics.free_mem)
    );

    // ----------------------------------------------------------------

    if(!tmem_chart_generated) {
        tmem_chart_generated = 1;
        printf("CHART xenstat.tmem '' 'Freeable Transcedent Memory' 'MiB' 'memory' '' line %d %d '' %s\n"
               , NETDATA_CHART_PRIO_XENSTAT_NODE_TMEM
               , netdata_update_every
               , PLUGIN_XENSTAT_NAME
        );
        printf("DIMENSION %s '' absolute 1 %d\n", "freeable", netdata_update_every * 1024 * 1024);
    }

    printf(
            "BEGIN xenstat.tmem\n"
            "SET freeable = %lld\n"
            "END\n"
            , (collected_number) node_metrics.freeable_mb
    );

    // ----------------------------------------------------------------

    if(!domains_chart_generated) {
        domains_chart_generated = 1;
        printf("CHART xenstat.domains '' 'Number of Domains' 'domains' 'domains' '' line %d %d '' %s\n"
               , NETDATA_CHART_PRIO_XENSTAT_NODE_DOMAINS
               , netdata_update_every
               , PLUGIN_XENSTAT_NAME
        );
        printf("DIMENSION %s '' absolute 1 %d\n", "domains", netdata_update_every);
    }

    printf(
            "BEGIN xenstat.domains\n"
            "SET domains = %lld\n"
            "END\n"
            , (collected_number) node_metrics.num_domains
    );

    // ----------------------------------------------------------------

    if(!cpus_chart_generated) {
        cpus_chart_generated = 1;
        printf("CHART xenstat.cpus '' 'Number of CPUs' 'cpus' 'cpu' '' line %d %d '' %s\n"
               , NETDATA_CHART_PRIO_XENSTAT_NODE_CPUS
               , netdata_update_every
               , PLUGIN_XENSTAT_NAME
        );
        printf("DIMENSION %s '' absolute 1 %d\n", "cpus", netdata_update_every);
    }

    printf(
            "BEGIN xenstat.cpus\n"
            "SET cpus = %lld\n"
            "END\n"
            , (collected_number) node_metrics.num_cpus
    );

    // ----------------------------------------------------------------

    if(!cpu_freq_chart_generated) {
        cpu_freq_chart_generated = 1;
        printf("CHART xenstat.cpu_freq '' 'CPU Frequency' 'MHz' 'cpu' '' line %d %d '' %s\n"
               , NETDATA_CHART_PRIO_XENSTAT_NODE_CPU_FREQ
               , netdata_update_every
               , PLUGIN_XENSTAT_NAME
        );
        printf("DIMENSION %s '' absolute 1 %d\n", "frequency", netdata_update_every * 1024 * 1024);
    }

    printf(
            "BEGIN xenstat.cpu_freq\n"
            "SET frequency = %lld\n"
            "END\n"
            , (collected_number) node_metrics.node_cpu_hz
    );
}

static void print_domain_cpu_chart_definition(char *type, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_cpu '' 'CPU Usage (100%% = 1 core)' 'percentage' 'cpu' '' line %d %d %s %s\n"
                       , type
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_CPU
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION usage '' incremental 100 %d\n", netdata_update_every * 1000000000);
}

static void print_domain_mem_chart_definition(char *type, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_mem '' 'Memory Reservation' 'MiB' 'memory' '' line %d %d %s %s\n"
                       , type
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_MEM
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION maximum '' absolute 1 %d\n", netdata_update_every * 1024 * 1024);
    printf("DIMENSION current '' absolute 1 %d\n", netdata_update_every * 1024 * 1024);
}

static void print_domain_tmem_pages_chart_definition(char *type, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_tmem_pages '' 'Current Number of Transcedent Memory Ephemeral Pages' 'pages' 'memory' '' line %d %d %s %s\n"
                       , type
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_TMEM_PAGES
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION pages '' absolute 1 %d\n", netdata_update_every);
}

static void print_domain_tmem_operations_chart_definition(char *type, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_tmem_operations '' 'Successful Transcedent Memory Puts and Gets' 'events/s' 'memory' '' line %d %d %s %s\n"
                       , type
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_TMEM_OPERATIONS
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION ephemeral_gets 'ephemeral gets' incremental 1 %d\n", netdata_update_every);
    printf("DIMENSION persistent_puts 'persistent puts' incremental 1 %d\n", netdata_update_every);
    printf("DIMENSION persistent_gets 'persistent gets' incremental 1 %d\n", netdata_update_every);
}

static void print_domain_vcpu_chart_definition(char *type, struct domain_metrics *d, int obsolete_flag) {
    struct vcpu_metrics *vcpu_m;

    printf("CHART %s.xenstat_domain_vcpu '' 'VCPU Usage' 'percentage' 'cpu' '' line %d %d %s %s\n"
                       , type
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VCPU
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );

    for(vcpu_m = d->vcpu_root; vcpu_m; vcpu_m = vcpu_m->next) {
        if(likely(vcpu_m->updated && vcpu_m->online)) {
            printf("DIMENSION vcpu%u '' incremental 100 %d\n", vcpu_m->id, netdata_update_every * 1000000000);
        }
    }
}

static void print_domain_vbd_oo_chart_definition(char *type, unsigned int vbd, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_oo_req_vbd%u '' 'VBD%u \"Out Of\" Requests' 'requests/s' 'vbd' '' line %d %d %s %s\n"
                       , type
                       , vbd
                       , vbd
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VBD_OO_REQ + vbd
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION requests '' incremental 1 %d\n", netdata_update_every);
}

static void print_domain_vbd_requests_chart_definition(char *type, unsigned int vbd, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_requests_vbd%u '' 'VBD%u Requests' 'requests/s' 'vbd' '' line %d %d %s %s\n"
                       , type
                       , vbd
                       , vbd
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VBD_REQUESTS + vbd
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION read '' incremental 1 %d\n", netdata_update_every);
    printf("DIMENSION write '' incremental 1 %d\n", netdata_update_every);
}

static void print_domain_vbd_sectors_chart_definition(char *type, unsigned int vbd, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_sectors_vbd%u '' 'VBD%u Read/Written Sectors' 'sectors/s' 'vbd' '' line %d %d %s %s\n"
                       , type
                       , vbd
                       , vbd
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_VBD_SECTORS + vbd
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION read '' incremental 1 %d\n", netdata_update_every);
    printf("DIMENSION write '' incremental 1 %d\n", netdata_update_every);
}

static void print_domain_network_bytes_chart_definition(char *type, unsigned int network, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_bytes_network%u '' 'Network%u Received/Sent Bytes' 'kilobits/s' 'network' '' line %d %d %s %s\n"
                       , type
                       , network
                       , network
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_BYTES + network
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION received '' incremental 8 %d\n", netdata_update_every * 1000);
    printf("DIMENSION sent '' incremental 8 %d\n", netdata_update_every * 1000);
}

static void print_domain_network_packets_chart_definition(char *type, unsigned int network, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_packets_network%u '' 'Network%u Recieved/Sent Packets' 'packets/s' 'network' '' line %d %d %s %s\n"
                       , type
                       , network
                       , network
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_PACKETS + network
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION received '' incremental 1 %d\n", netdata_update_every);
    printf("DIMENSION sent '' incremental 1 %d\n", netdata_update_every);
}

static void print_domain_network_errors_chart_definition(char *type, unsigned int network, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_errors_network%u '' 'Network%u Receive/Transmit Errors' 'errors/s' 'network' '' line %d %d %s %s\n"
                       , type
                       , network
                       , network
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_PACKETS + network
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION received '' incremental 1 %d\n", netdata_update_every);
    printf("DIMENSION sent '' incremental 1 %d\n", netdata_update_every);
}

static void print_domain_network_drops_chart_definition(char *type, unsigned int network, int obsolete_flag) {
    printf("CHART %s.xenstat_domain_drops_network%u '' 'Network%u Recieve/Transmit Drops' 'drops/s' 'network' '' line %d %d %s %s\n"
                       , type
                       , network
                       , network
                       , NETDATA_CHART_PRIO_XENSTAT_DOMAIN_NET_PACKETS + network
                       , netdata_update_every
                       , obsolete_flag ? "obsolete": "''"
                       , PLUGIN_XENSTAT_NAME
    );
    printf("DIMENSION received '' incremental 1 %d\n", netdata_update_every);
    printf("DIMENSION sent '' incremental 1 %d\n", netdata_update_every);
}

static void xenstat_send_domain_metrics() {

    if(!node_metrics.domain_root) return;
    struct domain_metrics *d;

    for(d = node_metrics.domain_root; d; d = d->next) {
        char type[TYPE_LENGTH_MAX + 1];
        snprintfz(type, TYPE_LENGTH_MAX, "xendomain_%s_%s", d->name, d->uuid);

        if(likely(d->updated)) {

            // ----------------------------------------------------------------

            if(!d->cpu_chart_generated) {
                d->cpu_chart_generated = 1;
                print_domain_cpu_chart_definition(type, CHART_IS_NOT_OBSOLETE);
            }
            printf(
                    "BEGIN %s.xenstat_domain_cpu\n"
                    "SET usage = %lld\n"
                    "END\n"
                    , type
                    , (collected_number)d->cpu_ns
            );

            // ----------------------------------------------------------------

            struct vcpu_metrics *vcpu_m;

            if(!d->vcpu_chart_generated || d->num_vcpus_changed) {
                d->vcpu_chart_generated = 1;
                d->num_vcpus_changed = 0;
                print_domain_vcpu_chart_definition(type, d, CHART_IS_NOT_OBSOLETE);
            }

            printf("BEGIN %s.xenstat_domain_vcpu\n", type);
            for(vcpu_m = d->vcpu_root; vcpu_m; vcpu_m = vcpu_m->next) {
                if(likely(vcpu_m->updated && vcpu_m->online)) {
                    printf(
                            "SET vcpu%u = %lld\n"
                            , vcpu_m->id
                            , (collected_number)vcpu_m->ns
                    );
                }
            }
            printf("END\n");

            // ----------------------------------------------------------------

            if(!d->mem_chart_generated) {
                d->mem_chart_generated = 1;
                print_domain_mem_chart_definition(type, CHART_IS_NOT_OBSOLETE);
            }
            printf(
                    "BEGIN %s.xenstat_domain_mem\n"
                    "SET maximum = %lld\n"
                    "SET current = %lld\n"
                    "END\n"
                    , type
                    , (collected_number)d->max_mem
                    , (collected_number)d->cur_mem
            );

            // ----------------------------------------------------------------

            if(!d->tmem.pages_chart_generated) {
                d->tmem.pages_chart_generated = 1;
                print_domain_tmem_pages_chart_definition(type, CHART_IS_NOT_OBSOLETE);
            }
            printf(
                    "BEGIN %s.xenstat_domain_tmem_pages\n"
                    "SET pages = %lld\n"
                    "END\n"
                    , type
                    , (collected_number)d->tmem.curr_eph_pages
            );

            // ----------------------------------------------------------------

            if(!d->tmem.operation_chart_generated) {
                d->tmem.operation_chart_generated = 1;
                print_domain_tmem_operations_chart_definition(type, CHART_IS_NOT_OBSOLETE);
            }
            printf(
                    "BEGIN %s.xenstat_domain_tmem_operations\n"
                    "SET ephemeral_gets = %lld\n"
                    "SET persistent_puts = %lld\n"
                    "SET persistent_gets = %lld\n"
                    "END\n"
                    , type
                    , (collected_number)d->tmem.succ_eph_gets
                    , (collected_number)d->tmem.succ_pers_puts
                    , (collected_number)d->tmem.succ_eph_gets
            );

            // ----------------------------------------------------------------

            struct vbd_metrics *vbd_m;
            for(vbd_m = d->vbd_root; vbd_m; vbd_m = vbd_m->next) {
                if(likely(vbd_m->updated && !vbd_m->error)) {
                    if(!vbd_m->oo_req_chart_generated) {
                        vbd_m->oo_req_chart_generated = 1;
                        print_domain_vbd_oo_chart_definition(type, vbd_m->id, CHART_IS_NOT_OBSOLETE);
                    }
                    printf(
                            "BEGIN %s.xenstat_domain_oo_req_vbd%u\n"
                            "SET requests = %lld\n"
                            "END\n"
                            , type
                            , vbd_m->id
                            , (collected_number)vbd_m->oo_reqs
                    );

                    // ----------------------------------------------------------------

                    if(!vbd_m->requests_chart_generated) {
                        vbd_m->requests_chart_generated = 1;
                        print_domain_vbd_requests_chart_definition(type, vbd_m->id, CHART_IS_NOT_OBSOLETE);
                    }
                    printf(
                            "BEGIN %s.xenstat_domain_requests_vbd%u\n"
                            "SET read = %lld\n"
                            "SET write = %lld\n"
                            "END\n"
                            , type
                            , vbd_m->id
                            , (collected_number)vbd_m->rd_reqs
                            , (collected_number)vbd_m->wr_reqs
                    );

                    // ----------------------------------------------------------------

                    if(!vbd_m->sectors_chart_generated) {
                        vbd_m->sectors_chart_generated = 1;
                        print_domain_vbd_sectors_chart_definition(type, vbd_m->id, CHART_IS_NOT_OBSOLETE);
                    }
                    printf(
                            "BEGIN %s.xenstat_domain_sectors_vbd%u\n"
                            "SET read = %lld\n"
                            "SET write = %lld\n"
                            "END\n"
                            , type
                            , vbd_m->id
                            , (collected_number)vbd_m->rd_sects
                            , (collected_number)vbd_m->wr_sects
                    );
                }
                else {
                    print_domain_vbd_oo_chart_definition(type, vbd_m->id, CHART_IS_OBSOLETE);
                    print_domain_vbd_requests_chart_definition(type, vbd_m->id, CHART_IS_OBSOLETE);
                    print_domain_vbd_sectors_chart_definition(type, vbd_m->id, CHART_IS_OBSOLETE);
                    vbd_m->oo_req_chart_generated = 0;
                    vbd_m->requests_chart_generated = 0;
                    vbd_m->sectors_chart_generated = 0;
                }
            }

            // ----------------------------------------------------------------

            struct network_metrics *network_m;
            for(network_m = d->network_root; network_m; network_m = network_m->next) {
                if(likely(network_m->updated)) {
                    if(!network_m->bytes_chart_generated) {
                        network_m->bytes_chart_generated = 1;
                        print_domain_network_bytes_chart_definition(type, network_m->id, CHART_IS_NOT_OBSOLETE);
                    }
                    printf(
                            "BEGIN %s.xenstat_domain_bytes_network%u\n"
                            "SET recieved = %lld\n"
                            "SET sent = %lld\n"
                            "END\n"
                            , type
                            , network_m->id
                            , (collected_number)network_m->rbytes
                            , (collected_number)network_m->tbytes
                    );

                    // ----------------------------------------------------------------

                    if(!network_m->packets_chart_generated) {
                        network_m->packets_chart_generated = 1;
                        print_domain_network_packets_chart_definition(type, network_m->id, CHART_IS_NOT_OBSOLETE);
                    }
                    printf(
                            "BEGIN %s.xenstat_domain_packets_network%u\n"
                            "SET recieved = %lld\n"
                            "SET sent = %lld\n"
                            "END\n"
                            , type
                            , network_m->id
                            , (collected_number)network_m->rpackets
                            , (collected_number)network_m->tpackets
                    );

                    // ----------------------------------------------------------------

                    if(!network_m->errors_chart_generated) {
                        network_m->errors_chart_generated = 1;
                        print_domain_network_errors_chart_definition(type, network_m->id, CHART_IS_NOT_OBSOLETE);
                    }
                    printf(
                            "BEGIN %s.xenstat_domain_errors_network%u\n"
                            "SET recieved = %lld\n"
                            "SET sent = %lld\n"
                            "END\n"
                            , type
                            , network_m->id
                            , (collected_number)network_m->rerrs
                            , (collected_number)network_m->terrs
                    );

                    // ----------------------------------------------------------------

                    if(!network_m->drops_chart_generated) {
                        network_m->drops_chart_generated = 1;
                        print_domain_network_drops_chart_definition(type, network_m->id, CHART_IS_NOT_OBSOLETE);
                    }
                    printf(
                            "BEGIN %s.xenstat_domain_drops_network%u\n"
                            "SET recieved = %lld\n"
                            "SET sent = %lld\n"
                            "END\n"
                            , type
                            , network_m->id
                            , (collected_number)network_m->rdrops
                            , (collected_number)network_m->tdrops
                    );
                }
                else {
                    print_domain_network_bytes_chart_definition(type, network_m->id, CHART_IS_OBSOLETE);
                    print_domain_network_packets_chart_definition(type, network_m->id, CHART_IS_OBSOLETE);
                    print_domain_network_errors_chart_definition(type, network_m->id, CHART_IS_OBSOLETE);
                    print_domain_network_drops_chart_definition(type, network_m->id, CHART_IS_OBSOLETE);
                    network_m->bytes_chart_generated   = 0;
                    network_m->packets_chart_generated = 0;
                    network_m->errors_chart_generated  = 0;
                    network_m->drops_chart_generated   = 0;
                }
            }
        }
        else{
            print_domain_cpu_chart_definition(type, CHART_IS_OBSOLETE);
            print_domain_vcpu_chart_definition(type, d, CHART_IS_OBSOLETE);
            print_domain_mem_chart_definition(type, CHART_IS_OBSOLETE);
            print_domain_tmem_pages_chart_definition(type, CHART_IS_OBSOLETE);
            print_domain_tmem_operations_chart_definition(type, CHART_IS_OBSOLETE);

            d->cpu_chart_generated = 0;
            d->vcpu_chart_generated = 0;
            d->mem_chart_generated = 0;
            d->tmem.pages_chart_generated = 0;
            d->tmem.operation_chart_generated = 0;
        }
    }
}

int main(int argc, char **argv) {

    // ------------------------------------------------------------------------
    // initialization of netdata plugin

    program_name = "xenstat.plugin";

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
            printf("xenstat.plugin %s\n", VERSION);
            exit(0);
        }
        else if(strcmp("debug", argv[i]) == 0) {
            debug = 1;
            continue;
        }
        else if(strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
            fprintf(stderr,
                    "\n"
                    " netdata xenstat.plugin %s\n"
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
                    " https://github.com/netdata/netdata/tree/master/collectors/xenstat.plugin\n"
                    "\n"
                    , VERSION
                    , netdata_update_every
            );
            exit(1);
        }

        error("xenstat.plugin: ignoring parameter '%s'", argv[i]);
    }

    errno = 0;

    if(freq >= netdata_update_every)
        netdata_update_every = freq;
    else if(freq)
        error("update frequency %d seconds is too small for XENSTAT. Using %d.", freq, netdata_update_every);

    // ------------------------------------------------------------------------
    // initialize xen API handles

    if(debug) fprintf(stderr, "xenstat.plugin: calling xenstat_init()\n");
    xhandle = xenstat_init();
    if (xhandle == NULL)
        error("XENSTAT: failed to initialize xenstat library.");

    if(debug) fprintf(stderr, "xenstat.plugin: calling libxl_ctx_alloc()\n");
    if (libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, NULL)) {
        error("XENSTAT: failed to initialize xl context.");
    }
    libxl_dominfo_init(&info);

    // ------------------------------------------------------------------------
    // the main loop

    if(debug) fprintf(stderr, "xenstat.plugin: starting data collection\n");

    time_t started_t = now_monotonic_sec();

    size_t iteration;
    usec_t step = netdata_update_every * USEC_PER_SEC;

    heartbeat_t hb;
    heartbeat_init(&hb);
    for(iteration = 0; 1; iteration++) {
        usec_t dt = heartbeat_next(&hb, step);

        if(unlikely(netdata_exit)) break;

        if(debug && iteration)
            fprintf(stderr, "xenstat.plugin: iteration %zu, dt %llu usec\n"
                    , iteration
                    , dt
            );

        if(likely(xhandle)) {
            if(debug) fprintf(stderr, "xenstat.plugin: calling xenstat_collect()\n");
            int ret = xenstat_collect();

            if(likely(!ret)) {
                if(debug) fprintf(stderr, "xenstat.plugin: calling xenstat_send_node_metrics()\n");
                xenstat_send_node_metrics();
                if(debug) fprintf(stderr, "xenstat.plugin: calling xenstat_send_domain_metrics()\n");
                xenstat_send_domain_metrics();
            }
        }

        fflush(stdout);

        // restart check (14400 seconds)
        if(now_monotonic_sec() - started_t > 14400) break;
    }

    info("XENSTAT process exiting");
}

#else // !HAVE_LIBXENSTAT

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    fatal("xenstat.plugin is not compiled.");
}

#endif // !HAVE_LIBXENSTAT

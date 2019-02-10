// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../libnetdata/libnetdata.h"

#define PLUGIN_NFACCT_NAME "nfacct.plugin"

#ifdef HAVE_LIBMNL
#include <libmnl/libmnl.h>

static inline size_t mnl_buffer_size() {
    long s = MNL_SOCKET_BUFFER_SIZE;
    if(s <= 0) return 8192;
    return (size_t)s;
}

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
static int netdata_priority = 100004;

// ----------------------------------------------------------------------------
// DO_NFSTAT - collect netfilter connection tracker statistics via netlink
// example: https://github.com/formorer/pkg-conntrack-tools/blob/master/src/conntrack.c

#ifdef HAVE_LINUX_NETFILTER_NFNETLINK_CONNTRACK_H
#define DO_NFSTAT 1

#define RRD_TYPE_NET_STAT_NETFILTER "netfilter"
#define RRD_TYPE_NET_STAT_CONNTRACK "netlink"

#include <linux/netfilter/nfnetlink_conntrack.h>

static struct {
    int update_every;
    char *buf;
    size_t buf_size;
    struct mnl_socket *mnl;
    struct nlmsghdr *nlh;
    struct nfgenmsg *nfh;
    unsigned int seq;
    uint32_t portid;

    struct nlattr *tb[CTA_STATS_MAX+1];
    const char *attr2name[CTA_STATS_MAX+1];
    kernel_uint_t metrics[CTA_STATS_MAX+1];

    struct nlattr *tb_exp[CTA_STATS_EXP_MAX+1];
    const char *attr2name_exp[CTA_STATS_EXP_MAX+1];
    kernel_uint_t metrics_exp[CTA_STATS_EXP_MAX+1];
} nfstat_root = {
        .update_every = 1,
        .buf = NULL,
        .buf_size = 0,
        .mnl = NULL,
        .nlh = NULL,
        .nfh = NULL,
        .seq = 0,
        .portid = 0,
        .tb = {},
        .attr2name = {
                [CTA_STATS_SEARCHED]	   = "searched",
                [CTA_STATS_FOUND]	       = "found",
                [CTA_STATS_NEW]		       = "new",
                [CTA_STATS_INVALID]	       = "invalid",
                [CTA_STATS_IGNORE]	       = "ignore",
                [CTA_STATS_DELETE]	       = "delete",
                [CTA_STATS_DELETE_LIST]	   = "delete_list",
                [CTA_STATS_INSERT]	       = "insert",
                [CTA_STATS_INSERT_FAILED]  = "insert_failed",
                [CTA_STATS_DROP]	       = "drop",
                [CTA_STATS_EARLY_DROP]	   = "early_drop",
                [CTA_STATS_ERROR]	       = "icmp_error",
                [CTA_STATS_SEARCH_RESTART] = "search_restart",
        },
        .metrics = {},
        .tb_exp = {},
        .attr2name_exp = {
                [CTA_STATS_EXP_NEW]	       = "new",
                [CTA_STATS_EXP_CREATE]	   = "created",
                [CTA_STATS_EXP_DELETE]	   = "deleted",
        },
        .metrics_exp = {}
};


static int nfstat_init(int netdata_update_every) {
    nfstat_root.update_every = netdata_update_every;

    nfstat_root.buf_size = mnl_buffer_size();
    nfstat_root.buf = mallocz(nfstat_root.buf_size);

    nfstat_root.mnl  = mnl_socket_open(NETLINK_NETFILTER);
    if(!nfstat_root.mnl) {
        error("NFSTAT: mnl_socket_open() failed");
        return 1;
    }

    nfstat_root.seq = (unsigned int)now_realtime_sec() - 1;

    if(mnl_socket_bind(nfstat_root.mnl, 0, MNL_SOCKET_AUTOPID) < 0) {
        error("NFSTAT: mnl_socket_bind() failed");
        return 1;
    }
    nfstat_root.portid = mnl_socket_get_portid(nfstat_root.mnl);

    return 0;
}

static void nfstat_cleanup() {
    if(nfstat_root.mnl) {
        mnl_socket_close(nfstat_root.mnl);
        nfstat_root.mnl = NULL;
    }

    freez(nfstat_root.buf);
    nfstat_root.buf = NULL;
    nfstat_root.buf_size = 0;
}

static struct nlmsghdr * nfct_mnl_nlmsghdr_put(char *buf, uint16_t subsys, uint16_t type, uint8_t family, uint32_t seq) {
    struct nlmsghdr *nlh;
    struct nfgenmsg *nfh;

    nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_type = (subsys << 8) | type;
    nlh->nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
    nlh->nlmsg_seq = seq;

    nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
    nfh->nfgen_family = family;
    nfh->version = NFNETLINK_V0;
    nfh->res_id = 0;

    return nlh;
}

static int nfct_stats_attr_cb(const struct nlattr *attr, void *data) {
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_STATS_MAX) < 0)
        return MNL_CB_OK;

    if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
        error("NFSTAT: mnl_attr_validate() failed");
        return MNL_CB_ERROR;
    }

    tb[type] = attr;
    return MNL_CB_OK;
}

static int nfstat_callback(const struct nlmsghdr *nlh, void *data) {
    (void)data;

    struct nfgenmsg *nfg = mnl_nlmsg_get_payload(nlh);

    mnl_attr_parse(nlh, sizeof(*nfg), nfct_stats_attr_cb, nfstat_root.tb);

    // printf("cpu=%-4u\t", ntohs(nfg->res_id));

    int i;
    // add the metrics of this CPU into the metrics
    for (i = 0; i < CTA_STATS_MAX+1; i++) {
        if (nfstat_root.tb[i]) {
            // printf("%s=%u ", nfstat_root.attr2name[i], ntohl(mnl_attr_get_u32(nfstat_root.tb[i])));
            nfstat_root.metrics[i] += ntohl(mnl_attr_get_u32(nfstat_root.tb[i]));
        }
    }
    // printf("\n");

    return MNL_CB_OK;
}

static int nfstat_collect_conntrack() {
    // zero all metrics - we will sum the metrics of all CPUs later
    int i;
    for (i = 0; i < CTA_STATS_MAX+1; i++)
        nfstat_root.metrics[i] = 0;

    // prepare the request
    nfstat_root.nlh = nfct_mnl_nlmsghdr_put(nfstat_root.buf, NFNL_SUBSYS_CTNETLINK, IPCTNL_MSG_CT_GET_STATS_CPU, AF_UNSPEC, nfstat_root.seq);

    // send the request
    if(mnl_socket_sendto(nfstat_root.mnl, nfstat_root.nlh, nfstat_root.nlh->nlmsg_len) < 0) {
        error("NFSTAT: mnl_socket_sendto() failed");
        return 1;
    }

    // get the reply
    ssize_t ret;
    while ((ret = mnl_socket_recvfrom(nfstat_root.mnl, nfstat_root.buf, nfstat_root.buf_size)) > 0) {
        if(mnl_cb_run(
                nfstat_root.buf
                , (size_t)ret
                , nfstat_root.nlh->nlmsg_seq
                , nfstat_root.portid
                , nfstat_callback
                , NULL
        ) <= MNL_CB_STOP)
            break;
    }

    // verify we run without issues
    if (ret == -1) {
        error("NFSTAT: error communicating with kernel. This plugin can only work when netdata runs as root.");
        return 1;
    }

    return 0;
}

static int nfexp_stats_attr_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_STATS_EXP_MAX) < 0)
        return MNL_CB_OK;

    if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
        error("NFSTAT EXP: mnl_attr_validate() failed");
        return MNL_CB_ERROR;
    }

    tb[type] = attr;
    return MNL_CB_OK;
}

static int nfstat_callback_exp(const struct nlmsghdr *nlh, void *data) {
    (void)data;

    struct nfgenmsg *nfg = mnl_nlmsg_get_payload(nlh);

    mnl_attr_parse(nlh, sizeof(*nfg), nfexp_stats_attr_cb, nfstat_root.tb_exp);

    int i;
    for (i = 0; i < CTA_STATS_EXP_MAX+1; i++) {
        if (nfstat_root.tb_exp[i]) {
            nfstat_root.metrics_exp[i] += ntohl(mnl_attr_get_u32(nfstat_root.tb_exp[i]));
        }
    }

    return MNL_CB_OK;
}

static int nfstat_collect_conntrack_expectations() {
    // zero all metrics - we will sum the metrics of all CPUs later
    int i;
    for (i = 0; i < CTA_STATS_EXP_MAX+1; i++)
        nfstat_root.metrics_exp[i] = 0;

    // prepare the request
    nfstat_root.nlh = nfct_mnl_nlmsghdr_put(nfstat_root.buf, NFNL_SUBSYS_CTNETLINK_EXP, IPCTNL_MSG_EXP_GET_STATS_CPU, AF_UNSPEC, nfstat_root.seq);

    // send the request
    if(mnl_socket_sendto(nfstat_root.mnl, nfstat_root.nlh, nfstat_root.nlh->nlmsg_len) < 0) {
        error("NFSTAT: mnl_socket_sendto() failed");
        return 1;
    }

    // get the reply
    ssize_t ret;
    while ((ret = mnl_socket_recvfrom(nfstat_root.mnl, nfstat_root.buf, nfstat_root.buf_size)) > 0) {
        if(mnl_cb_run(
                nfstat_root.buf
                , (size_t)ret
                , nfstat_root.nlh->nlmsg_seq
                , nfstat_root.portid
                , nfstat_callback_exp
                , NULL
        ) <= MNL_CB_STOP)
            break;
    }

    // verify we run without issues
    if (ret == -1) {
        error("NFSTAT: error communicating with kernel. This plugin can only work when netdata runs as root.");
        return 1;
    }

    return 0;
}

static int nfstat_collect() {
    nfstat_root.seq++;

    if(nfstat_collect_conntrack())
        return 1;

    if(nfstat_collect_conntrack_expectations())
        return 1;

    return 0;
}

static void nfstat_send_metrics() {

    {
        // TODO: define the chart and set dimensions
        // static RRDSET *st_new = NULL;
        // static RRDDIM *rd_new = NULL, *rd_ignore = NULL, *rd_invalid = NULL;

        // if(!st_new) {
        //     st_new = rrdset_create_localhost(
        //             RRD_TYPE_NET_STAT_NETFILTER
        //             , RRD_TYPE_NET_STAT_CONNTRACK "_new"
        //             , NULL
        //             , RRD_TYPE_NET_STAT_CONNTRACK
        //             , NULL
        //             , "Connection Tracker New Connections"
        //             , "connections/s"
        //             , PLUGIN_NFACCT_NAME
        //             , NULL
        //             , NETDATA_CHART_PRIO_NETFILTER_NEW
        //             , nfstat_root.update_every
        //             , RRDSET_TYPE_LINE
        //     );

        //     rd_new     = rrddim_add(st_new, nfstat_root.attr2name[CTA_STATS_NEW], NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_ignore  = rrddim_add(st_new, nfstat_root.attr2name[CTA_STATS_IGNORE], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_invalid = rrddim_add(st_new, nfstat_root.attr2name[CTA_STATS_INVALID], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        // }
        // else
        //     rrdset_next(st_new);

        // rrddim_set_by_pointer(st_new, rd_new, (collected_number) nfstat_root.metrics[CTA_STATS_NEW]);
        // rrddim_set_by_pointer(st_new, rd_ignore, (collected_number) nfstat_root.metrics[CTA_STATS_IGNORE]);
        // rrddim_set_by_pointer(st_new, rd_invalid, (collected_number) nfstat_root.metrics[CTA_STATS_INVALID]);

        // rrdset_done(st_new);
    }

    // ----------------------------------------------------------------

    {
        // TODO: define the chart and set dimensions
        // static RRDSET *st_changes = NULL;
        // static RRDDIM *rd_inserted = NULL, *rd_deleted = NULL, *rd_delete_list = NULL;

        // if(!st_changes) {
        //     st_changes = rrdset_create_localhost(
        //             RRD_TYPE_NET_STAT_NETFILTER
        //             , RRD_TYPE_NET_STAT_CONNTRACK "_changes"
        //             , NULL
        //             , RRD_TYPE_NET_STAT_CONNTRACK
        //             , NULL
        //             , "Connection Tracker Changes"
        //             , "changes/s"
        //             , PLUGIN_NFACCT_NAME
        //             , NULL
        //             , NETDATA_CHART_PRIO_NETFILTER_CHANGES
        //             , nfstat_root.update_every
        //             , RRDSET_TYPE_LINE
        //     );
        //     rrdset_flag_set(st_changes, RRDSET_FLAG_DETAIL);

        //     rd_inserted = rrddim_add(st_changes, nfstat_root.attr2name[CTA_STATS_INSERT], NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_deleted = rrddim_add(st_changes, nfstat_root.attr2name[CTA_STATS_DELETE], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_delete_list = rrddim_add(st_changes, nfstat_root.attr2name[CTA_STATS_DELETE_LIST], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        // }
        // else
        //     rrdset_next(st_changes);

        // rrddim_set_by_pointer(st_changes, rd_inserted, (collected_number) nfstat_root.metrics[CTA_STATS_INSERT]);
        // rrddim_set_by_pointer(st_changes, rd_deleted, (collected_number) nfstat_root.metrics[CTA_STATS_DELETE]);
        // rrddim_set_by_pointer(st_changes, rd_delete_list, (collected_number) nfstat_root.metrics[CTA_STATS_DELETE_LIST]);

        // rrdset_done(st_changes);
    }

    // ----------------------------------------------------------------

    {
        // TODO: define the chart and set dimensions
        // static RRDSET *st_search = NULL;
        // static RRDDIM *rd_searched = NULL, *rd_restarted = NULL, *rd_found = NULL;

        // if(!st_search) {
        //     st_search = rrdset_create_localhost(
        //             RRD_TYPE_NET_STAT_NETFILTER
        //             , RRD_TYPE_NET_STAT_CONNTRACK "_search"
        //             , NULL
        //             , RRD_TYPE_NET_STAT_CONNTRACK
        //             , NULL
        //             , "Connection Tracker Searches"
        //             , "searches/s"
        //             , PLUGIN_NFACCT_NAME
        //             , NULL
        //             , NETDATA_CHART_PRIO_NETFILTER_SEARCH
        //             , nfstat_root.update_every
        //             , RRDSET_TYPE_LINE
        //     );
        //     rrdset_flag_set(st_search, RRDSET_FLAG_DETAIL);

        //     rd_searched = rrddim_add(st_search, nfstat_root.attr2name[CTA_STATS_SEARCHED], NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_restarted = rrddim_add(st_search, nfstat_root.attr2name[CTA_STATS_SEARCH_RESTART], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_found = rrddim_add(st_search, nfstat_root.attr2name[CTA_STATS_FOUND], NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
        // }
        // else
        //     rrdset_next(st_search);

        // rrddim_set_by_pointer(st_search, rd_searched, (collected_number) nfstat_root.metrics[CTA_STATS_SEARCHED]);
        // rrddim_set_by_pointer(st_search, rd_restarted, (collected_number) nfstat_root.metrics[CTA_STATS_SEARCH_RESTART]);
        // rrddim_set_by_pointer(st_search, rd_found, (collected_number) nfstat_root.metrics[CTA_STATS_FOUND]);

        // rrdset_done(st_search);
    }

    // ----------------------------------------------------------------

    {
        // TODO: define the chart and set dimensions
        // static RRDSET *st_errors = NULL;
        // static RRDDIM *rd_error = NULL, *rd_insert_failed = NULL, *rd_drop = NULL, *rd_early_drop = NULL;

        // if(!st_errors) {
        //     st_errors = rrdset_create_localhost(
        //             RRD_TYPE_NET_STAT_NETFILTER
        //             , RRD_TYPE_NET_STAT_CONNTRACK "_errors"
        //             , NULL
        //             , RRD_TYPE_NET_STAT_CONNTRACK
        //             , NULL
        //             , "Connection Tracker Errors"
        //             , "events/s"
        //             , PLUGIN_NFACCT_NAME
        //             , NULL
        //             , NETDATA_CHART_PRIO_NETFILTER_ERRORS
        //             , nfstat_root.update_every
        //             , RRDSET_TYPE_LINE
        //     );
        //     rrdset_flag_set(st_errors, RRDSET_FLAG_DETAIL);

        //     rd_error = rrddim_add(st_errors, nfstat_root.attr2name[CTA_STATS_ERROR], NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_insert_failed = rrddim_add(st_errors, nfstat_root.attr2name[CTA_STATS_INSERT_FAILED], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_drop = rrddim_add(st_errors, nfstat_root.attr2name[CTA_STATS_DROP], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_early_drop = rrddim_add(st_errors, nfstat_root.attr2name[CTA_STATS_EARLY_DROP], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        // }
        // else
        //     rrdset_next(st_errors);

        // rrddim_set_by_pointer(st_errors, rd_error, (collected_number) nfstat_root.metrics[CTA_STATS_ERROR]);
        // rrddim_set_by_pointer(st_errors, rd_insert_failed, (collected_number) nfstat_root.metrics[CTA_STATS_INSERT_FAILED]);
        // rrddim_set_by_pointer(st_errors, rd_drop, (collected_number) nfstat_root.metrics[CTA_STATS_DROP]);
        // rrddim_set_by_pointer(st_errors, rd_early_drop, (collected_number) nfstat_root.metrics[CTA_STATS_EARLY_DROP]);

        // rrdset_done(st_errors);
    }

    // ----------------------------------------------------------------

    {
        // TODO: define the chart and set dimensions
        // static RRDSET *st_expect = NULL;
        // static RRDDIM *rd_new = NULL, *rd_created = NULL, *rd_deleted = NULL;

        // if(!st_expect) {
        //     st_expect = rrdset_create_localhost(
        //             RRD_TYPE_NET_STAT_NETFILTER
        //             , RRD_TYPE_NET_STAT_CONNTRACK "_expect"
        //             , NULL
        //             , RRD_TYPE_NET_STAT_CONNTRACK
        //             , NULL
        //             , "Connection Tracker Expectations"
        //             , "expectations/s"
        //             , PLUGIN_NFACCT_NAME
        //             , NULL
        //             , NETDATA_CHART_PRIO_NETFILTER_EXPECT
        //             , nfstat_root.update_every
        //             , RRDSET_TYPE_LINE
        //     );
        //     rrdset_flag_set(st_expect, RRDSET_FLAG_DETAIL);

        //     rd_created = rrddim_add(st_expect, nfstat_root.attr2name_exp[CTA_STATS_EXP_CREATE], NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_deleted = rrddim_add(st_expect, nfstat_root.attr2name_exp[CTA_STATS_EXP_DELETE], NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
        //     rd_new = rrddim_add(st_expect, nfstat_root.attr2name_exp[CTA_STATS_EXP_NEW], NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
        // }
        // else
        //     rrdset_next(st_expect);

        // rrddim_set_by_pointer(st_expect, rd_created, (collected_number) nfstat_root.metrics_exp[CTA_STATS_EXP_CREATE]);
        // rrddim_set_by_pointer(st_expect, rd_deleted, (collected_number) nfstat_root.metrics_exp[CTA_STATS_EXP_DELETE]);
        // rrddim_set_by_pointer(st_expect, rd_new, (collected_number) nfstat_root.metrics_exp[CTA_STATS_EXP_NEW]);

        // rrdset_done(st_expect);
    }

}

#endif // HAVE_LINUX_NETFILTER_NFNETLINK_CONNTRACK_H


// ----------------------------------------------------------------------------
// DO_NFACCT - collect netfilter accounting statistics via netlink

#ifdef HAVE_LIBNETFILTER_ACCT
#define DO_NFACCT 1

#include <libnetfilter_acct/libnetfilter_acct.h>

struct nfacct_data {
    char *name;
    uint32_t hash;

    uint64_t pkts;
    uint64_t bytes;

    // TODO: do we need poiners for text pulled dimensions?
    // RRDDIM *rd_bytes;
    // RRDDIM *rd_packets;

    int updated;

    struct nfacct_data *next;
};

static struct {
    int update_every;
    char *buf;
    size_t buf_size;
    struct mnl_socket *mnl;
    struct nlmsghdr *nlh;
    unsigned int seq;
    uint32_t portid;
    struct nfacct *nfacct_buffer;
    struct nfacct_data *nfacct_metrics;
} nfacct_root = {
        .update_every = 1,
        .buf = NULL,
        .buf_size = 0,
        .mnl = NULL,
        .nlh = NULL,
        .seq = 0,
        .portid = 0,
        .nfacct_buffer = NULL,
        .nfacct_metrics = NULL
};

static inline struct nfacct_data *nfacct_data_get(const char *name, uint32_t hash) {
    struct nfacct_data *d = NULL, *last = NULL;
    for(d = nfacct_root.nfacct_metrics; d ; last = d, d = d->next) {
        if(unlikely(d->hash == hash && !strcmp(d->name, name)))
            return d;
    }

    d = callocz(1, sizeof(struct nfacct_data));
    d->name = strdupz(name);
    d->hash = hash;

    if(!last) {
        d->next = nfacct_root.nfacct_metrics;
        nfacct_root.nfacct_metrics = d;
    }
    else {
        d->next = last->next;
        last->next = d;
    }

    return d;
}

static int nfacct_init(int netdata_update_every) {
    nfacct_root.update_every = netdata_update_every;

    nfacct_root.buf_size = mnl_buffer_size();
    nfacct_root.buf = mallocz(nfacct_root.buf_size);

    nfacct_root.nfacct_buffer = nfacct_alloc();
    if(!nfacct_root.nfacct_buffer) {
        error("nfacct.plugin: nfacct_alloc() failed.");
        return 0;
    }

    nfacct_root.seq = (unsigned int)now_realtime_sec() - 1;

    nfacct_root.mnl  = mnl_socket_open(NETLINK_NETFILTER);
    if(!nfacct_root.mnl) {
        error("nfacct.plugin: mnl_socket_open() failed");
        return 1;
    }

    if(mnl_socket_bind(nfacct_root.mnl, 0, MNL_SOCKET_AUTOPID) < 0) {
        error("nfacct.plugin: mnl_socket_bind() failed");
        return 1;
    }
    nfacct_root.portid = mnl_socket_get_portid(nfacct_root.mnl);

    return 0;
}

static void nfacct_cleanup() {
    if(nfacct_root.mnl) {
        mnl_socket_close(nfacct_root.mnl);
        nfacct_root.mnl = NULL;
    }

    if(nfacct_root.nfacct_buffer) {
        nfacct_free(nfacct_root.nfacct_buffer);
        nfacct_root.nfacct_buffer = NULL;
    }

    freez(nfacct_root.buf);
    nfacct_root.buf = NULL;
    nfacct_root.buf_size = 0;

    // TODO: cleanup the metrics linked list
}

static int nfacct_callback(const struct nlmsghdr *nlh, void *data) {
    (void)data;

    if(nfacct_nlmsg_parse_payload(nlh, nfacct_root.nfacct_buffer) < 0) {
        error("NFACCT: nfacct_nlmsg_parse_payload() failed.");
        return MNL_CB_OK;
    }

    const char *name = nfacct_attr_get_str(nfacct_root.nfacct_buffer, NFACCT_ATTR_NAME);
    uint32_t hash = simple_hash(name);

    struct nfacct_data *d = nfacct_data_get(name, hash);

    d->pkts  = nfacct_attr_get_u64(nfacct_root.nfacct_buffer, NFACCT_ATTR_PKTS);
    d->bytes = nfacct_attr_get_u64(nfacct_root.nfacct_buffer, NFACCT_ATTR_BYTES);
    d->updated = 1;

    return MNL_CB_OK;
}

static int nfacct_collect() {
    // mark all old metrics as not-updated
    struct nfacct_data *d;
    for(d = nfacct_root.nfacct_metrics; d ; d = d->next)
        d->updated = 0;

    // prepare the request
    nfacct_root.seq++;
    nfacct_root.nlh = nfacct_nlmsg_build_hdr(nfacct_root.buf, NFNL_MSG_ACCT_GET, NLM_F_DUMP, (uint32_t)nfacct_root.seq);
    if(!nfacct_root.nlh) {
        error("NFACCT: nfacct_nlmsg_build_hdr() failed");
        return 1;
    }

    // send the request
    if(mnl_socket_sendto(nfacct_root.mnl, nfacct_root.nlh, nfacct_root.nlh->nlmsg_len) < 0) {
        error("NFACCT: mnl_socket_sendto() failed");
        return 1;
    }

    // get the reply
    ssize_t ret;
    while((ret = mnl_socket_recvfrom(nfacct_root.mnl, nfacct_root.buf, nfacct_root.buf_size)) > 0) {
        if(mnl_cb_run(
                nfacct_root.buf
                , (size_t)ret
                , nfacct_root.seq
                , nfacct_root.portid
                , nfacct_callback
                , NULL
        ) <= 0)
            break;
    }

    // verify we run without issues
    if (ret == -1) {
        error("NFACCT: error communicating with kernel. This plugin can only work when netdata runs as root.");
        return 1;
    }

    return 0;
}

static void nfacct_send_metrics() {
    // TODO: define the chart and set dimensions
    // static RRDSET *st_bytes = NULL, *st_packets = NULL;

    // if(!nfacct_root.nfacct_metrics) return;
    // struct nfacct_data *d;

    // if(!st_packets) {
    //     st_packets = rrdset_create_localhost(
    //             "netfilter"
    //             , "nfacct_packets"
    //             , NULL
    //             , "nfacct"
    //             , NULL
    //             , "Netfilter Accounting Packets"
    //             , "packets/s"
    //             , PLUGIN_NFACCT_NAME
    //             , NULL
    //             , NETDATA_CHART_PRIO_NETFILTER_PACKETS
    //             , nfacct_root.update_every
    //             , RRDSET_TYPE_STACKED
    //     );
    // }
    // else rrdset_next(st_packets);

    // for(d = nfacct_root.nfacct_metrics; d ; d = d->next) {
    //     if(likely(d->updated)) {
    //         if(unlikely(!d->rd_packets))
    //             d->rd_packets = rrddim_add(
    //                     st_packets
    //                     , d->name
    //                     , NULL
    //                     , 1
    //                     , nfacct_root.update_every
    //                     , RRD_ALGORITHM_INCREMENTAL
    //             );

    //         rrddim_set_by_pointer(
    //                 st_packets
    //                 , d->rd_packets
    //                 , (collected_number)d->pkts
    //         );
    //     }
    // }

    // rrdset_done(st_packets);

    // ----------------------------------------------------------------

    // TODO: define the chart and set dimensions
    // st_bytes = rrdset_find_bytype_localhost("netfilter", "nfacct_bytes");
    // if(!st_bytes) {
    //     st_bytes = rrdset_create_localhost(
    //             "netfilter"
    //             , "nfacct_bytes"
    //             , NULL
    //             , "nfacct"
    //             , NULL
    //             , "Netfilter Accounting Bandwidth"
    //             , "kilobytes/s"
    //             , PLUGIN_NFACCT_NAME
    //             , NULL
    //             , NETDATA_CHART_PRIO_NETFILTER_BYTES
    //             , nfacct_root.update_every
    //             , RRDSET_TYPE_STACKED
    //     );
    // }
    // else rrdset_next(st_bytes);

    // for(d = nfacct_root.nfacct_metrics; d ; d = d->next) {
    //     if(likely(d->updated)) {
    //         if(unlikely(!d->rd_bytes))
    //             d->rd_bytes = rrddim_add(
    //                     st_bytes
    //                     , d->name
    //                     , NULL
    //                     , 1
    //                     , 1000 * nfacct_root.update_every
    //                     , RRD_ALGORITHM_INCREMENTAL
    //             );

    //         rrddim_set_by_pointer(
    //                 st_bytes
    //                 , d->rd_bytes
    //                 , (collected_number)d->bytes
    //         );
    //     }
    // }

    // rrdset_done(st_bytes);
}

#endif // HAVE_LIBNETFILTER_ACCT

int main(int argc, char **argv) {

    // ------------------------------------------------------------------------
    // initialization of netdata plugin

    program_name = "nfacct.plugin";

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
            printf("nfacct.plugin %s\n", VERSION);
            exit(0);
        }
        else if(strcmp("debug", argv[i]) == 0) {
            debug = 1;
            continue;
        }
        else if(strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
            fprintf(stderr,
                    "\n"
                    " netdata nfacct.plugin %s\n"
                    " Copyright (C) 2015-2017 Costa Tsaousis <costa@tsaousis.gr>\n"
                    " Released under GNU General Public License v3 or later.\n"
                    " All rights reserved.\n"
                    "\n"
                    " This program is a data collector plugin for netdata.\n"
                    "\n"
                    " Available command line options:\n"
                    "\n"
                    "  SECONDS                 data collection frequency\n"
                    "                          minimum: %d\n"
                    "\n"
                    "  debug                   enable verbose output\n"
                    "                          default: disabled\n"
                    "\n"
                    "  -v\n"
                    "  -V\n"
                    "  version                 print version and exit\n"
                    "\n"
                    " For more information:\n"
                    " https://github.com/netdata/netdata/tree/master/collectors/nfacct.plugin\n"
                    "\n"
                    , VERSION
                    , netdata_update_every
            );
            exit(1);
        }

        error("nfacct.plugin: ignoring parameter '%s'", argv[i]);
    }

    errno = 0;

    if(freq > netdata_update_every)
        netdata_update_every = freq;
    else if(freq)
        error("update frequency %d seconds is too small for NFACCT. Using %d.", freq, netdata_update_every);

#ifdef DO_NFACCT
    int nfacct = !nfacct_init(netdata_update_every);
#endif

#ifdef DO_NFSTAT
    int nfstat = !nfstat_init(netdata_update_every);
#endif

    // ------------------------------------------------------------------------

    usec_t step = netdata_update_every * USEC_PER_SEC;
    heartbeat_t hb;
    heartbeat_init(&hb);
    for(;;) {
        heartbeat_next(&hb, step);

        if(unlikely(netdata_exit)) break;

#ifdef DO_NFACCT
        if(likely(nfacct)) {
            nfacct = !nfacct_collect();

            if(likely(nfacct))
                nfacct_send_metrics();
        }
#endif

#ifdef DO_NFSTAT
        if(likely(nfstat)) {
            nfstat = !nfstat_collect();

            if(likely(nfstat))
                nfstat_send_metrics();
        }
#endif
    }

    return 0;
}

#else // !HAVE_LIBMNL

int main(int argc, char **argv) {
    fatal("nfacct.plugin is not compiled.");
}

#endif // !HAVE_LIBMNL

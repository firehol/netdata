#include "common.h"

struct nfsd_procs {
    char name[30];
    unsigned long long value;
    int present;
};

struct nfsd_procs nfsd_proc2_values[] = {
    { "null", 0ULL, 0 },
    { "getattr", 0ULL, 0 },
    { "setattr", 0ULL, 0 },
    { "root", 0ULL, 0 },
    { "lookup", 0ULL, 0 },
    { "readlink", 0ULL, 0 },
    { "read", 0ULL, 0 },
    { "wrcache", 0ULL, 0 },
    { "write", 0ULL, 0 },
    { "create", 0ULL, 0 },
    { "remove", 0ULL, 0 },
    { "rename", 0ULL, 0 },
    { "link", 0ULL, 0 },
    { "symlink", 0ULL, 0 },
    { "mkdir", 0ULL, 0 },
    { "rmdir", 0ULL, 0 },
    { "readdir", 0ULL, 0 },
    { "fsstat", 0ULL, 0 },

    /* termination */
    { "", 0ULL, 0 }
};

struct nfsd_procs nfsd_proc3_values[] = {
    { "null", 0ULL, 0 },
    { "getattr", 0ULL, 0 },
    { "setattr", 0ULL, 0 },
    { "lookup", 0ULL, 0 },
    { "access", 0ULL, 0 },
    { "readlink", 0ULL, 0 },
    { "read", 0ULL, 0 },
    { "write", 0ULL, 0 },
    { "create", 0ULL, 0 },
    { "mkdir", 0ULL, 0 },
    { "symlink", 0ULL, 0 },
    { "mknod", 0ULL, 0 },
    { "remove", 0ULL, 0 },
    { "rmdir", 0ULL, 0 },
    { "rename", 0ULL, 0 },
    { "link", 0ULL, 0 },
    { "readdir", 0ULL, 0 },
    { "readdirplus", 0ULL, 0 },
    { "fsstat", 0ULL, 0 },
    { "fsinfo", 0ULL, 0 },
    { "pathconf", 0ULL, 0 },
    { "commit", 0ULL, 0 },

    /* termination */
    { "", 0ULL, 0 }
};

struct nfsd_procs nfsd_proc4_values[] = {
    { "null", 0ULL, 0 },
    { "read", 0ULL, 0 },
    { "write", 0ULL, 0 },
    { "commit", 0ULL, 0 },
    { "open", 0ULL, 0 },
    { "open_conf", 0ULL, 0 },
    { "open_noat", 0ULL, 0 },
    { "open_dgrd", 0ULL, 0 },
    { "close", 0ULL, 0 },
    { "setattr", 0ULL, 0 },
    { "fsinfo", 0ULL, 0 },
    { "renew", 0ULL, 0 },
    { "setclntid", 0ULL, 0 },
    { "confirm", 0ULL, 0 },
    { "lock", 0ULL, 0 },
    { "lockt", 0ULL, 0 },
    { "locku", 0ULL, 0 },
    { "access", 0ULL, 0 },
    { "getattr", 0ULL, 0 },
    { "lookup", 0ULL, 0 },
    { "lookup_root", 0ULL, 0 },
    { "remove", 0ULL, 0 },
    { "rename", 0ULL, 0 },
    { "link", 0ULL, 0 },
    { "symlink", 0ULL, 0 },
    { "create", 0ULL, 0 },
    { "pathconf", 0ULL, 0 },
    { "statfs", 0ULL, 0 },
    { "readlink", 0ULL, 0 },
    { "readdir", 0ULL, 0 },
    { "server_caps", 0ULL, 0 },
    { "delegreturn", 0ULL, 0 },
    { "getacl", 0ULL, 0 },
    { "setacl", 0ULL, 0 },
    { "fs_locations", 0ULL, 0 },
    { "rel_lkowner", 0ULL, 0 },
    { "secinfo", 0ULL, 0 },
    { "fsid_present", 0ULL, 0 },

    /* nfsv4.1 client ops */
    { "exchange_id", 0ULL, 0 },
    { "create_session", 0ULL, 0 },
    { "destroy_session", 0ULL, 0 },
    { "sequence", 0ULL, 0 },
    { "get_lease_time", 0ULL, 0 },
    { "reclaim_comp", 0ULL, 0 },
    { "layoutget", 0ULL, 0 },
    { "getdevinfo", 0ULL, 0 },
    { "layoutcommit", 0ULL, 0 },
    { "layoutreturn", 0ULL, 0 },
    { "secinfo_no", 0ULL, 0 },
    { "test_stateid", 0ULL, 0 },
    { "free_stateid", 0ULL, 0 },
    { "getdevicelist", 0ULL, 0 },
    { "bind_conn_to_ses", 0ULL, 0 },
    { "destroy_clientid", 0ULL, 0 },

    /* nfsv4.2 client ops */
    { "seek", 0ULL, 0 },
    { "allocate", 0ULL, 0 },
    { "deallocate", 0ULL, 0 },
    { "layoutstats", 0ULL, 0 },
    { "clone", 0ULL, 0 },

    /* termination */
    { "", 0ULL, 0 }
};

struct nfsd_procs nfsd4_ops_values[] = {
    { "unused_op0", 0ULL, 0},
    { "unused_op1", 0ULL, 0},
    { "future_op2", 0ULL, 0},
    { "access", 0ULL, 0},
    { "close", 0ULL, 0},
    { "commit", 0ULL, 0},
    { "create", 0ULL, 0},
    { "delegpurge", 0ULL, 0},
    { "delegreturn", 0ULL, 0},
    { "getattr", 0ULL, 0},
    { "getfh", 0ULL, 0},
    { "link", 0ULL, 0},
    { "lock", 0ULL, 0},
    { "lockt", 0ULL, 0},
    { "locku", 0ULL, 0},
    { "lookup", 0ULL, 0},
    { "lookup_root", 0ULL, 0},
    { "nverify", 0ULL, 0},
    { "open", 0ULL, 0},
    { "openattr", 0ULL, 0},
    { "open_confirm", 0ULL, 0},
    { "open_downgrade", 0ULL, 0},
    { "putfh", 0ULL, 0},
    { "putpubfh", 0ULL, 0},
    { "putrootfh", 0ULL, 0},
    { "read", 0ULL, 0},
    { "readdir", 0ULL, 0},
    { "readlink", 0ULL, 0},
    { "remove", 0ULL, 0},
    { "rename", 0ULL, 0},
    { "renew", 0ULL, 0},
    { "restorefh", 0ULL, 0},
    { "savefh", 0ULL, 0},
    { "secinfo", 0ULL, 0},
    { "setattr", 0ULL, 0},
    { "setclientid", 0ULL, 0},
    { "setclientid_confirm", 0ULL, 0},
    { "verify", 0ULL, 0},
    { "write", 0ULL, 0},
    { "release_lockowner", 0ULL, 0},

    /* nfs41 */
    { "backchannel_ctl", 0ULL, 0},
    { "bind_conn_to_session", 0ULL, 0},
    { "exchange_id", 0ULL, 0},
    { "create_session", 0ULL, 0},
    { "destroy_session", 0ULL, 0},
    { "free_stateid", 0ULL, 0},
    { "get_dir_delegation", 0ULL, 0},
    { "getdeviceinfo", 0ULL, 0},
    { "getdevicelist", 0ULL, 0},
    { "layoutcommit", 0ULL, 0},
    { "layoutget", 0ULL, 0},
    { "layoutreturn", 0ULL, 0},
    { "secinfo_no_name", 0ULL, 0},
    { "sequence", 0ULL, 0},
    { "set_ssv", 0ULL, 0},
    { "test_stateid", 0ULL, 0},
    { "want_delegation", 0ULL, 0},
    { "destroy_clientid", 0ULL, 0},
    { "reclaim_complete", 0ULL, 0},

    /* nfs42 */
    { "allocate", 0ULL, 0},
    { "copy", 0ULL, 0},
    { "copy_notify", 0ULL, 0},
    { "deallocate", 0ULL, 0},
    { "ioadvise", 0ULL, 0},
    { "layouterror", 0ULL, 0},
    { "layoutstats", 0ULL, 0},
    { "offload_cancel", 0ULL, 0},
    { "offload_status", 0ULL, 0},
    { "read_plus", 0ULL, 0},
    { "seek", 0ULL, 0},
    { "write_same", 0ULL, 0},

    /* termination */
    { "", 0ULL, 0 }
};


int do_proc_net_rpc_nfsd(int update_every, usec_t dt) {
    (void)dt;
    static procfile *ff = NULL;
    static int do_rc = -1, do_fh = -1, do_io = -1, do_th = -1, do_ra = -1, do_net = -1, do_rpc = -1, do_proc2 = -1, do_proc3 = -1, do_proc4 = -1, do_proc4ops = -1;
    static int ra_warning = 0, th_warning = 0, proc2_warning = 0, proc3_warning = 0, proc4_warning = 0, proc4ops_warning = 0;

    if(!ff) {
        char filename[FILENAME_MAX + 1];
        snprintfz(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/proc/net/rpc/nfsd");
        ff = procfile_open(config_get("plugin:proc:/proc/net/rpc/nfsd", "filename to monitor", filename), " \t", PROCFILE_FLAG_DEFAULT);
    }
    if(!ff) return 1;

    ff = procfile_readall(ff);
    if(!ff) return 0; // we return 0, so that we will retry to open it next time

    if(do_rc == -1) do_rc = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "read cache", 1);
    if(do_fh == -1) do_fh = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "file handles", 1);
    if(do_io == -1) do_io = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "I/O", 1);
    if(do_th == -1) do_th = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "threads", 1);
    if(do_ra == -1) do_ra = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "read ahead", 1);
    if(do_net == -1) do_net = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "network", 1);
    if(do_rpc == -1) do_rpc = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "rpc", 1);
    if(do_proc2 == -1) do_proc2 = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "NFS v2 procedures", 1);
    if(do_proc3 == -1) do_proc3 = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "NFS v3 procedures", 1);
    if(do_proc4 == -1) do_proc4 = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "NFS v4 procedures", 1);
    if(do_proc4ops == -1) do_proc4ops = config_get_boolean("plugin:proc:/proc/net/rpc/nfsd", "NFS v4 operations", 1);

    // if they are enabled, reset them to 1
    // later we do them =2 to avoid doing strcmp() for all lines
    if(do_rc) do_rc = 1;
    if(do_fh) do_fh = 1;
    if(do_io) do_io = 1;
    if(do_th) do_th = 1;
    if(do_ra) do_ra = 1;
    if(do_net) do_net = 1;
    if(do_rpc) do_rpc = 1;
    if(do_proc2) do_proc2 = 1;
    if(do_proc3) do_proc3 = 1;
    if(do_proc4) do_proc4 = 1;
    if(do_proc4ops) do_proc4ops = 1;

    size_t lines = procfile_lines(ff), l;

    char *type;
    unsigned long long rc_hits = 0, rc_misses = 0, rc_nocache = 0;
    unsigned long long fh_stale = 0, fh_total_lookups = 0, fh_anonymous_lookups = 0, fh_dir_not_in_dcache = 0, fh_non_dir_not_in_dcache = 0;
    unsigned long long io_read = 0, io_write = 0;
    unsigned long long th_threads = 0, th_fullcnt = 0, th_hist10 = 0, th_hist20 = 0, th_hist30 = 0, th_hist40 = 0, th_hist50 = 0, th_hist60 = 0, th_hist70 = 0, th_hist80 = 0, th_hist90 = 0, th_hist100 = 0;
    unsigned long long ra_size = 0, ra_hist10 = 0, ra_hist20 = 0, ra_hist30 = 0, ra_hist40 = 0, ra_hist50 = 0, ra_hist60 = 0, ra_hist70 = 0, ra_hist80 = 0, ra_hist90 = 0, ra_hist100 = 0, ra_none = 0;
    unsigned long long net_count = 0, net_udp_count = 0, net_tcp_count = 0, net_tcp_connections = 0;
    unsigned long long rpc_calls = 0, rpc_bad_format = 0, rpc_bad_auth = 0, rpc_bad_client = 0;

    for(l = 0; l < lines ;l++) {
        size_t words = procfile_linewords(ff, l);
        if(!words) continue;

        type = procfile_lineword(ff, l, 0);

        if(do_rc == 1 && strcmp(type, "rc") == 0) {
            if(words < 4) {
                error("%s line of /proc/net/rpc/nfsd has %zu words, expected %d", type, words, 4);
                continue;
            }

            rc_hits = str2ull(procfile_lineword(ff, l, 1));
            rc_misses = str2ull(procfile_lineword(ff, l, 2));
            rc_nocache = str2ull(procfile_lineword(ff, l, 3));

            unsigned long long sum = rc_hits + rc_misses + rc_nocache;
            if(sum == 0ULL) do_rc = -1;
            else do_rc = 2;
        }
        else if(do_fh == 1 && strcmp(type, "fh") == 0) {
            if(words < 6) {
                error("%s line of /proc/net/rpc/nfsd has %zu words, expected %d", type, words, 6);
                continue;
            }

            fh_stale = str2ull(procfile_lineword(ff, l, 1));
            fh_total_lookups = str2ull(procfile_lineword(ff, l, 2));
            fh_anonymous_lookups = str2ull(procfile_lineword(ff, l, 3));
            fh_dir_not_in_dcache = str2ull(procfile_lineword(ff, l, 4));
            fh_non_dir_not_in_dcache = str2ull(procfile_lineword(ff, l, 5));

            unsigned long long sum = fh_stale + fh_total_lookups + fh_anonymous_lookups + fh_dir_not_in_dcache + fh_non_dir_not_in_dcache;
            if(sum == 0ULL) do_fh = -1;
            else do_fh = 2;
        }
        else if(do_io == 1 && strcmp(type, "io") == 0) {
            if(words < 3) {
                error("%s line of /proc/net/rpc/nfsd has %zu words, expected %d", type, words, 3);
                continue;
            }

            io_read = str2ull(procfile_lineword(ff, l, 1));
            io_write = str2ull(procfile_lineword(ff, l, 2));

            unsigned long long sum = io_read + io_write;
            if(sum == 0ULL) do_io = -1;
            else do_io = 2;
        }
        else if(do_th == 1 && strcmp(type, "th") == 0) {
            if(words < 13) {
                error("%s line of /proc/net/rpc/nfsd has %zu words, expected %d", type, words, 13);
                continue;
            }

            th_threads = str2ull(procfile_lineword(ff, l, 1));
            th_fullcnt = str2ull(procfile_lineword(ff, l, 2));
            th_hist10 = (unsigned long long)(atof(procfile_lineword(ff, l, 3)) * 1000.0);
            th_hist20 = (unsigned long long)(atof(procfile_lineword(ff, l, 4)) * 1000.0);
            th_hist30 = (unsigned long long)(atof(procfile_lineword(ff, l, 5)) * 1000.0);
            th_hist40 = (unsigned long long)(atof(procfile_lineword(ff, l, 6)) * 1000.0);
            th_hist50 = (unsigned long long)(atof(procfile_lineword(ff, l, 7)) * 1000.0);
            th_hist60 = (unsigned long long)(atof(procfile_lineword(ff, l, 8)) * 1000.0);
            th_hist70 = (unsigned long long)(atof(procfile_lineword(ff, l, 9)) * 1000.0);
            th_hist80 = (unsigned long long)(atof(procfile_lineword(ff, l, 10)) * 1000.0);
            th_hist90 = (unsigned long long)(atof(procfile_lineword(ff, l, 11)) * 1000.0);
            th_hist100 = (unsigned long long)(atof(procfile_lineword(ff, l, 12)) * 1000.0);

            // threads histogram has been disabled on recent kernels
            // http://permalink.gmane.org/gmane.linux.nfs/24528
            unsigned long long sum = th_hist10 + th_hist20 + th_hist30 + th_hist40 + th_hist50 + th_hist60 + th_hist70 + th_hist80 + th_hist90 + th_hist100;
            if(sum == 0ULL) {
                if(!th_warning) {
                    info("Disabling /proc/net/rpc/nfsd threads histogram. It seems unused on this machine. It will be enabled automatically when found with data in it.");
                    th_warning = 1;
                }
                do_th = -1;
            }
            else do_th = 2;
        }
        else if(do_ra == 1 && strcmp(type, "ra") == 0) {
            if(words < 13) {
                error("%s line of /proc/net/rpc/nfsd has %zu words, expected %d", type, words, 13);
                continue;
            }

            ra_size = str2ull(procfile_lineword(ff, l, 1));
            ra_hist10 = str2ull(procfile_lineword(ff, l, 2));
            ra_hist20 = str2ull(procfile_lineword(ff, l, 3));
            ra_hist30 = str2ull(procfile_lineword(ff, l, 4));
            ra_hist40 = str2ull(procfile_lineword(ff, l, 5));
            ra_hist50 = str2ull(procfile_lineword(ff, l, 6));
            ra_hist60 = str2ull(procfile_lineword(ff, l, 7));
            ra_hist70 = str2ull(procfile_lineword(ff, l, 8));
            ra_hist80 = str2ull(procfile_lineword(ff, l, 9));
            ra_hist90 = str2ull(procfile_lineword(ff, l, 10));
            ra_hist100 = str2ull(procfile_lineword(ff, l, 11));
            ra_none = str2ull(procfile_lineword(ff, l, 12));

            unsigned long long sum = ra_hist10 + ra_hist20 + ra_hist30 + ra_hist40 + ra_hist50 + ra_hist60 + ra_hist70 + ra_hist80 + ra_hist90 + ra_hist100 + ra_none;
            if(sum == 0ULL) {
                if(!ra_warning) {
                    info("Disabling /proc/net/rpc/nfsd read ahead histogram. It seems unused on this machine. It will be enabled automatically when found with data in it.");
                    ra_warning = 1;
                }
                do_ra = -1;
            }
            else do_ra = 2;
        }
        else if(do_net == 1 && strcmp(type, "net") == 0) {
            if(words < 5) {
                error("%s line of /proc/net/rpc/nfsd has %zu words, expected %d", type, words, 5);
                continue;
            }

            net_count = str2ull(procfile_lineword(ff, l, 1));
            net_udp_count = str2ull(procfile_lineword(ff, l, 2));
            net_tcp_count = str2ull(procfile_lineword(ff, l, 3));
            net_tcp_connections = str2ull(procfile_lineword(ff, l, 4));

            unsigned long long sum = net_count + net_udp_count + net_tcp_count + net_tcp_connections;
            if(sum == 0ULL) do_net = -1;
            else do_net = 2;
        }
        else if(do_rpc == 1 && strcmp(type, "rpc") == 0) {
            if(words < 6) {
                error("%s line of /proc/net/rpc/nfsd has %zu words, expected %d", type, words, 6);
                continue;
            }

            rpc_calls = str2ull(procfile_lineword(ff, l, 1));
            rpc_bad_format = str2ull(procfile_lineword(ff, l, 2));
            rpc_bad_auth = str2ull(procfile_lineword(ff, l, 3));
            rpc_bad_client = str2ull(procfile_lineword(ff, l, 4));

            unsigned long long sum = rpc_calls + rpc_bad_format + rpc_bad_auth + rpc_bad_client;
            if(sum == 0ULL) do_rpc = -1;
            else do_rpc = 2;
        }
        else if(do_proc2 == 1 && strcmp(type, "proc2") == 0) {
            // the first number is the count of numbers present
            // so we start for word 2

            unsigned long long sum = 0;
            unsigned int i, j;
            for(i = 0, j = 2; j < words && nfsd_proc2_values[i].name[0] ; i++, j++) {
                nfsd_proc2_values[i].value = str2ull(procfile_lineword(ff, l, j));
                nfsd_proc2_values[i].present = 1;
                sum += nfsd_proc2_values[i].value;
            }

            if(sum == 0ULL) {
                if(!proc2_warning) {
                    error("Disabling /proc/net/rpc/nfsd v2 procedure calls chart. It seems unused on this machine. It will be enabled automatically when found with data in it.");
                    proc2_warning = 1;
                }
                do_proc2 = 0;
            }
            else do_proc2 = 2;
        }
        else if(do_proc3 == 1 && strcmp(type, "proc3") == 0) {
            // the first number is the count of numbers present
            // so we start for word 2

            unsigned long long sum = 0;
            unsigned int i, j;
            for(i = 0, j = 2; j < words && nfsd_proc3_values[i].name[0] ; i++, j++) {
                nfsd_proc3_values[i].value = str2ull(procfile_lineword(ff, l, j));
                nfsd_proc3_values[i].present = 1;
                sum += nfsd_proc3_values[i].value;
            }

            if(sum == 0ULL) {
                if(!proc3_warning) {
                    info("Disabling /proc/net/rpc/nfsd v3 procedure calls chart. It seems unused on this machine. It will be enabled automatically when found with data in it.");
                    proc3_warning = 1;
                }
                do_proc3 = 0;
            }
            else do_proc3 = 2;
        }
        else if(do_proc4 == 1 && strcmp(type, "proc4") == 0) {
            // the first number is the count of numbers present
            // so we start for word 2

            unsigned long long sum = 0;
            unsigned int i, j;
            for(i = 0, j = 2; j < words && nfsd_proc4_values[i].name[0] ; i++, j++) {
                nfsd_proc4_values[i].value = str2ull(procfile_lineword(ff, l, j));
                nfsd_proc4_values[i].present = 1;
                sum += nfsd_proc4_values[i].value;
            }

            if(sum == 0ULL) {
                if(!proc4_warning) {
                    info("Disabling /proc/net/rpc/nfsd v4 procedure calls chart. It seems unused on this machine. It will be enabled automatically when found with data in it.");
                    proc4_warning = 1;
                }
                do_proc4 = 0;
            }
            else do_proc4 = 2;
        }
        else if(do_proc4ops == 1 && strcmp(type, "proc4ops") == 0) {
            // the first number is the count of numbers present
            // so we start for word 2

            unsigned long long sum = 0;
            unsigned int i, j;
            for(i = 0, j = 2; j < words && nfsd4_ops_values[i].name[0] ; i++, j++) {
                nfsd4_ops_values[i].value = str2ull(procfile_lineword(ff, l, j));
                nfsd4_ops_values[i].present = 1;
                sum += nfsd4_ops_values[i].value;
            }

            if(sum == 0ULL) {
                if(!proc4ops_warning) {
                    info("Disabling /proc/net/rpc/nfsd v4 operations chart. It seems unused on this machine. It will be enabled automatically when found with data in it.");
                    proc4ops_warning = 1;
                }
                do_proc4ops = 0;
            }
            else do_proc4ops = 2;
        }
    }

    RRDSET *st;

    // --------------------------------------------------------------------

    if(do_rc == 2) {
        st = rrdset_find_bytype("nfsd", "readcache");
        if(!st) {
            st = rrdset_create("nfsd", "readcache", NULL, "cache", NULL, "NFS Server Read Cache", "reads/s", 5000, update_every, RRDSET_TYPE_STACKED);

            rrddim_add(st, "hits", NULL, 1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "misses", NULL, 1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "nocache", NULL, 1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        rrddim_set(st, "hits", rc_hits);
        rrddim_set(st, "misses", rc_misses);
        rrddim_set(st, "nocache", rc_nocache);
        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_fh == 2) {
        st = rrdset_find_bytype("nfsd", "filehandles");
        if(!st) {
            st = rrdset_create("nfsd", "filehandles", NULL, "filehandles", NULL, "NFS Server File Handles", "handles/s", 5001, update_every, RRDSET_TYPE_LINE);
            st->isdetail = 1;

            rrddim_add(st, "stale", NULL, 1, 1, RRDDIM_ABSOLUTE);
            rrddim_add(st, "total_lookups", NULL, 1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "anonymous_lookups", NULL, 1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "dir_not_in_dcache", NULL, -1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "non_dir_not_in_dcache", NULL, -1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        rrddim_set(st, "stale", fh_stale);
        rrddim_set(st, "total_lookups", fh_total_lookups);
        rrddim_set(st, "anonymous_lookups", fh_anonymous_lookups);
        rrddim_set(st, "dir_not_in_dcache", fh_dir_not_in_dcache);
        rrddim_set(st, "non_dir_not_in_dcache", fh_non_dir_not_in_dcache);
        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_io == 2) {
        st = rrdset_find_bytype("nfsd", "io");
        if(!st) {
            st = rrdset_create("nfsd", "io", NULL, "io", NULL, "NFS Server I/O", "kilobytes/s", 5002, update_every, RRDSET_TYPE_AREA);

            rrddim_add(st, "read", NULL, 1, 1000, RRDDIM_INCREMENTAL);
            rrddim_add(st, "write", NULL, -1, 1000, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        rrddim_set(st, "read", io_read);
        rrddim_set(st, "write", io_write);
        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_th == 2) {
        st = rrdset_find_bytype("nfsd", "threads");
        if(!st) {
            st = rrdset_create("nfsd", "threads", NULL, "threads", NULL, "NFS Server Threads", "threads", 5003, update_every, RRDSET_TYPE_LINE);

            rrddim_add(st, "threads", NULL, 1, 1, RRDDIM_ABSOLUTE);
        }
        else rrdset_next(st);

        rrddim_set(st, "threads", th_threads);
        rrdset_done(st);

        st = rrdset_find_bytype("nfsd", "threads_fullcnt");
        if(!st) {
            st = rrdset_create("nfsd", "threads_fullcnt", NULL, "threads", NULL, "NFS Server Threads Full Count", "ops/s", 5004, update_every, RRDSET_TYPE_LINE);

            rrddim_add(st, "full_count", NULL, 1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        rrddim_set(st, "full_count", th_fullcnt);
        rrdset_done(st);

        st = rrdset_find_bytype("nfsd", "threads_histogram");
        if(!st) {
            st = rrdset_create("nfsd", "threads_histogram", NULL, "threads", NULL, "NFS Server Threads Usage Histogram", "percentage", 5005, update_every, RRDSET_TYPE_LINE);

            rrddim_add(st, "0%-10%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "10%-20%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "20%-30%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "30%-40%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "40%-50%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "50%-60%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "60%-70%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "70%-80%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "80%-90%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
            rrddim_add(st, "90%-100%", NULL, 1, 1000, RRDDIM_ABSOLUTE);
        }
        else rrdset_next(st);

        rrddim_set(st, "0%-10%", th_hist10);
        rrddim_set(st, "10%-20%", th_hist20);
        rrddim_set(st, "20%-30%", th_hist30);
        rrddim_set(st, "30%-40%", th_hist40);
        rrddim_set(st, "40%-50%", th_hist50);
        rrddim_set(st, "50%-60%", th_hist60);
        rrddim_set(st, "60%-70%", th_hist70);
        rrddim_set(st, "70%-80%", th_hist80);
        rrddim_set(st, "80%-90%", th_hist90);
        rrddim_set(st, "90%-100%", th_hist100);
        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_ra == 2) {
        st = rrdset_find_bytype("nfsd", "readahead");
        if(!st) {
            st = rrdset_create("nfsd", "readahead", NULL, "readahead", NULL, "NFS Server Read Ahead Depth", "percentage", 5005, update_every, RRDSET_TYPE_STACKED);

            rrddim_add(st, "10%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "20%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "30%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "40%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "50%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "60%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "70%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "80%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "90%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "100%", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
            rrddim_add(st, "misses", NULL, 1, 1, RRDDIM_PCENT_OVER_DIFF_TOTAL);
        }
        else rrdset_next(st);

        // ignore ra_size
        if(ra_size) {};

        rrddim_set(st, "10%", ra_hist10);
        rrddim_set(st, "20%", ra_hist20);
        rrddim_set(st, "30%", ra_hist30);
        rrddim_set(st, "40%", ra_hist40);
        rrddim_set(st, "50%", ra_hist50);
        rrddim_set(st, "60%", ra_hist60);
        rrddim_set(st, "70%", ra_hist70);
        rrddim_set(st, "80%", ra_hist80);
        rrddim_set(st, "90%", ra_hist90);
        rrddim_set(st, "100%", ra_hist100);
        rrddim_set(st, "misses", ra_none);
        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_net == 2) {
        st = rrdset_find_bytype("nfsd", "net");
        if(!st) {
            st = rrdset_create("nfsd", "net", NULL, "network", NULL, "NFS Server Network Statistics", "packets/s", 5007, update_every, RRDSET_TYPE_STACKED);
            st->isdetail = 1;

            rrddim_add(st, "udp", NULL, 1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "tcp", NULL, 1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        // ignore net_count, net_tcp_connections
        if(net_count) {};
        if(net_tcp_connections) {};

        rrddim_set(st, "udp", net_udp_count);
        rrddim_set(st, "tcp", net_tcp_count);
        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_rpc == 2) {
        st = rrdset_find_bytype("nfsd", "rpc");
        if(!st) {
            st = rrdset_create("nfsd", "rpc", NULL, "rpc", NULL, "NFS Server Remote Procedure Calls Statistics", "calls/s", 5008, update_every, RRDSET_TYPE_LINE);
            st->isdetail = 1;

            rrddim_add(st, "calls", NULL, 1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "bad_format", NULL, -1, 1, RRDDIM_INCREMENTAL);
            rrddim_add(st, "bad_auth", NULL, -1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        // ignore rpc_bad_client
        if(rpc_bad_client) {};

        rrddim_set(st, "calls", rpc_calls);
        rrddim_set(st, "bad_format", rpc_bad_format);
        rrddim_set(st, "bad_auth", rpc_bad_auth);
        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_proc2 == 2) {
        unsigned int i;
        st = rrdset_find_bytype("nfsd", "proc2");
        if(!st) {
            st = rrdset_create("nfsd", "proc2", NULL, "nfsv2rpc", NULL, "NFS v2 Server Remote Procedure Calls", "calls/s", 5009, update_every, RRDSET_TYPE_STACKED);

            for(i = 0; nfsd_proc2_values[i].present ; i++)
                rrddim_add(st, nfsd_proc2_values[i].name, NULL, 1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        for(i = 0; nfsd_proc2_values[i].present ; i++)
            rrddim_set(st, nfsd_proc2_values[i].name, nfsd_proc2_values[i].value);

        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_proc3 == 2) {
        unsigned int i;
        st = rrdset_find_bytype("nfsd", "proc3");
        if(!st) {
            st = rrdset_create("nfsd", "proc3", NULL, "nfsv3rpc", NULL, "NFS v3 Server Remote Procedure Calls", "calls/s", 5010, update_every, RRDSET_TYPE_STACKED);

            for(i = 0; nfsd_proc3_values[i].present ; i++)
                rrddim_add(st, nfsd_proc3_values[i].name, NULL, 1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        for(i = 0; nfsd_proc3_values[i].present ; i++)
            rrddim_set(st, nfsd_proc3_values[i].name, nfsd_proc3_values[i].value);

        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_proc4 == 2) {
        unsigned int i;
        st = rrdset_find_bytype("nfsd", "proc4");
        if(!st) {
            st = rrdset_create("nfsd", "proc4", NULL, "nfsv4rpc", NULL, "NFS v4 Server Remote Procedure Calls", "calls/s", 5011, update_every, RRDSET_TYPE_STACKED);

            for(i = 0; nfsd_proc4_values[i].present ; i++)
                rrddim_add(st, nfsd_proc4_values[i].name, NULL, 1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        for(i = 0; nfsd_proc4_values[i].present ; i++)
            rrddim_set(st, nfsd_proc4_values[i].name, nfsd_proc4_values[i].value);

        rrdset_done(st);
    }

    // --------------------------------------------------------------------

    if(do_proc4ops == 2) {
        unsigned int i;
        st = rrdset_find_bytype("nfsd", "proc4ops");
        if(!st) {
            st = rrdset_create("nfsd", "proc4ops", NULL, "nfsv2ops", NULL, "NFS v4 Server Operations", "operations/s", 5012, update_every, RRDSET_TYPE_STACKED);

            for(i = 0; nfsd4_ops_values[i].present ; i++)
                rrddim_add(st, nfsd4_ops_values[i].name, NULL, 1, 1, RRDDIM_INCREMENTAL);
        }
        else rrdset_next(st);

        for(i = 0; nfsd4_ops_values[i].present ; i++)
            rrddim_set(st, nfsd4_ops_values[i].name, nfsd4_ops_values[i].value);

        rrdset_done(st);
    }

    return 0;
}

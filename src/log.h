#ifndef NETDATA_LOG_H
#define NETDATA_LOG_H 1

#define D_WEB_BUFFER        0x0000000000000001
#define D_WEB_CLIENT        0x0000000000000002
#define D_LISTENER          0x0000000000000004
#define D_WEB_DATA          0x0000000000000008
#define D_OPTIONS           0x0000000000000010
#define D_PROCNETDEV_LOOP   0x0000000000000020
#define D_RRD_STATS         0x0000000000000040
#define D_WEB_CLIENT_ACCESS 0x0000000000000080
#define D_TC_LOOP           0x0000000000000100
#define D_DEFLATE           0x0000000000000200
#define D_CONFIG            0x0000000000000400
#define D_PLUGINSD          0x0000000000000800
#define D_CHILDS            0x0000000000001000
#define D_EXIT              0x0000000000002000
#define D_CHECKS            0x0000000000004000
#define D_NFACCT_LOOP       0x0000000000008000
#define D_PROCFILE          0x0000000000010000
#define D_RRD_CALLS         0x0000000000020000
#define D_DICTIONARY        0x0000000000040000
#define D_MEMORY            0x0000000000080000
#define D_CGROUP            0x0000000000100000
#define D_REGISTRY          0x0000000000200000
#define D_VARIABLES         0x0000000000400000
#define D_HEALTH            0x0000000000800000
#define D_CONNECT_TO        0x0000000001000000
#define D_RRDHOST           0x0000000002000000
#define D_SYSTEM            0x8000000000000000

//#define DEBUG (D_WEB_CLIENT_ACCESS|D_LISTENER|D_RRD_STATS)
//#define DEBUG 0xffffffff
#define DEBUG (0)

extern uint64_t debug_flags;

extern const char *program_name;

extern int stdaccess_fd;
extern FILE *stdaccess;

extern const char *stdaccess_filename;
extern const char *stderr_filename;
extern const char *stdout_filename;

extern int access_log_syslog;
extern int error_log_syslog;
extern int output_log_syslog;

extern time_t error_log_throttle_period, error_log_throttle_period_backup;
extern unsigned long error_log_errors_per_period;
extern int error_log_limit(int reset);

extern void open_all_log_files();
extern void reopen_all_log_files();

#define error_log_limit_reset() do { error_log_throttle_period = error_log_throttle_period_backup; error_log_limit(1); } while(0)
#define error_log_limit_unlimited() do { error_log_throttle_period = 0; } while(0)

#define debug(type, args...) do { if(unlikely(debug_flags & type)) debug_int(__FILE__, __FUNCTION__, __LINE__, ##args); } while(0)
#define info(args...)    info_int(__FILE__, __FUNCTION__, __LINE__, ##args)
#define infoerr(args...) error_int("INFO", __FILE__, __FUNCTION__, __LINE__, ##args)
#define error(args...)   error_int("ERROR", __FILE__, __FUNCTION__, __LINE__, ##args)
#define fatal(args...)   fatal_int(__FILE__, __FUNCTION__, __LINE__, ##args)

extern void log_date(FILE *out);
extern void debug_int( const char *file, const char *function, const unsigned long line, const char *fmt, ... ) PRINTFLIKE(4, 5);
extern void info_int( const char *file, const char *function, const unsigned long line, const char *fmt, ... ) PRINTFLIKE(4, 5);
extern void error_int( const char *prefix, const char *file, const char *function, const unsigned long line, const char *fmt, ... ) PRINTFLIKE(5, 6);
extern void fatal_int( const char *file, const char *function, const unsigned long line, const char *fmt, ... ) NORETURN PRINTFLIKE(4, 5);
extern void log_access( const char *fmt, ... ) PRINTFLIKE(1, 2);

#endif /* NETDATA_LOG_H */

#ifndef NETDATA_SIMPLE_PATTERN_H
#define NETDATA_SIMPLE_PATTERN_H

typedef enum {
    NETDATA_SIMPLE_PATTERN_MODE_EXACT,
    NETDATA_SIMPLE_PATTERN_MODE_PREFIX,
    NETDATA_SIMPLE_PATTERN_MODE_SUFFIX,
    NETDATA_SIMPLE_PATTERN_MODE_SUBSTRING
} NETDATA_SIMPLE_PREFIX_MODE;

typedef void NETDATA_SIMPLE_PATTERN;
extern NETDATA_SIMPLE_PATTERN *netdata_simple_pattern_list_create(const char *list, NETDATA_SIMPLE_PREFIX_MODE default_mode);
extern int netdata_simple_pattern_list_matches(NETDATA_SIMPLE_PATTERN *list, const char *str);
extern void netdata_simple_pattern_free(NETDATA_SIMPLE_PATTERN *list);

#endif //NETDATA_SIMPLE_PATTERN_H

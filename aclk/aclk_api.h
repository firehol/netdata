// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ACLK_API_H
#define ACLK_API_H

#include "libnetdata/libnetdata.h"

#include "aclk_proxy.h"

// TODO get rid global vars as soon as
// ACLK Legacy is removed
extern int aclk_connected;
extern int aclk_kill_link;

extern usec_t aclk_session_us;
extern time_t aclk_session_sec;

extern int aclk_disable_runtime;
extern int aclk_disable_single_updates;

extern int aclk_stats_enabled;

#ifdef ENABLE_ACLK
void *aclk_starter(void *ptr);

void aclk_single_update_disable();
void aclk_single_update_enable();

void aclk_alarm_reload(void);

int aclk_update_chart(RRDHOST *host, char *chart_name, int create);
int aclk_update_alarm(RRDHOST *host, ALARM_ENTRY *ae);

void aclk_add_collector(RRDHOST *host, const char *plugin_name, const char *module_name);
void aclk_del_collector(RRDHOST *host, const char *plugin_name, const char *module_name);

#define NETDATA_ACLK_HOOK                                                                                              \
    { .name = "ACLK_Main",                                                                                             \
      .config_section = NULL,                                                                                          \
      .config_name = NULL,                                                                                             \
      .enabled = 1,                                                                                                    \
      .thread = NULL,                                                                                                  \
      .init_routine = NULL,                                                                                            \
      .start_routine = aclk_starter },

#endif

struct label *add_aclk_host_labels(struct label *label);

#endif /* ACLK_API_H */

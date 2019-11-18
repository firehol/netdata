// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEST_EXPORTING_ENGINE_H
#define TEST_EXPORTING_ENGINE_H 1

#include "libnetdata/libnetdata.h"

#include "exporting/exporting_engine.h"
#include "exporting/graphite/graphite.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#define MAX_LOG_LINE 1024
extern char log_line[];

// -----------------------------------------------------------------------
// wraps for Netdata functions

const char *__wrap_strdupz(const char *s);
void __wrap_info_int(const char *file, const char *function, const unsigned long line, const char *fmt, ...);
int __wrap_connect_to_one_of(
    const char *destination,
    int default_port,
    struct timeval *timeout,
    size_t *reconnects_counter,
    char *connected_to,
    size_t connected_to_size);
void __rrdhost_check_rdlock(RRDHOST *host, const char *file, const char *function, const unsigned long line);
void __rrdset_check_rdlock(RRDSET *st, const char *file, const char *function, const unsigned long line);
void __rrd_check_rdlock(const char *file, const char *function, const unsigned long line);

// -----------------------------------------------------------------------
// wraps for system functions

void __wrap_uv_thread_create(uv_thread_t thread, void (*worker)(void *arg), void *arg);
void __wrap_uv_mutex_lock(uv_mutex_t *mutex);
void __wrap_uv_mutex_unlock(uv_mutex_t *mutex);
void __wrap_uv_cond_signal(uv_cond_t *cond_var);
void __wrap_uv_cond_wait(uv_cond_t *cond_var, uv_mutex_t *mutex);
ssize_t __wrap_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t __wrap_send(int sockfd, const void *buf, size_t len, int flags);

// -----------------------------------------------------------------------
// doubles and originals for exporting engine functions

struct engine *__real_read_exporting_config();
struct engine *__wrap_read_exporting_config();
struct engine *__mock_read_exporting_config();

int __real_init_connectors(struct engine *engine);
int __wrap_init_connectors(struct engine *engine);

int __real_mark_scheduled_instances(struct engine *engine);
int __wrap_mark_scheduled_instances(struct engine *engine);

int __real_prepare_buffers(struct engine *engine);
int __wrap_prepare_buffers(struct engine *engine);

int __wrap_notify_workers(struct engine *engine);

int __wrap_send_internal_metrics(struct engine *engine);

int __mock_start_batch_formatting(struct instance *instance);
int __mock_start_host_formatting(struct instance *instance);
int __mock_start_chart_formatting(struct instance *instance);
int __mock_metric_formatting(struct instance *instance, RRDDIM *rd);
int __mock_end_chart_formatting(struct instance *instance);
int __mock_end_host_formatting(struct instance *instance);
int __mock_end_batch_formatting(struct instance *instance);

// -----------------------------------------------------------------------
// fixtures

int setup_configured_engine(void **state);
int teardown_configured_engine(void **state);
int setup_rrdhost();
int teardown_rrdhost();
int setup_initialized_engine(void **state);
int teardown_initialized_engine(void **state);

void init_connectors_in_tests(struct engine *engine);

#endif /* TEST_EXPORTING_ENGINE_H */

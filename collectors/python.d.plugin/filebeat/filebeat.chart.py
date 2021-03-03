# -*- coding: utf-8 -*-
# Description: filebeat search node stats netdata python.d module
# Author: zarak, ilyam8
# SPDX-License-Identifier: GPL-3.0-or-later

import json

from collections import namedtuple
from socket import gethostbyname, gaierror

from bases.FrameworkServices.UrlService import UrlService

# default module values (can be overridden per job in `config`)
update_every = 5

METHODS = namedtuple('METHODS', ['get_data', 'url'])

NODE_STATS = [
    'memstats.memory_alloc',
    'memstats.memory_total',
    'memstats.rss',
    'info.uptime.ms',
    'runtime.goroutines',
    'cpu.system.time.ms',
    'cpu.total.time.ms',
    'cpu.user.time.ms',
    'events.active',
    'events.added',
    'events.done',
    'harvester.closed',
    'harvester.open_files',
    'harvester.running',
    'harvester.skipped',
    'harvester.started',
    'input.log.files.renamed',
    'input.log.files.truncated',
    'output.events.acked',
    'output.events.active',
    'output.events.batches',
    'output.events.duplicates',
    'output.events.failed',
    'output.read.bytes',
    'output.read.errors',
    'output.write.bytes',
    'output.write.errors',
    'pipeline.clients',
    'pipeline.events.active',
    'pipeline.events.dropped',
    'pipeline.events.failed',
    'pipeline.events.filtered',
    'pipeline.events.published',
    'pipeline.events.retry',
    'pipeline.queue.acked',
]

# charts order (can be overridden if you want less charts, or different order)
ORDER = [
    'memory_used',
    'memory_rss',
    'cpu',
    'uptime',
    'goroutines',
    'events',
    'events_rates',
    'harvester',
    'harvester_rates',
    'input_files_events',
    'output_events',
    'output_events_rates',
    'output_read',
    'output_read_rates',
    'output_write',
    'output_write_rates',
    'pipeline_clients',
    'pipeline_events',
    'pipeline_events_rates',
    'pipeline_queue',
]

CHARTS = {
    'memory_used': {
                   # name, title, units, family, context, charttype
        'options': [None, 'Memory used', 'KiB', 'memory',
                    'filebeat.memory', 'area'],
        'lines': [
            ['memstats_memory_alloc', 'Allocated memory', 'absolute', 1, 1024],
            ['memstats_memory_total', 'Total memory', 'absolute', 1, 1024]
        ]
    },
    'memory_rss': {
        'options': [None, 'RSS memory', 'KiB', 'memory', 'filebeat.memory_rss', 'area'],
        'lines': [
            ['memstats_rss', 'RSS memory', 'absolute', 1, 1024],
        ]
    },
    'uptime': {
        'options': [None, 'Uptime', 's', 'uptime', 'filebeat.uptime', 'line'],
        'lines': [
            ['info_uptime_ms', 'uptime', 'absolute', 1, 1000],
        ]
    },
    'goroutines': {
        'options': [None, 'Number of goroutines', 'goroutine', 'goroutine',
                    'filebeat.goroutines', 'line'],
        'lines': [
            ['runtime_goroutines', 'goroutines', 'absolute'],
        ]
    },
    'cpu': {
        'options': [None, 'CPU used', 'ms', 'cpu', 'filebeat.cpu', 'line'],
        'lines': [
            ['cpu_system_time_ms', 'System time', 'absolute'],
            ['cpu_total_time_ms', 'Total time', 'absolute'],
            ['cpu_user_time_ms', 'User time', 'absolute'],
        ]
    },
    'events': {
        'options': [None, 'Number of events', 'events', 'events', 'filebeat.events', 'line'],
        'lines': [
            ['events_active', 'Active events', 'absolute'],
            ['events_added', 'Added events', 'absolute'],
            ['events_done', 'Done events', 'absolute'],
        ]
    },
    'events_rates': {
        'options': [None, 'Events rates', 'events/s', 'events', 'filebeat.events_rates', 'line'],
        'lines': [
            ['events_active', 'Active events', 'incremental'],
            ['events_added', 'Added events', 'incremental'],
            ['events_done', 'Done events', 'incremental'],
        ]
    },
    'harvester': {
        'options': [None, 'Harverster stats', 'files', 'harverster', 'filebeat.harvester', 'line'],
        'lines': [
            ['harvester_closed', 'Closed files', 'absolute'],
            ['harvester_open_files', 'Open files', 'absolute'],
            ['harvester_running', 'Running', 'absolute'],
            ['harvester_skipped', 'Skipped', 'absolute'],
            ['harvester_started', 'Started', 'absolute'],
        ]
    },
    'harvester_rates': {
        'options': [None, 'Harverster stats per second', 'files/s', 'harverster', 'filebeat.harvester_rates', 'line'],
        'lines': [
            ['harvester_closed', 'Closed files', 'incremental'],
            ['harvester_open_files', 'Open files', 'incremental'],
            ['harvester_running', 'Running', 'incremental'],
            ['harvester_skipped', 'Skipped', 'incremental'],
            ['harvester_started', 'Started', 'incremental'],
        ]
    },
    'input_files_events': {
        'options': [None, 'Input files rotated/truncated', 'files', 'harverster', 'filebeat.input_files_events', 'line'],
        'lines': [
            ['input_log_files_truncated', 'Truncated files', 'absolute'],
            ['input_log_files_rotated', 'Rotated files', 'absolute'],
        ]
    },
    'output_events': {
        'options': [None, 'Output events', 'events', 'output', 'filebeat.output_events', 'line'],
        'lines': [
            ['output_events_acked', 'Acked', 'absolute'],
            ['output_events_active', 'Active', 'absolute'],
            ['output_events_batches', 'Batches', 'absolute'],
            ['output_events_duplicates', 'Duplicates', 'absolute'],
            ['output_events_failed', 'Failed', 'absolute'],
        ]
    },
    'output_events_rates': {
        'options': [None, 'Output events rates', 'events/s', 'output', 'filebeat.output_events_rates', 'line'],
        'lines': [
            ['output_events_acked', 'Acked', 'incremental'],
            ['output_events_active', 'Active', 'incremental'],
            ['output_events_batches', 'Batches', 'incremental'],
            ['output_events_duplicates', 'Duplicates', 'incremental'],
            ['output_events_failed', 'Failed', 'incremental'],
        ]
    },
    'output_read': {
        'options': [None, 'Output read', None, 'output', 'filebeat.output_read', 'line'],
        'lines': [
            ['output_read_bytes', 'KiB', 'absolute', 1, 1024],
            ['output_read_errors', 'Number of errors', 'absolute'],
        ]
    },
    'output_read_rates': {
        'options': [None, 'Output read rates', None, 'output', 'filebeat.output_read_rates', 'line'],
        'lines': [
            ['output_read_bytes', 'KiB/s', 'incremental', 1, 1024],
            ['output_read_errors', 'Number of errors/s', 'incremental'],
        ]
    },
    'output_write': {
        'options': [None, 'Output write', None, 'output', 'filebeat.output_write', 'line'],
        'lines': [
            ['output_write_bytes', 'KiB', 'absolute', 1, 1024],
            ['output_write_errors', 'Number of errors', 'absolute'],
        ]
    },
    'output_write_rates': {
        'options': [None, 'Output write rates', None, 'output', 'filebeat.output_write_rates', 'line'],
        'lines': [
            ['output_write_bytes', 'KiB/s', 'incremental', 1, 1024],
            ['output_write_errors', 'Number of errors/s', 'incremental'],
        ]
    },
    'pipeline_clients': {
        'options': [None, 'Number of pipeline clients', 'clients', 'pipeline', 'filebeat.pipeline_clients', 'line'],
        'lines': [
            ['pipeline_clients', 'clients', 'absolute'],
        ]
    },
    'pipeline_events': {
        'options': [None, 'Number of pipeline events', 'events', 'pipeline', 'filebeat.pipeline_events', 'line'],
        'lines': [
            ['pipeline_events_active', 'Active', 'absolute'],
            ['pipeline_events_dropped', 'Dropped', 'absolute'],
            ['pipeline_events_failed', 'Failed', 'absolute'],
            ['pipeline_events_filtered', 'Filtered', 'absolute'],
            ['pipeline_events_published', 'Published', 'absolute'],
            ['pipeline_events_retry', 'Retry', 'absolute'],
        ]
    },
    'pipeline_events_rates': {
        'options': [None, 'Pipeline events rates', 'events/s', 'pipeline', 'filebeat.pipeline_events_rates', 'line'],
        'lines': [
            ['pipeline_events_active', 'Active', 'incremental'],
            ['pipeline_events_dropped', 'Dropped', 'incremental'],
            ['pipeline_events_failed', 'Failed', 'incremental'],
            ['pipeline_events_filtered', 'Filtered', 'incremental'],
            ['pipeline_events_published', 'Published', 'incremental'],
            ['pipeline_events_retry', 'Retry', 'incremental'],
        ]
    },
    'pipeline_queue': {
        'options': [None, 'Pipeline queue', 'events/s', 'pipeline', 'filebeat.pipeline_queue', 'line'],
        'lines': [
            ['pipeline_queue_acked', 'Ack/s', 'incremental'],
        ]
    },
}

def get_survive_any(method):
    def w(*args):
        try:
            return method(*args)
        except Exception as error:
            self, url = args[0], args[2]
            self.error("error during '{0}' : {1}".format(url, error))

    return w


class Service(UrlService):
    def __init__(self, configuration=None, name=None):
        UrlService.__init__(self, configuration=configuration, name=name)
        self.order = ORDER
        self.definitions = CHARTS
        self.host = self.configuration.get('host', "127.0.0.1")
        self.port = self.configuration.get('port', 5066)
        self.url = '{scheme}://{host}:{port}'.format(
            scheme=self.configuration.get('scheme', 'http'),
            host=self.host,
            port=self.port,
        )
        self.latency = dict()
        self.methods = list()
        self.collected_pipelines = set()

    def check(self):
        if not self.host:
            self.error('Host is not defined in the module configuration file')
            return False

        try:
            self.host = gethostbyname(self.host)
        except gaierror as error:
            self.error(repr(error))
            return False

        self.methods = [
            METHODS(
                get_data=self._get_node_stats,
                url=self.url + '/stats',
            ),
        ]
        return UrlService.check(self)

    def _get_data(self):
        result = dict()

        for method in self.methods:
            sub_data = method.get_data(method.url)
            assert sub_data is not None
            result.update(sub_data)

        self.debug(result)
        return result or None

    def add_pipeline_to_charts(self, idx_name):
        # FIXME
        for name in ('index_docs_count', 'index_store_size', 'index_replica', 'index_health'):
            chart = self.charts[name]
            dim = ['{0}_{1}'.format(idx_name, name), idx_name]
            chart.add_dimension(dim)

    @get_survive_any
    def _get_node_stats(self, url):
        raw = self._get_raw_data(url)
        if not raw:
            return dict()

        parsed = self.json_parse(raw)
        if not parsed:
            return dict()

        data = fetch_data(raw_data=parsed['beat'], metrics=NODE_STATS)
        data.update(fetch_data(raw_data=parsed['filebeat'], metrics=NODE_STATS))
        data.update(fetch_data(raw_data=parsed['libbeat'], metrics=NODE_STATS))

        return data

    def json_parse(self, reply):
        try:
            return json.loads(reply)
        except ValueError as err:
            self.error(err)
            return None

def fetch_data(raw_data, metrics):
    data = dict()
    for metric in metrics:
        value = raw_data
        metrics_list = metric.split('.')
        try:
            for m in metrics_list:
                value = value[m]
        except (KeyError, TypeError):
            continue
        data['_'.join(metrics_list)] = value
    return data

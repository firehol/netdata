# -*- coding: utf-8 -*-
# Description: filebeat search node stats netdata python.d module
# Author: zarak, ilyam8
# SPDX-License-Identifier: GPL-3.0-or-later

import json
import threading

from collections import namedtuple
from socket import gethostbyname, gaierror

try:
    from queue import Queue
except ImportError:
    from Queue import Queue

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
]


# charts order (can be overridden if you want less charts, or different order)
ORDER = [
    'memory_used',
    'memory_rss',
    'uptime',
    'goroutines',
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
        'options': [None, 'RSS memory', 'KiB', 'memory',
                    'filebeat.memory_rss', 'area'],
        'lines': [
            ['memstats_rss', 'RSS memory', 'absolute', 1, 1024],
        ]
    },
    'uptime': {
        'options': [None, 'Uptime', 's', 'uptime',
                    'filebeat.uptime', 'line'],
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
        self.port = self.configuration.get('port', 9200)
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

        if 'process_open_file_descriptors' in data and 'process_max_file_descriptors' in data:
            v = float(data['process_open_file_descriptors']) / data['process_max_file_descriptors'] * 1000
            data['file_descriptors_used'] = round(v)

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

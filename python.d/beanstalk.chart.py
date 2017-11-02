# -*- coding: utf-8 -*-
# Description: beanstalk netdata python.d module
# Author: l2isbad

from collections import defaultdict
from sys import version_info

try:
    import beanstalkc
    BEANSTALKC = True
except ImportError:
    BEANSTALKC = False


if version_info[:2] > (3, 1):
    import pyyaml3 as yaml
else:
    import pyyaml2 as yaml


from bases.FrameworkServices.SimpleService import SimpleService

# default module values (can be overridden per job in `config`)
# update_every = 2
priority = 60000
retries = 60


def chart_template(name):
    order = ['{0}_jobs_rate'.format(name),
             '{0}_jobs'.format(name),
             '{0}_connections'.format(name),
             '{0}_commands'.format(name),
             '{0}_pause'.format(name)
             ]
    family = 'tube {0}'.format(name)

    charts = {
        order[0]: {
            'options': [None, 'Job Rate', 'jobs/s', family, 'beanstalk.jobs_rate', 'area'],
            'lines': [
                ['_'.join([name, 'total-jobs']), 'jobs', 'incremental']
            ]},
        order[1]: {
            'options': [None, 'Jobs', 'jobs', family, 'beanstalk.jobs', 'stacked'],
            'lines': [
                ['_'.join([name, 'current-jobs-urgent']), 'urgent'],
                ['_'.join([name, 'current-jobs-ready']), 'ready'],
                ['_'.join([name, 'current-jobs-reserved']), 'reserved'],
                ['_'.join([name, 'current-jobs-delayed']), 'delayed'],
                ['_'.join([name, 'current-jobs-buried']), 'buried']
            ]},
        order[2]: {
            'options': [None, 'Connections', 'connections', family, 'beanstalk.connections', 'stacked'],
            'lines': [
                ['_'.join([name, 'current-using']), 'using'],
                ['_'.join([name, 'current-waiting']), 'waiting'],
                ['_'.join([name, 'current-watching']), 'watching']
            ]},
        order[3]: {
            'options': [None, 'Commands', 'command/s', family, 'beanstalk.commands', 'stacked'],
            'lines': [
                ['_'.join([name, 'cmd-delete']), 'deletes', 'incremental'],
                ['_'.join([name, 'cmd-pause-tube']), 'pauses', 'incremental']
            ]},
        order[4]: {
            'options': [None, 'Pause', 'seconds', family, 'beanstalk.pause', 'stacked'],
            'lines': [
                ['_'.join([name, 'pause']), 'since'],
                ['_'.join([name, 'pause-time-left']), 'left']
            ]}

    }

    return order, charts


class Service(SimpleService):
    def __init__(self, configuration=None, name=None):
        SimpleService.__init__(self, configuration=configuration, name=name)
        self.configuration = configuration
        self.conn = self.connect()
        self.order = list()
        self.definitions = dict()
        self.alive = True

    def check(self):
        if not BEANSTALKC:
            self.error("'beanstalkc' module is needed to use beanstalk.chart.py")
            return False

        if not self.conn:
            return False

        for tube in self.conn.tubes():
            order, charts = chart_template(tube)
            self.order.extend(order)
            self.definitions.update(charts)

        return bool(self.order)

    def get_data(self):
        """
        Format data received from shell command
        :return: dict
        """
        if not self.is_alive():
            return None
        else:
            self.alive = True

        tubes_stats, data = defaultdict(dict), dict()
        try:
            for tube in self.conn.tubes():
                stats = self.conn.stats_tube(tube)
                for stat in stats:
                    dimension, value = '_'.join([tube, stat]),  stats[stat]
                    tubes_stats[tube][dimension] = value
        except beanstalkc.SocketError:
            self.alive = False
            return None

        for tube in tubes_stats:
            if tube + '_jobs' not in self.charts.active_charts():
                self.create_new_tube_charts(tube)

            data.update(tubes_stats[tube])

        return data or None

    def create_new_tube_charts(self, tube):
        order, charts = chart_template(tube)

        for chart_name in order:
            params = [chart_name] + charts[chart_name]['options']
            dimensions = charts[chart_name]['lines']

            new_chart = self.charts.add_chart(params)
            for dimension in dimensions:
                new_chart.add_dimension(dimension)

    def connect(self):
        host = self.configuration.get('host', '127.0.0.1')
        port = self.configuration.get('port', 11300)
        try:
            return beanstalkc.Connection(host=host,
                                         port=port)
        except beanstalkc.SocketError as error:
            self.error('Connection to {0}:{1} failed: {2}'.format(host, port, error))
            return None

    def reconnect(self):
        try:
            self.conn.reconnect()
            return True
        except beanstalkc.SocketError:
            return False

    def is_alive(self):
        if not self.alive:
            return self.reconnect()
        return True

# -*- coding: utf-8 -*-
# Description: redis netdata python.d module
# Author: Pawel Krupa (paulfantom)

from base import SocketService

# default module values (can be overridden per job in `config`)
#update_every = 2
priority = 60000
retries = 60

# default job configuration (overridden by python.d.plugin)
# config = {'local': {
#             'update_every': update_every,
#             'retries': retries,
#             'priority': priority,
#             'host': 'localhost',
#             'port': 6379,
#             'unix_socket': None
#          }}

ORDER = ['operations', 'hit_rate', 'memory', 'keys', 'connections', 'clients', 'slaves', 'persistence']

CHARTS = {
    'operations': {
        'options': [None, 'Operations', 'operations/s', 'operations', 'redis.operations', 'line'],
        'lines': [
            ['total_commands_processed', 'commands', 'incremental'],
            ['instantaneous_ops_per_sec', 'operations', 'absolute']
        ]},
    'hit_rate': {
        'options': [None, 'Hit rate', 'percent', 'hits', 'redis.hit_rate', 'line'],
        'lines': [
            ['hit_rate', 'rate', 'absolute']
        ]},
    'memory': {
        'options': [None, 'Memory utilization', 'kilobytes', 'memory', 'redis.memory', 'line'],
        'lines': [
            ['used_memory', 'total', 'absolute', 1, 1024],
            ['used_memory_lua', 'lua', 'absolute', 1, 1024]
        ]},
    'keys': {
        'options': [None, 'Database keys', 'keys', 'keys', 'redis.keys', 'line'],
        'lines': [
            # lines are created dynamically in `check()` method
        ]},
    'connections': {
        'options': [None, 'Connections', 'connections/s', 'connections', 'redis.connections', 'line'],
        'lines': [
            ['total_connections_received', 'received', 'incremental'],
            ['rejected_connections', 'rejected', 'incremental']
        ]},
    'clients': {
        'options': [None, 'Clients', 'clients', 'connections', 'redis.clients', 'line'],
        'lines': [
            ['connected_clients', 'connected', 'absolute'],
            ['blocked_clients', 'blocked', 'absolute']
        ]},
    'slaves': {
        'options': [None, 'Slaves', 'slaves', 'replication', 'redis.slaves', 'line'],
        'lines': [
            ['connected_slaves', 'connected', 'absolute']
        ]},
    'persistence': {
        'options': [None, 'Changes since last save', 'changes', 'persistence', 'redis.rdb_changes', 'line'],
        'lines': [
            ['rdb_changes_since_last_save', 'changes', 'absolute']
        ]}
}


class Service(SocketService):
    def __init__(self, configuration=None, name=None):
        SocketService.__init__(self, configuration=configuration, name=name)
        self.request = "INFO\r\n"
        self.host = "localhost"
        self.port = 6379
        self.unix_socket = None
        self.order = ORDER
        self.definitions = CHARTS
        self._keep_alive = True
        self.chart_name = ""

    def _get_data(self):
        """
        Get data from socket
        :return: dict
        """
        response = self._get_raw_data()
        if response is None:
            # error has already been logged
            return None

        try:
            parsed = response.split("\n")
        except AttributeError:
            self.error("response is invalid/empty")
            return None

        data = {}
        for line in parsed:
            if len(line) < 5 or line[0] == '$' or line[0] == '#':
                continue

            if line.startswith('db'):
                tmp = line.split(',')[0].replace('keys=', '')
                record = tmp.split(':')
                data[record[0]] = record[1]
                continue

            try:
                t = line.split(':')
                data[t[0]] = t[1]
            except (IndexError, ValueError):
                self.debug("invalid line received: " + str(line))
                pass

        if len(data) == 0:
            self.error("received data doesn't have any records")
            return None

        try:
            data['hit_rate'] = (int(data['keyspace_hits']) * 100) / (int(data['keyspace_hits']) + int(data['keyspace_misses']))
        except:
            data['hit_rate'] = 0

        return data

    def _check_raw_data(self, data):
        """
        Check if all data has been gathered from socket.
        Parse first line containing message length and check against received message
        :param data: str
        :return: boolean
        """
        length = len(data)
        supposed = data.split('\n')[0][1:]
        offset = len(supposed) + 4  # 1 dollar sing, 1 new line character + 1 ending sequence '\r\n'
        supposed = int(supposed)

        if length - offset >= supposed:
            self.debug("received full response from redis")
            return True

        self.debug("waiting more data from redis")
        return False

    def check(self):
        """
        Parse configuration, check if redis is available, and dynamically create chart lines data
        :return: boolean
        """
        self._parse_config()
        if self.name == "":
            self.name = "local"
            self.chart_name += "_" + self.name
        data = self._get_data()
        if data is None:
            return False

        for name in data:
            if name.startswith('db'):
                self.definitions['keys']['lines'].append([name, None, 'absolute'])

        return True

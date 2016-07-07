# -*- coding: utf-8 -*-
# Description: postfix netdata python.d plugin
# Author: Pawel Krupa (paulfantom)

from base import ExecutableService

# default module values (can be overridden per job in `config`)
# update_every = 2
priority = 60000
retries = 5

# default job configuration (overridden by python.d.plugin)
# config = {'local': {
#             'update_every': update_every,
#             'retries': retries,
#             'priority': priority,
#             'url': 'http://localhost/stub_status'
#          }}

# charts order (can be overridden if you want less charts, or different order)
ORDER = ['qemails', 'qsize']

CHARTS = {
    'qemails': {
        'options': [None, "Postfix Queue Emails", "emails", 'queue', 'postfix.queued.emails', 'line'],
        'lines': [
            ['emails', None, 'absolute']
        ]},
    'qsize': {
        'options': [None, "Postfix Queue Emails Size", "emails size in KB", 'queue', 'postfix.queued.size', 'area'],
        'lines': [
            ["size", None, 'absolute']
        ]}
}


class Service(ExecutableService):
    def __init__(self, configuration=None, name=None):
        ExecutableService.__init__(self, configuration=configuration, name=name)
        self.command = "postqueue -p"
        self.order = ORDER
        self.definitions = CHARTS

    def _get_data(self):
        """
        Format data received from shell command
        :return: dict
        """
        try:
            raw = self._get_raw_data()[-1].split(' ')
            return {'emails': raw[4],
                    'size': raw[1]}
        except (ValueError, AttributeError):
            return None

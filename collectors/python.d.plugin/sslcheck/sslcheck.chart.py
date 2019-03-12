# -*- coding: utf-8 -*-
# Description: simple ssl expiration check netdata python.d module
# Original Author: Peter Thurner (github.com/p-thurner)
# SPDX-License-Identifier: GPL-3.0-or-later

import datetime
import socket
import ssl

from bases.FrameworkServices.SimpleService import SimpleService


ORDER = [
    'time_until_expiration',
]

CHARTS = {
        'time_until_expiration': {
            'options': [
                None,
                'Time Until Certificate Expiration',
                'seconds',
                'certificate expiration',
                'sslcheck.time_until_expiration',
                'line',
            ],
            'lines': [
                ['time'],
            ]
        },
}


SSL_DATE_FMT = r'%b %d %H:%M:%S %Y %Z'


class Service(SimpleService):
    def __init__(self, configuration=None, name=None):
        SimpleService.__init__(self, configuration=configuration, name=name)
        self.order = ORDER
        self.definitions = CHARTS
        self.host = configuration.get('host')
        self.port = configuration.get('port', 443)
        self.timeout = configuration.get('timeout', 3)

    def check(self):
        if not self.host:
            self.error('host parameter is mandatory, but it is not set')
            return False

        self.debug('run check : host {host}:{port}, update every {update}s, timeout {timeout}s'.format(
            host=self.host, port=self.port, update=self.update_every, timeout=self.timeout))

        return True

    def get_data(self):
        conn = create_ssl_conn(self.host, self.timeout)

        try:
            conn.connect((self.host, self.port))
        except Exception as error:
            self.error("error on connection to {0}:{1} : {2}".format(self.host, self.port, error))
            return None

        peer_cert = conn.getpeercert()
        conn.close()

        if peer_cert is None:
            self.warning("no certificate was provided by {0}:{1}".format(self.host, self.port))
            return None
        elif not peer_cert:
            self.warning("certificate was provided by {0}:{1}, but not validated".format(self.host, self.port))
            return None

        return {
            'time': calc_cert_expiration_time(peer_cert).seconds
        }


def create_ssl_conn(hostname, timeout):
    context = ssl.create_default_context()
    conn = context.wrap_socket(
        socket.socket(socket.AF_INET),
        server_hostname=hostname,
    )
    conn.settimeout(timeout)

    return conn


def calc_cert_expiration_time(cert):
    expiration_date = datetime.datetime.strptime(cert['notAfter'], SSL_DATE_FMT)
    current_date = datetime.datetime.utcnow()

    return expiration_date - current_date

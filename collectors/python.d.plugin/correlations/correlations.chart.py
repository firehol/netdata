# -*- coding: utf-8 -*-
# Description: correlations netdata python.d module
# Author: andrewm4894
# SPDX-License-Identifier: GPL-3.0-or-later

from random import SystemRandom, randint

from bases.FrameworkServices.SimpleService import SimpleService

import requests
import pandas as pd


priority = 3

ORDER = [
    'correlations',
]

CHARTS = {
    'correlations': {
        'options': [None, 'Cross Correlations', 'value', 'correlations', 'correlations.correlations', 'line'],
        'lines': [
        ]
    },
}


HOST_PORT = '127.0.0.1:19999'
N = 10
CHARTS_IN_SCOPE = [
    'system.cpu', 'system.load', 'system.ram', 'system.io', 'system.pgpgio', 'system.net', 'system.ip', 'system.ipv6',
    'system.processes', 'system.intr', 'system.forks', 'system.softnet_stat'
]


def get_allmetrics(host: str = None, charts: list = None):
    url = f'http://{host}/api/v1/allmetrics?format=json'
    response = requests.get(url)
    raw_data = response.json()
    data = []
    for k in raw_data:
        if k in charts:
            time = raw_data[k]['last_updated']
            dimensions = raw_data[k]['dimensions']
            for dimension in dimensions:
                data.append([time, k, f"{k}.{dimensions[dimension]['name']}", dimensions[dimension]['value']])
    return data


def data_to_df(data):
    df = pd.DataFrame([item for sublist in data for item in sublist], columns=['time', 'chart', 'variable', 'value'])
    return df


def df_long_to_wide(df):
    df = df.drop_duplicates().pivot(index='time', columns='variable', values='value').ffill()
    return df


def get_raw_data(host: str = None, after: int = 500, charts: list = None) -> pd.DataFrame:

    df = pd.DataFrame(columns=['time'])

    # get all relevant data
    for chart in charts:

        # get data
        url = f'http://{host}/api/v1/data?chart={chart}&after=-{after}&format=json'
        response = requests.get(url)
        response_json = response.json()
        raw_data = response_json['data']

        # create df
        df_chart = pd.DataFrame(raw_data, columns=response_json['labels'])
        df_chart = df_chart.rename(
            columns={col: f'{chart}.{col}' for col in df_chart.columns[df_chart.columns != 'time']}
        )
        df = df.merge(df_chart, on='time', how='outer')

    df = df.set_index('time')
    df = df.ffill()

    return df


def process_data(self=None, df: pd.DataFrame = None) -> dict:

    data = dict()
    df = df.corr()
    df = df.rename_axis("var1", axis="index")
    df = df.rename_axis("var2", axis="columns")
    df = df.stack().to_frame().reset_index().rename(columns={0: 'value'})
    df['variable'] = df['var1'] + '__' + df['var2']
    df = df[df['var1'] != df['var2']]
    df = df[['variable', 'value']]
    df['idx'] = 1
    df = df.pivot(index='idx', columns='variable', values='value')

    for col in df.columns:
        if self:
            self.counter += 1
            if col not in self.charts['correlations']:
                self.charts['correlations'].add_dimension([col, None, 'absolute', 1, 1000])
        data[col] = df[col].values[0] * 1000

    return data


class Service(SimpleService):

    def __init__(self, configuration=None, name=None):
        SimpleService.__init__(self, configuration=configuration, name=name)
        self.order = ORDER
        self.definitions = CHARTS
        self.random = SystemRandom()
        self.counter = 1
        self.data = []

    @staticmethod
    def check():
        return True

    def append_data(self, data):
        self.data.append(data)

    def get_data(self):

        self.append_data(get_allmetrics(host=HOST_PORT, charts=CHARTS_IN_SCOPE))
        self.data = self.data[-N:]
        self.debug(f"length of self.data is {len(self.data)}")
        self.debug(self.data)
        df = data_to_df(self.data)
        self.debug(df.shape)
        self.debug(df.head(10))
        df = df_long_to_wide(df)
        self.debug(df.shape)
        self.debug(df.head(10))
        data = process_data(self, df)
        self.debug(data)

        return data


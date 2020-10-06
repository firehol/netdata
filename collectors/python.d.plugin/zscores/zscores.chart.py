# -*- coding: utf-8 -*-
# Description: zscores netdata python.d module
# Author: andrewm4894
# SPDX-License-Identifier: GPL-3.0-or-later

from datetime import datetime
from random import SystemRandom

import requests
import numpy as np
import pandas as pd

from bases.FrameworkServices.SimpleService import SimpleService
from netdata_pandas.data import get_data, get_allmetrics


priority = 2
update_every = 1

HOST_PORT = '127.0.0.1:19999'
CHARTS_IN_SCOPE = [
    'system.cpu', 'system.load'
]
N = 60*5
RECALC_EVERY = 60
ZSCORE_CLIP = 10

TRAIN_N_SECS = 60*60*4
OFFSET_N_SECS = 60*5
TRAIN_EVERY_N = 60*5
Z_SMOOTH_N = 5

ORDER = [
    'zscores',
    'zscores_3sigma'
]

CHARTS = {
    'zscores': {
        'options': [None, 'Z Scores', 'name.chart', 'zscores', 'zscores.zscores', 'line'],
        'lines': []
    },
    'zscores_3sigma': {
        'options': [None, 'Z Scores >3 Sigma', 'name.chart', 'zscores', 'zscores.zscores_3sigma', 'stacked'],
        'lines': []
    },
}


class Service(SimpleService):
    def __init__(self, configuration=None, name=None):
        SimpleService.__init__(self, configuration=configuration, name=name)
        self.order = ORDER
        self.definitions = CHARTS
        self.random = SystemRandom()
        self.data = []
        self.df_mean = pd.DataFrame()
        self.df_std = pd.DataFrame()
        self.df_z_history = pd.DataFrame()

    @staticmethod
    def check():
        return True

    def get_data(self):

        now = int(datetime.now().timestamp())
        after = now - OFFSET_N_SECS - TRAIN_N_SECS
        before = now - OFFSET_N_SECS

        if self.runs_counter % TRAIN_EVERY_N == 0:
            
            self.df_mean = get_data(HOST_PORT, charts=CHARTS_IN_SCOPE, after=after, before=before, points=1, group='average')
            self.df_mean = self.df_mean.transpose()
            self.df_mean.columns = ['mean']

            self.df_std = get_data(HOST_PORT, charts=CHARTS_IN_SCOPE, after=after, before=before, points=1, group='stddev')
            self.df_std = self.df_std.transpose()
            self.df_std.columns = ['std']

        df_allmetrics = get_allmetrics(HOST_PORT, charts=CHARTS_IN_SCOPE, wide=True).transpose()

        df_z = pd.concat([self.df_mean, self.df_std, df_allmetrics], axis=1, join='outer').dropna()
        df_z['z'] = (df_z['value'] - df_z['mean']) / df_z['std']
        df_z['z'] = df_z['z'].fillna(0)

        self.df_z_history = self.df_z_history.append(df_z).tail(Z_SMOOTH_N)

        df_z_smooth = self.df_z_history.reset_index().groupby('index')[['z']].mean() * 100
        df_z_smooth['3sig'] = np.where(abs(df_z_smooth['z']) >= 3, 1, 0)
        df_z_smooth.index = ['.'.join(reversed(x.replace('|', '.').split('.'))) + '_z' for x in df_z_smooth.index]

        data_dict_z = df_z_smooth['z'].to_dict()
        df_z_smooth.index = [x[:-2] + '_3sig' for x in df_z_smooth.index]

        data_dict_3sig = df_z_smooth['3sig'].to_dict()
        data_dict = {**data_dict_z, **data_dict_3sig}

        for dim in data_dict_z:
            if dim not in self.charts['zscores']:
                self.charts['zscores'].add_dimension([dim, dim, 'absolute', 1, 100])
        
        for dim in data_dict_3sig:
            if dim not in self.charts['zscores_3sigma']:
                self.charts['zscores_3sigma'].add_dimension([dim, dim, 'absolute', 1, 100])

        return data_dict

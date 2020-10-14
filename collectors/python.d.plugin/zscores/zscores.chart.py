# -*- coding: utf-8 -*-
# Description: zscores netdata python.d module
# Author: andrewm4894
# SPDX-License-Identifier: GPL-3.0-or-later

from datetime import datetime

import requests
import numpy as np
import pandas as pd

from bases.FrameworkServices.SimpleService import SimpleService
from netdata_pandas.data import get_data, get_allmetrics


priority = 50
update_every = 1

ORDER = [
    'z',
    '3sigma'
]

CHARTS = {
    'z': {
        'options': ['z', 'Z-Score', 'z', 'zscores', 'zscores.z', 'line'],
        'lines': []
    },
    '3sigma': {
        'options': ['3sigma', 'Z-Score >3', 'count', 'zscores', 'zscores.3sigma', 'stacked'],
        'lines': []
    },
}


class Service(SimpleService):
    def __init__(self, configuration=None, name=None):
        SimpleService.__init__(self, configuration=configuration, name=name)
        self.host = self.configuration.get('host')
        if self.configuration.get('charts_in_scope') == 'system.*':
            self.charts_in_scope = [c for c in requests.get(f'http://{self.host}/api/v1/charts').json()['charts'].keys() if c.startswith('system.')]
        else:
            self.charts_in_scope = self.configuration.get('charts_in_scope').split(',')
        self.train_secs = self.configuration.get('train_secs')
        self.offset_secs = self.configuration.get('offset_secs')
        self.train_every_n = self.configuration.get('train_every_n')
        self.z_smooth_n = self.configuration.get('z_smooth_n')
        self.z_clip = self.configuration.get('z_clip')
        self.z_abs = bool(self.configuration.get('z_abs'))
        self.burn_in = self.configuration.get('burn_in')
        self.mode = self.configuration.get('mode')
        self.per_chart_agg = self.configuration.get('per_chart_agg', 'absmax')
        self.order = ORDER
        self.definitions = CHARTS
        self.df_mean = pd.DataFrame()
        self.df_std = pd.DataFrame()
        self.df_z_history = pd.DataFrame()

    @staticmethod
    def check():
        return True

    def validate_charts(self, name, data, algorithm='absolute', multiplier=1, divisor=1):
        for dim in data:
            if dim not in self.charts[name]:
                self.charts[name].add_dimension([dim, dim, algorithm, multiplier, divisor])

    def train_model(self):
        """Calculate the mean and sigma for all relevant metrics and store them for use in calulcating zscore at each timestep.
        """

        # calculate the mean and sigma between the timestamps after to before
        before = int(datetime.now().timestamp()) - self.offset_secs
        after = before - self.train_secs

        # get means from rest api
        self.df_mean = get_data(
            self.host, self.charts_in_scope, after, before, points=10, group='average', col_sep='.'
            ).mean().to_frame().rename(columns={0: "mean"})

        # get sigmas from rest api
        self.df_std = get_data(
            self.host, self.charts_in_scope, after, before, points=10, group='stddev', col_sep='.'
            ).mean().to_frame().rename(columns={0: "std"})

    def create_data(self, df_allmetrics):
        """Use x, mean, sigma to generate z scores and 3sigma flags via some pandas manipulation.
        Returning two dictionaries of dimensions and measures, one for each chart.

        :param df_allmetrics <pd.DataFrame>: pandas dataframe with latest data from api/v1/allmetrics.
        :return: (<dict>,<dict>) tuple of dictionaries, one for  zscores and the other for a flag if abs(z)>3.
        """
        # calculate clipped z score for each available metric
        df_z = pd.concat([self.df_mean, self.df_std, df_allmetrics], axis=1, join='inner')
        df_z['z'] = ((df_z['value'] - df_z['mean']) / df_z['std']).clip(-self.z_clip, self.z_clip).fillna(0)
        if self.z_abs:
            df_z['z'] = df_z['z'].abs()
        # append last z_smooth_n rows of zscores to history table
        df_z_wide = df_z[['z']].reset_index().pivot_table(values='z', columns='index')
        self.df_z_history = self.df_z_history.append(df_z_wide, sort=True).tail(self.z_smooth_n)
        # get average zscore for last z_smooth_n for each metric
        df_z_smooth = (self.df_z_history.melt(value_name='z').groupby('index')['z'].mean() * 100).to_frame()
        df_z_smooth['3sigma'] = np.where(abs(df_z_smooth['z']) > 300, 1, 0)
        # create data dict for z scores
        data_z = df_z_smooth['z'].add_suffix('_z').to_dict()
        # aggregate to chart level if specified
        if self.mode == 'per_chart':
            df_z_smooth['chart'] = ['.'.join(x[0:2]) + '_z' for x in df_z_smooth.index.str.split('.').to_list()]
            if self.per_chart_agg == 'absmax':
                data_z = list(df_z_smooth.groupby('chart').agg({'z': lambda x: max(x, key=abs)})['z'].to_dict().values())[0]
            else:
                data_z = list(df_z_smooth.groupby('chart').agg({'z': [self.per_chart_agg]})['z'].to_dict().values())[0]
        # create data dict for 3sigma flags
        data_3sigma = {}
        for k in data_z:
            data_3sigma[k.replace('_z','_3sigma')] = 1 if abs(data_z[k]) > 300 else 0

        return data_z, data_3sigma

    def get_data(self):
        # train model if needed
        if self.runs_counter <= self.burn_in or self.runs_counter % self.train_every_n == 0:
            self.train_model()
        # get latest data
        df_allmetrics = get_allmetrics(self.host, self.charts_in_scope, wide=True, col_sep='.').transpose()
        # create data dicts
        data_z, data_3sigma = self.create_data(df_allmetrics)
        data = {**data_z, **data_3sigma}
        self.validate_charts('z', data_z, divisor=100)
        self.validate_charts('3sigma', data_3sigma)
        
        return data
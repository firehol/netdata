# Description: base for netdata python.d plugins
# Author: Pawel Krupa (paulfantom)

from time import time
import sys


class BaseService(object):
    def __init__(self,name=None,configuration=None):
        if configuration is None:
            self.error("BaseService: no configuration parameters supplied. Cannot create Service.")
            raise RuntimeError
        else:
            self._extract_base_config(configuration)
            self._create_timetable()
            self.execution_name = ""

    def _extract_base_config(self,config):
        self.update_every = int(config['update_every'])
        self.priority = int(config['priority'])
        self.retries = int(config['retries'])
        self.retries_left = self.retries

    def _create_timetable(self,freq=None):
        if freq is None:
            freq = self.update_every
        now = time()
        self.timetable = {'last' : now,
                          'next' : now - (now % freq) + freq,
                          'freq' : freq}


    def error(self, msg, exception=""):
        if exception != "":
            exception = " " + str(exception).replace("\n"," ")
        sys.stderr.write(str(msg)+exception+"\n")
        sys.stderr.flush()

    def check(self):
        # TODO notify about not overriden function
        self.error("Where is your check()?")
        return False

    def create(self):
        # TODO notify about not overriden function
        self.error("Where is your create()?")
        return False

    def update(self):
        # TODO notify about not overriden function
        self.error("Where is your update()?")
        return False

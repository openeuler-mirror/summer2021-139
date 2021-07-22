import os
import time
import argparse
try:
    from urllib.parse import urlencode
    from urllib.request import urlopen, Request
    from urllib.error import HTTPError
except ImportError:
    from urllib import urlencode
    from urllib2 import urlopen, Request, HTTPError

from hwcompatible.test import Test
from hwcompatible.command import Command
from hwcompatible.document import CertDocument
from hwcompatible.env import CertEnv

class SPDKTest(Test):
    def __init__(self):
        Test.__init__(self)
        self.requirements = ['test-pmd']
        self.subtests = []
        self.server_ip = None

    def test_speed(self):
        pass

    def test_latency(self):
        pass

    def test_cpu_usage(self):
        pass




import os
import time
import argparse
try:
    from urllib.parse import urlencode
    from urllib.request import urlopen, Request
    from urllib.error import HTTPError
except ImportError:
    from urllib import urlencode
    # from urllib2 import urlopen, Request, HTTPError

from hwcompatible.test import Test
from hwcompatible.command import Command
from hwcompatible.document import CertDocument
from hwcompatible.env import CertEnv

# TODO: do we need vfio modules?
class DPDKTest(Test):
    def __init__(self):
        Test.__init__(self)
        self.requirements = ['test-pmd']
        self.subtests = [self.test_setup, self.test_speed,
                self.test_latency, self.test_cpu_usage]
        self.server_ip = None
        self.numa = self._is_numa()


    def test_setup(self):
        if not self._check_hugepage_allocate():
            print("[X] No hugepage allocated.")
            return False
        if not self._check_hugepage_mount():
            print("[X] No hugepage mounted.")
            return False

        print("Hugepage successfully configured.")
        return True

    def test_speed(self):
        pass

    def test_latency(self):
        pass

    def test_cpu_usage(self):
        pass

    def probe_device(self):
        pass

    def setup_device(self):
        pass

    def _check_hugepage_allocate(self):
        if not self.numa:
            hugepagedir = '/sys/kernel/mm/hugepages/'
        else:
            numaid = 0 # TODO: detect numaid
            hugepagedir = '/sys/devices/system/node/node%d/hugepages/' % numaid

        for (_, dirs, _) in os.walk(hugepagedir):
            break

        for directory in dirs:
            comm = Command("cat %s" % hugepagedir + directory + '/nr_hugepages')
            nb = comm.read()
            if int(nb) != 0:
                return True
        
        return False
        # return false when
        # 1. no files in hugepagedir, 2. no non-zero entry was found

    def _check_hugepage_mount(self):
        comm = Command("mount | grep ^hugetlb") # TODO: check this
        comm.start()
        if not comm.readline():
            return False
        return True

    def _is_numa(self):
        '''Test if numa is used on this system'''
        return os.path.exists('/sys/devices/system/node')

    def set_hugepage(self):
        pass

    def call_remote_server(self, cmd, act='start'):
        """
        Connect to the server somehow. 
        """
        pass

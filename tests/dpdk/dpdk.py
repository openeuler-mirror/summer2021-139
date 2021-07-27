import os
import time
import argparse
import glob

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

from . import hugepages as hp
from . import devbind as db
# import hugepages as hp

# TODO: do we need vfio modules?
class DPDKTest(Test):
    def __init__(self):
        print("This is a basic test based on DPDK 20.11.0.\n"
        "DPDK version should be newer than this minimum to avoid unintended behaviour")
        Test.__init__(self)
        self.requirements = ['test-pmd']
        self.subtests = [self.test_setup, self.test_speed,
                self.test_latency, self.test_cpu_usage]
        self.server_ip = None
        self.numa = hp.is_numa()
        # list of suported DPDK drivers
        self.supported_modules = ["igb_uio", "vfio-pci", "uio_pci_generic"]


    def test_setup(self):
        if not self._check_hugepage_allocate():
            print("[X] No hugepage allocated.")
            return False
        if not self._check_hugepage_mount():
            print("[X] No hugepage mounted.")
            return False

        print("[.] Hugepage successfully configured.")
        self._show_hugepage()

        if not self._check_lsmod():
            print("[X] No required kernel module was found (uio, igb_uio or vfio).")
            return False
        print("[.] kernel module check done.")

        return True

    def test_speed(self):
        '''test (single-core) DPDK speed'''
        if not self.call_remote_server("test-pmd"):
            print("[X] start DPDK server failed.")
            return False
        
        print("[+] Testing speed...")
        pass

    def test_latency(self):
        pass

    def test_cpu_usage(self):
        pass

    def probe_device(self):
        pass

    def setup_device(self):
        pass

    def run(self):
        pass

    def _show_hugepage(self):
        if self.numa:
            hp.show_numa_pages()
        else:
            hp.show_non_numa_pages()

    def _check_hugepage_allocate(self):
        return hp.check_hugepage_allocate(self.numa)
    
    def _check_hugepage_mount(self):
        mounted = hp.get_mountpoints()
        if mounted:
            return True
        else:
            return False

    def _check_lsmod(self):
        return db.is_module_loaded(self.supported_modules)

    def call_remote_server(self, cmd, act='start'):
        """
        Connect to the server somehow. 
        """
        pass
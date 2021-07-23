import os
import time
import argparse
import glob
from math import log2
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
        print("This is a basic test based on DPDK 20.11.0.\n"
        "DPDK version should be newer than this minimum to avoid unintended behaviour")
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
        self.show_hugepage()

        if not self._check_lsmod():
            print("[X] No required kernel module was found (uio, igb_uio or vfio).")
            return False
        print("Found kernel module.")

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
            for directory in dirs:
                comm = Command("cat %s" % hugepagedir + directory + '/nr_hugepages')
                nb = comm.read()
                if int(nb) != 0:
                    return True
            break
            
        return False
        # return false when
        # 1. no files in hugepagedir, 2. no non-zero entry was found

    def _check_hugepage_mount(self):
        comm = Command("mount | grep ^hugetlb") # TODO: check this
        comm.start()
        if not comm.readline():
            return False
        return True
    
    def show_hugepage(self):
        if self.numa:
            self._show_numa_pages()
        else:
            self._show_non_numa_pages()

    def _show_numa_pages(self):
        print('Node Pages Size Total')
        for numa_path in glob.glob('/sys/devices/system/node/node*'):
            node = numa_path[29:]  # slice after /sys/devices/system/node/node
            path = numa_path + '/hugepages/'
            for hdir in os.listdir(path):
                comm = Command("cat %s" % path + hdir + '/nr_hugepages')
                pages = int(comm.read())
                if pages > 0:
                    kb = int(hdir[10:-2])  # slice out of hugepages-NNNkB
                    print('{:<4} {:<5} {:<6} {}'.format(node, pages,
                            self.fmt_memsize(kb),
                            self.fmt_memsize(pages * kb)))

    def _show_non_numa_pages(self):
        '''Show huge page reservations on non numa system'''
        print('Pages Size Total')
        hugepagedir = '/sys/kernel/mm/hugepages/'
        for hdir in os.listdir(hugepagedir):
            comm = Command("cat %s" % hugepagedir + hdir + '/nr_hugepages')
            pages = int(comm.read())
            if pages > 0:
                kb = int(hdir[10:-2])
                print('{:<5} {:<6} {}'.format(pages, self.fmt_memsize(kb),
                        self.fmt_memsize(pages * kb)))
    
    def _check_lsmod(self):
        comm = Command("lsmod | grep -E '^uio +'")
        comm.start()
        if not comm.readline():
            return False
        
        comm = Command("lsmod | grep -E '^igb_uio +|^vfio +'")
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

    def fmt_memsize(self, kb):
        '''format memsize. this is a code snippit from dpdk repo'''
        BINARY_PREFIX = "KMG"
        logk = int(log2(kb) / 10)
        suffix = BINARY_PREFIX[logk]
        unit = 2 ** (logk * 10)
        return '{}{}b'.format(int(kb / unit), suffix)
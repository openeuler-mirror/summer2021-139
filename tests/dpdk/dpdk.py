import os
import time
import argparse
import glob
import json

try:
    from urllib.parse import urlencode
    from urllib.request import urlopen, Request
    from urllib.error import HTTPError
except ImportError:
    from urllib import urlencode
    # from urllib2 import urlopen, Request, HTTPError

from hwcompatible.test import Test
from hwcompatible.command import Command, CertCommandError
from hwcompatible.document import CertDocument
from hwcompatible.env import CertEnv

import hugepages as hp
import devbind as db

# TODO: do we need vfio modules?
class DPDKTest(Test):
    def __init__(self):
        print("This is a basic test based on DPDK 20.11.0.\n"
        "DPDK version should be newer than this minimum to avoid unintended behaviour")
        Test.__init__(self)
        self.requirements = []
        self.subtests = [self.test_setup, self.test_speed,
                self.test_latency, self.test_cpu_usage]
        self.server_ip = None
        self.numa = hp.is_numa()
        # list of suported DPDK drivers
        self.supported_modules = ["igb_uio", "vfio-pci", "uio_pci_generic"]
        self.test_dir = os.path.dirname(os.path.realpath(__file__))
        self.dut = '' # name the the device under test
        self.portmask = "0xffff"


    def setup(self, args=None):
        """
        Initialization before test
        :return:
        """
        self.args = args or argparse.Namespace()
        self.dut = getattr(self.args, 'device', None)
        portid = int(self.dut.get_property("PORTNB"))
        self.portmask = "0x" + str(2 ** (portid - 1))
        self.server_ip = CertDocument(CertEnv.certificationfile).get_server()
        self.peermac = ""

    def test(self):
        """
        test case
        :return:
        """
        if not self.test_setup():
            return False

        # use dpdk-testpmd icmpecho as a receive side 
        if not self.call_remote_server('dpdk-testpmd', 'start'):
            print("[X] start dpdk-testpmd server failed."
            "Please check your dpdk installation on server.")
            return False

        if not self.test_speed():
            return False
        
        if not self.test_latency():
            return False
        
        if not self.call_remote_server('dpdk-testpmd', 'stop'):
            print("[X] Stop dpdk-testpmd server failed.")
            return False

        return True

    def test_setup(self):
        if not self._check_hugepage_allocate():
            print("[X] No hugepage allocated.")
            return False
        if not self._check_hugepage_mount():
            print("[X] No hugepage mounted.")
            return False

        print("[.] Hugepage successfully configured.")
        self._show_hugepage()

        # if not self._check_lsmod():
        #     print("[X] No required kernel module was found (uio, igb_uio or vfio).")
        #     return False
        # print("[.] kernel module check done.")

        return True

    def test_speed(self):
        '''test (single-core) DPDK speed'''
        print("Please wait while speed test is running...")
        self.packet_size = 1514
        try:
            comm = Command("cd %s; ./build/tx -l 0 -n 1 -d /usr/local/lib64 -- --peer %s -p %s -l %d --tx-mode"
                    % (self.test_dir, self.peermac, self.portmask, self.packet_size))
            res = comm.get_str(regex="tx-pps: [0-9.]*", single_line=False)
            pps = float(res.split(':')[-1].strip())
            print("[.] The average speed is around %f Mbps" % (8 * self.packet_size * pps / 1e6))
        except CertCommandError as concrete_error:
            print(concrete_error)
            print("[X] Speed test fail.\n")
            return False

        return True

    def test_latency(self):
        print('[+] running dpdk latency test...')
        try:
            comm = Command("cd %s; ./build/tx -l 0 -n 1 -d /usr/local/lib64 -- --peer %s -p %s --latency-mode"
                    % (self.test_dir, self.peermac, self.portmask))
            rttstrs = comm.get_str(regex="rtt: [0-9.]*ms", single_line=False, return_list=True)
            if not rttstrs or len(rttstrs) == 0:
                print("[X] no response from server.")
                return False
            # rtt in ms
            rtt = [float(res.split(':')[-1].strip()[:-2]) for res in rttstrs]
            rtt_avg = sum(rtt) / len(rtt)
            print("[.] Latency test done. The average latency is around %.4f ms" % rtt_avg)
        except CertCommandError as concrete_error:
            print(concrete_error)
            print("[X] latency test fail.")
            return False

        return True

    def test_cpu_usage(self):
        pass

    def setup_device(self):
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
        form = dict()
        form['cmd'] = cmd
        url = 'http://%s/api/dpdk/%s' % (self.server_ip, act)
        data = urlencode(form).encode('utf8')
        headers = {
            'Content-type': 'application/x-www-form-urlencoded',
            'Accept': 'text/plain'
        }
        try:
            request = Request(url, data=data, headers=headers)
            response = urlopen(request)
        except Exception as concrete_error:
            print(concrete_error)
            return False
        if act == 'start':
            self.peermac = json.loads(response.read())['mac']

        print("Status: %u %s" % (response.code, response.msg))
        return int(response.code) == 200
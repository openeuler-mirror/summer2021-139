import os
import argparse
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
# import devbind as db

# TODO: do we need vfio modules?
class DPDKTest(Test):
    def __init__(self):
        Test.__init__(self)
        self.requirements = []
        self.subtests = [self.test_setup, self.test_speed,
                self.test_latency]
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

        if os.system("dpdk-hugepages.py --setup 2G") != 0:
            print("Unable to run dpdk-hugepage script. Please check your dpdk installation.")

        devtype = self._get_dev_name_type(self.dut)
        # get pci address
        if devtype == 'ib':
            self.pci_address = self._get_pci_of_device(self.dut)
        else:
            self.pci_address = self.dut.get_name()

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
            "Please check your server configuration.")
            return False

        if not self.test_speed():
            if not self.call_remote_server('dpdk-testpmd', 'stop'):
                print("[X] Stop dpdk-testpmd server failed.")
            return False
        
        if not self.test_latency():
            if not self.call_remote_server('dpdk-testpmd', 'stop'):
                print("[X] Stop dpdk-testpmd server failed.")
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
            comm = Command("cd %s; ./build/tx -l 0 -n 1 -w %s -- --peer %s -p 0x1 -l %d --tx-mode"
                    % (self.test_dir, self.pci_address, self.peermac, self.packet_size))
            print(comm.command)
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
            # TODO: -w is deprecated since DPDK 20. I use -w for compatibility with version
            # before 20. Consider using -a instead
            comm = Command("cd %s; ./build/tx -l 0 -n 1 -w %s -- --peer %s -p 0x1 --latency-mode"
                    % (self.test_dir, self.pci_address, self.peermac))
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

    # def _dev_unbind(self, interface=None):
    #     if interface == None:
    #         return
        
    #     # os.system("dpdk-devbind.py -u  %s" % (self.pci_address))
    #     os.system("dpdk-devbind.py -b %s %s" % (self.old_driver, self.pci_address))
    #     os.system("ip link set up %s" % (interface))
    #     return

    # def _dev_bind(self, interface=None):
    #     if interface == None:
    #         return

    #     drivers = ['uio_pci_generic', 'igb_uio', 'vfio_pci']
    #     if os.system("modprobe uio_pci_generic"):
    #         print("uio_pci_generic is not supported. trying without it")
        
    #     if os.system("ip link set down %s" % (interface)):
    #         print("Unable to set this device down, is this device currently in use?")
    #         return

    #     for driver in drivers:
    #         if os.system("dpdk-devbind.py -b %s %s" % (driver, interface)) == 0:
    #             return
        
    #     print("Bind failed. Please make sure at least one supported driver is available.")
    #     return


    def _get_dev_name_type(self, device):
        '''
        for ethernet devices, we use PCI address,
        for IB devices, we use interface name
        '''
        name = device.get_name()
        if ':' in name:
            return 'eth'
        else:
            return 'ib'

    def _get_pci_of_device(self, device):
        name = device.get_name()
        comm = Command("ethtool -i %s" % (name))
        comm.start()
        pci = ""
        while True:
            line = comm.readline()
            if line.split(":", 1)[0].strip() == "bus-info":
                pci = line.split(":", 1)[1].strip()
                break
        return pci

    # def _get_driver_of_device(self, device):
    #     name =device.get_name()
    #     comm = Command("ethtool -i %s" % (name))
    #     comm.start()
    #     driver = ""
    #     while True:
    #         line = comm.readline()
    #         if line.split(":", 1)[0].strip() == "driver":
    #             pci = line.split(":", 1)[1].strip()
    #             break
    #     return driver

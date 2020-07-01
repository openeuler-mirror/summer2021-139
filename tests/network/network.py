#!/usr/bin/env python
# coding: utf-8

# Copyright (c) 2020 Huawei Technologies Co., Ltd.
# oec-hardware is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# PURPOSE.
# See the Mulan PSL v2 for more details.
# Create: 2020-04-01

import os
import time
import argparse
import base64
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


class NetworkTest(Test):
    """
    Network Test
    """
    def __init__(self):
        Test.__init__(self)
        self.args = None
        self.cert = None
        self.device = None
        self.requirements = ['ethtool', 'iproute', 'psmisc', 'qperf']
        self.subtests = [self.test_ip_info, self.test_eth_link, self.test_icmp,
                         self.test_udp_tcp, self.test_http]
        self.interface = None
        self.other_interfaces = []
        self.server_ip = None
        self.retries = 3
        self.speed = 1000   # Mb/s
        self.target_bandwidth_percent = 0.8
        self.testfile = 'testfile'

    def ifdown(self, interface):
        """
        Judge whether the specified interface is closed successfully
        :param interface:
        :return:
        """
        os.system("ip link set down %s" % interface)
        for _ in range(5):
            if 0 == os.system("ip link show %s | grep 'state DOWN'" % interface):
                return True
            time.sleep(1)
        return False

    def ifup(self, interface):
        """
        Judge whether the specified interface is enabled successfully
        :param interface:
        :return:
        """
        os.system("ip link set up %s" % interface)
        for _ in range(5):
            time.sleep(1)
            if 0 == os.system("ip link show %s | grep 'state UP'" % interface):
                return True
        return False

    def get_other_interfaces(self):
        """
        Get other interfaces
        :return:
        """
        ignore_interfaces = ['^lo', '^v', 'docker', 'br', 'bond']
        cmd = "ip route show default | awk '/default/ {print $5}'"
        c = Command(cmd)
        management_interface = c.read()
        if management_interface:
            ignore_interfaces.append(management_interface)
            # print(cmd)
            # print("Management interface: %s" % management_interface)

        ignore_pattern = '|'.join(ignore_interfaces)
        # print(ignore_pattern)
        cmd = "ls /sys/class/net/ | grep -vE '%s'" % ignore_pattern
        # print(cmd)
        c = Command(cmd)
        try:
            c.run()
            return c.output
        except Exception as e:
            print(e)
            return []

    def set_other_interfaces_down(self):
        """
        Judge whether the interface is closed
        :return:
        """
        for interface in self.other_interfaces:
            if not self.ifdown(interface):
                return False
        return True

    def set_other_interfaces_up(self):
        """
        Set other interfaces to up
        :return:
        """
        for interface in self.other_interfaces:
            # Not ifup(), as some interfaces may not be linked
            os.system("ip link set up %s" % interface)
            # os.system("ip link | grep -w %s" % interface)
        return True

    def get_speed(self):
        """
        Get speed on the interface
        :return:
        """
        c = Command("ethtool %s" % self.interface)
        pattern = r".*Speed:\s+(?P<speed>\d+)Mb/s"
        try:
            speed = c.get_str(pattern, 'speed', False)
            return int(speed)
        except Exception as e:
            print("[X] No speed found on the interface.")
            return None

    def get_interface_ip(self):
        """
        Get interface ip
        :return:
        """
        c = Command("ip addr show %s" % self.interface)
        pattern = r".*inet.? (?P<ip>.+)/.*"
        try:
            ip = c.get_str(pattern, 'ip', False)
            return ip
        except Exception as e:
            print("[X] No available ip on the interface.")
            return None

    def test_icmp(self):
        """
        Test ICMP
        :return:
        """
        count = 500
        c = Command("ping -q -c %d -i 0 %s" % (count, self.server_ip))
        pattern = r".*, (?P<loss>\d+\.{0,1}\d*)% packet loss.*"

        for _ in range(self.retries):
            try:
                print(c.command)
                loss = c.get_str(pattern, 'loss', False)
                c.print_output()
                if float(loss) == 0:
                    return True
            except Exception as e:
                print(e)
        return False

    def call_remote_server(self, cmd, act='start', ib_server_ip=''):
        """
        Call remote server
        :param cmd:
        :param act:
        :param ib_server_ip:
        :return:
        """
        form = dict()
        form['cmd'] = cmd
        form['ib_server_ip'] = ib_server_ip
        url = 'http://%s/api/%s' % (self.server_ip, act)
        data = urlencode(form).encode('utf8')
        headers = {
            'Content-type': 'application/x-www-form-urlencoded',
            'Accept': 'text/plain'
        }
        try:
            request = Request(url, data=data, headers=headers)
            response = urlopen(request)
        except Exception as e:
            print(e)
            return False
        print("Status: %u %s" % (response.code, response.msg))
        return int(response.code) == 200

    def test_udp_latency(self):
        """
        Test udp latency
        :return:
        """
        cmd = "qperf %s udp_lat" % self.server_ip
        print(cmd)
        for _ in range(self.retries):
            if 0 == os.system(cmd):
                return True
        return False

    def test_tcp_latency(self):
        cmd = "qperf %s tcp_lat" % self.server_ip
        print(cmd)
        for _ in range(self.retries):
            if 0 == os.system(cmd):
                return True
        return False

    def test_tcp_bandwidth(self):
        """
        Test tcp bandwidth
        :return:
        """
        cmd = "qperf %s tcp_bw" % self.server_ip
        print(cmd)
        c = Command(cmd)
        pattern = r"\s+bw\s+=\s+(?P<bandwidth>[\.0-9]+ [MG]B/sec)"
        for _ in range(self.retries):
            try:
                bandwidth = c.get_str(pattern, 'bandwidth', False)
                bw = bandwidth.split()
                if 'GB' in bw[1]:
                    bandwidth = float(bw[0]) * 8 * 1024
                else:
                    bandwidth = float(bw[0]) * 8

                target_bandwidth = self.target_bandwidth_percent * self.speed
                print("Current bandwidth is %.2fMb/s, target is %.2fMb/s" %
                      (bandwidth, target_bandwidth))
                if bandwidth > target_bandwidth:
                    return True
            except Exception as e:
                print(e)
        return False

    def create_testfile(self):
        """
        Create testfile
        :return:
        """
        bs = 128
        count = self.speed/8
        cmd = "dd if=/dev/urandom of=%s bs=%uk count=%u" % (self.testfile, bs, count)
        return 0 == os.system(cmd)

    def test_http_upload(self):
        """
        Test http upload
        :return:
        """
        form = {}
        size = os.path.getsize(self.testfile)
        filename = os.path.basename(self.testfile)

        try:
            with open(self.testfile, 'rb') as f:
                filetext = base64.b64encode(f.read())
        except Exception as e:
            print(e)
            return False

        form['filename'] = filename
        form['filetext'] = filetext
        url = 'http://%s/api/file/upload' % self.server_ip
        data = urlencode(form).encode('utf8')
        headers = {
            'Content-type': 'application/x-www-form-urlencoded',
            'Accept': 'text/plain'
        }

        time_start = time.time()
        try:
            request = Request(url, data=data, headers=headers)
            response = urlopen(request)
        except Exception as e:
            print(e)
            return False
        time_stop = time.time()
        time_upload = time_stop - time_start

        print("Status: %u %s" % (response.code, response.msg))
        print(response.headers)
        print("Upload %s in %.2fs, %.2f MB/s" % (filename, time_upload,
                                                 size/1000000 / time_upload))
        return True

    def test_http_download(self):
        """
        Test http download
        :return:
        """
        filename = os.path.basename(self.testfile)
        url = "http://%s/files/%s" % (self.server_ip, filename)

        time_start = time.time()
        try:
            response = urlopen(url)
        except Exception as e:
            print(e)
            return False
        time_stop = time.time()
        time_download = time_stop - time_start

        print("Status: %u %s" % (response.code, response.msg))
        print(response.headers)
        filetext = response.read()
        try:
            with open(self.testfile, 'wb') as f:
                f.write(filetext)
        except Exception as e:
            print(e)
            return False

        size = os.path.getsize(self.testfile)
        print("Download %s in %.2fs, %.2f MB/s" % (filename, time_download,
                                                   size/1000000 / time_download))
        return True

    def test_udp_tcp(self):
        """
        Test udp tcp
        :return:
        """
        if not self.call_remote_server('qperf', 'start'):
            print("[X] start qperf server failed.")
            return False

        print("[+] Testing udp latency...")
        if not self.test_udp_latency():
            print("[X] Test udp latency failed.")
            return False

        print("[+] Testing tcp latency...")
        if not self.test_tcp_latency():
            print("[X] Test tcp latency failed.")
            return False

        print("[+] Testing tcp bandwidth...")
        if not self.test_tcp_bandwidth():
            print("[X] Test tcp bandwidth failed.")
            return False

        self.call_remote_server('qperf', 'stop')
        return True

    def test_http(self):
        """
        Test http
        :return:
        """
        print("[+] Creating testfile to upload...")
        if not self.create_testfile():
            print("[X] Create testfile failed.")
            return False

        print("[+] Testing http upload(POST)...")
        if not self.test_http_upload():
            print("[X] Test http upload failed.")
            return False

        print("[+] Testing http download(GET)...")
        if not self.test_http_download():
            print("[X] Test http download failed.")
            return False

        return True

    def test_eth_link(self):
        """
        Test eth link
        :return:
        """
        self.other_interfaces = self.get_other_interfaces()
        print("[+] Setting irrelevant interfaces down...")
        if not self.set_other_interfaces_down():
            print("[X] Fail to set irrelevant interfaces down.")
            print("[X] Stop test and restore interfaces up...")
            self.set_other_interfaces_up()
            return False

        print("[+] Setting interface %s down..." % self.interface)
        if not self.ifdown(self.interface):
            print("[X] Fail to set interface %s down." % self.interface)
            return False

        print("[+] Setting interface %s up..." % self.interface)
        if not self.ifup(self.interface):
            print("[X] Fail to set interface %s up." % self.interface)
            return False

        self.speed = self.get_speed()
        if self.speed:
            print("[.] The speed of %s is %sMb/s." % (self.interface, self.speed))
        else:
            print("[X] Fail to get speed of %s." % self.interface)
            return False

        return True

    def test_ip_info(self):
        """
        Test ip info
        :return:
        """
        if not self.interface:
            print("[X] No interface assigned.")
            return False
        print("[.] The test interface is %s." % self.interface)

        if not self.server_ip:
            print("[X] No server ip assigned.")
            return False
        print("[.] The server ip is %s." % self.server_ip)

        client_ip = self.get_interface_ip()
        if not client_ip:
            print("[X] No available ip on %s." % self.interface)
            return False
        print("[.] The client ip is %s on %s." % (client_ip, self.interface))

        return True

    def setup(self, args=None):
        """
        Initialization before test
        :param args:
        :return:
        """
        self.args = args or argparse.Namespace()
        self.device = getattr(self.args, 'device', None)
        self.interface = self.device.get_property("INTERFACE")

        self.cert = CertDocument(CertEnv.certificationfile)
        self.server_ip = self.cert.get_server()

    def test(self):
        """
        test case
        :return:
        """
        for subtest in self.subtests:
            if not subtest():
                return False
        return True

    def teardown(self):
        """
        Environment recovery after test
        :return:
        """
        print("[.] Stop all test servers...")
        self.call_remote_server('all', 'stop')

        print("[.] Restore interfaces up...")
        self.set_other_interfaces_up()

        print("[.] Test finished.")

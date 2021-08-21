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

"""Test task management"""

import os
import sys
import string
import random
import argparse

from .test import Test
from .env import CertEnv
from .command import Command, CertCommandError
from .commandUI import CommandUI
from .log import Logger
from .reboot import Reboot


class Job():
    """
    Test task management
    """
    def __init__(self, args=None):
        """
        Creates an instance of Job class.

        :param args: the job configuration, usually set by command
                     line options and argument parsing
        :type args: :class:`argparse.Namespace`
        """
        self.args = args or argparse.Namespace()
        self.test_factory = getattr(args, "test_factory", [])
        self.test_suite = []
        self.job_id = ''.join(random.sample(string.ascii_letters + string.digits, 10))
        self.com_ui = CommandUI()
        self.subtests_filter = getattr(args, "subtests_filter", None)

        self.test_parameters = None
        if "test_parameters" in self.args:
            self.test_parameters = {}
            for parameter_name, parameter_value in self.args.test_parameters:
                self.test_parameters[parameter_name] = parameter_value

    def discover(self, testname, subtests_filter=None):
        """
        discover test
        :param testname:
        :param subtests_filter:
        :return:
        """
        if not testname:
            print("testname not specified, discover test failed")
            return None

        filename = testname + ".py"
        dirpath = ''
        for (dirpath, dirs, files) in os.walk(CertEnv.testdirectoy):
            if filename in files:
                break
        pth = os.path.join(dirpath, filename)
        if not os.access(pth, os.R_OK):
            return None

        sys.path.insert(0, dirpath)
        # try:
        module = __import__(testname, globals(), locals())
        # except Exception as concrete_error:
        #     print("Error: module import failed for %s" % testname)
        #     print(concrete_error)
        #     return None

        for thing in dir(module):
            test_class = getattr(module, thing)
            try:
                from types import ClassType as ct
            except ImportError:
                ct = type
            if isinstance(test_class, ct) and issubclass(test_class, Test):
                if "test" not in dir(test_class):
                    continue
                if (subtests_filter and not subtests_filter in dir(test_class)):
                    continue
                test = test_class()
                if "pri" not in dir(test):
                    continue
                return test

        return None

    def create_test_suite(self, subtests_filter=None):
        """
        Create test suites
        :param subtests_filter:
        :return:
        """
        if self.test_suite:
            return

        self.test_suite = []
        for test in self.test_factory:
            if test["run"]:
                testclass = self.discover(test["name"], subtests_filter)
                if testclass:
                    testcase = dict()
                    testcase["test"] = testclass
                    testcase["name"] = test["name"]
                    testcase["device"] = test["device"]
                    testcase["status"] = "FAIL"
                    self.test_suite.append(testcase)
                else:
                    if not subtests_filter:
                        test["status"] = "FAIL"
                        print("not found %s" % test["name"])

        if not len(self.test_suite):
            print("No test found")

    def check_test_depends(self):
        """
        Install  dependency packages
        :return: depending
        """
        required_rpms = []
        for tests in self.test_suite:
            for pkg in tests["test"].requirements:
                try:
                    Command("rpm -q " + pkg).run_quiet()
                except CertCommandError:
                    if not pkg in required_rpms:
                        required_rpms.append(pkg)

        if len(required_rpms):
            print("Installing required packages: %s" % ", ".join(required_rpms))
            try:
                cmd = Command("yum install -y " + " ".join(required_rpms))
                cmd.echo()
            except CertCommandError as concrete_error:
                print(concrete_error)
                print("Fail to install required packages.")
                return False

        return True

    def _run_test(self, testcase, subtests_filter=None):
        """
        Start a testing item
        :param testcase:
        :param subtests_filter:
        :return:
        """
        name = testcase["name"]
        if testcase["device"].get_name():
            name = testcase["name"] + "-" + testcase["device"].get_name()
        logname = name + ".log"
        reboot = None
        test = None
        logger = None
        try:
            test = testcase["test"]
            logger = Logger(logname, self.job_id, sys.stdout, sys.stderr)
            logger.start()
            if subtests_filter:
                return_code = getattr(test, subtests_filter)()
            else:
                print("----  start to run test %s  ----" % name)
                args = argparse.Namespace(device=testcase["device"], logdir=logger.log.dir)
                test.setup(args)
                if test.reboot:
                    reboot = Reboot(testcase["name"], self, test.rebootup)
                    return_code = False
                    if reboot.setup():
                        return_code = test.test()
                else:
                    return_code = test.test()
        except Exception as concrete_error:
            print(concrete_error)
            return_code = False

        if reboot:
            reboot.clean()
        if not subtests_filter:
            test.teardown()
        logger.stop()
        print("")
        return return_code

    def run_tests(self, subtests_filter=None):
        """
        Start testing
        :param subtests_filter:
        :return:
        """
        if not len(self.test_suite):
            print("No test to run.")
            return

        self.test_suite.sort(key=lambda k: k["test"].pri)
        for testcase in self.test_suite:
            if self._run_test(testcase, subtests_filter):
                testcase["status"] = "PASS"
            else:
                testcase["status"] = "FAIL"

    def run(self):
        """
        Test entrance
        :return:
        """
        logger = Logger("job.log", self.job_id, sys.stdout, sys.stderr)
        logger.start()
        self.create_test_suite(self.subtests_filter)
        if not self.check_test_depends():
            print("Required rpm package not installed, test stopped.")
        else:
            self.run_tests(self.subtests_filter)
        self.save_result()
        logger.stop()
        self.show_summary()

    def show_summary(self):
        """
        Command line interface display summary
        :return:
        """
        print("-------------  Summary  -------------")
        for test in self.test_factory:
            if test["run"]:
                name = test["name"]
                if test["device"].get_name():
                    name = test["name"] + "-" + test["device"].get_name()
                if test["status"] == "PASS":
                    print(name.ljust(33) + "\033[0;32mPASS\033[0m")
                else:
                    print(name.ljust(33) + "\033[0;31mFAIL\033[0m")
        print("")

    def save_result(self):
        """
        Get test status
        :return:
        """
        for test in self.test_factory:
            for testcase in self.test_suite:
                if test["name"] == testcase["name"] and test["device"].path == \
                        testcase["device"].path:
                    test["status"] = testcase["status"]

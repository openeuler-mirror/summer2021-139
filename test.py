from tests.dpdk.dpdk import DPDKTest

tmp = DPDKTest()

tmp.numa = True
tmp.test_setup()

tmp.numa = False
tmp.test_setup()
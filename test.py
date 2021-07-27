from tests.dpdk.dpdk import DPDKTest
from tests.dpdk.devbind import get_devices
from tests.dpdk.devbind import network_devices

tmp = DPDKTest()

tmp.numa = True
tmp.test_setup()

tmp.numa = False
tmp.test_setup()

dv = get_devices(network_devices)
print(dv)
assert(len([d for d in dv.keys()]) == 3)
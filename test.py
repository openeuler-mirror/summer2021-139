from tests.dpdk.dpdk import DPDKTest
from tests.dpdk.devbind import get_devices
from tests.dpdk.devbind import network_devices
from tests.dpdk.devbind import is_device_bind
from hwcompatible.dpdkutil import get_devices_with_compatible_driver

tmp = DPDKTest()

tmp.numa = True
tmp.test_setup()

tmp.numa = False
tmp.test_setup()

dv = get_devices(network_devices)
assert(len([d for d in dv.keys()]) == 3)
print(dv)
is_device_bind()

l = get_devices_with_compatible_driver()
print(l)
import sys
import os
import subprocess
import argparse

from glob import glob
from os.path import exists, basename
from os.path import join as path_join
from tests.dpdk.temp import device_type_match, do_arg_actions

from hwcompatible.command import Command

## some of the codes are ported from dpdk official repo

network_class = {'Class': '02', 'Vendor': None, 'Device': None,
                 'SVendor': None, 'SDevice': None}

ifpga_class = {'Class': '12', 'Vendor': '8086', 'Device': '0b30',
               'SVendor': None, 'SDevice': None}

cavium_pkx = {'Class': '08', 'Vendor': '177d', 'Device': 'a0dd,a049',
              'SVendor': None, 'SDevice': None}

avp_vnic = {'Class': '05', 'Vendor': '1af4', 'Device': '1110',
            'SVendor': None, 'SDevice': None}

supported_modules = ["igb_uio", "vfio-pci", "uio_pci_generic"]
network_devices = [network_class, cavium_pkx, avp_vnic, ifpga_class]

def is_module_loaded(supported_modules):
    comm = Command("lsmod")
    comm.start()

    all_modules = []
    while True:
        line = comm.readline()
        if not line:
            break
        all_modules.append(line.split()[0])

    module_found_nb = 0
    for mod in supported_modules:
        if mod in all_modules:
            print(mod, "found")
            module_found_nb += 1
    
    return not module_found_nb == 0

def is_device_bind():
    global network_devices
    global supported_modules

    devices = get_devices(network_devices)
    if len(devices) == 0:
        print("[X] No interface detected.")
        return False
    for d in devices.keys():
        if "Driver_str" in devices[d]:
            if devices[d]["Driver_str"] in supported_modules:
                return True
    print('[X] No device was bound to DPDK suported drivers.'
            'Try solving this using "dpdk-devbind.py" provided by DPDK installation')
    return False

def get_devices(device_type):
    devices = {}
    dev = {}
    comm = Command("lspci -Dvmmnnk")
    comm.start()

    while True:
        line = comm.readline()
        if not line:
            break

        if line == '\n':
            if device_type_match(dev, device_type):
                if "Driver" in dev.keys(): # for consistency of key names
                    dev["Driver_str"] = dev.pop("Driver")
                if "Module" in dev.keys():
                    dev["Module_str"] = dev.pop("Module")
                devices[dev["Slot"]] = dict(dev)
            dev = {}
        else:
            line = line.strip()
            name, value = line.split("\t", 1)
            value_list = value.rsplit(' ', 1)
            if len(value_list) == 2:
                # String stored in <name>_str
                dev[name.rstrip(":") + '_str'] = value_list[0]
            # Numeric IDs stored in <name>
            dev[name.rstrip(":")] = value_list[len(value_list) - 1] \
                .strip().rstrip("]").lstrip("[")
    
    return devices

def device_type_match(dev, devices_type):
    for i in range(len(devices_type)):
        param_count = len(
            [x for x in devices_type[i].values() if x is not None])
        match_count = 0
        if dev["Class"][0:2] == devices_type[i]["Class"]:
            match_count = match_count + 1
            for key in devices_type[i].keys():
                if key != 'Class' and devices_type[i][key]:
                    value_list = devices_type[i][key].split(',')
                    for value in value_list:
                        if value.strip(' ') == dev[key]:
                            match_count = match_count + 1
            # count must be the number of non None parameters to match
            if match_count == param_count:
                return True
    return False
